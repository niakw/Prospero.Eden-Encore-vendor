// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Native GPU memory registration is real NVMap + guest page-table ownership.
// 4-KiB GMMU mappings and GPU command submission are *not* implemented here.
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include "common/alignment.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/gpu_memory_registration.h"
#include "core/hle/service/am/service/gpu_worker.h"
#include "core/hle/service/am/service/gpu_memory_registration.h"
#include "core/hle/service/am/service/gpu_worker_shared.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"

namespace Service::AM {
namespace {
constexpr std::size_t MaxOwnedGpuMemoryHandles = 4096;
}

IGpuWorker::IGpuWorker(Core::System& system_, std::shared_ptr<Nvidia::Module> module,
                       Kernel::KProcess* process,
                       std::shared_ptr<GpuWorkerSharedState> shared)
    : ServiceFramework{system_, "IGpuWorker"}, m_shared{std::move(shared)},
      m_module{std::move(module)}, m_process{process} {
    m_process->Open(kernel);
    static const FunctionInfo functions[] = {
        {101, &IGpuWorker::CreateGpuAccessibleMemory, "CreateGpuAccessibleMemory"},
        {102, &IGpuWorker::DestroyGpuAccessibleMemory, "DestroyGpuAccessibleMemory"},
        {211, &IGpuWorker::NormalMapGpuMemory4K, "NormalMapGpuMemory4K"},
        {212, &IGpuWorker::NormalModifyGpuMemory4K, "NormalModifyGpuMemory4K"},
        {213, &IGpuWorker::NormalUnmapGpuMemory4K, "NormalUnmapGpuMemory4K"},
        {214, &IGpuWorker::CreateSparseRegion, "CreateSparseRegion"},
        {215, &IGpuWorker::DestroySparseRegion, "DestroySparseRegion"},
        {216, &IGpuWorker::ExecuteSparseMapRequests, "ExecuteSparseMapRequests"},
    };
    RegisterHandlersTipc(functions);
}

IGpuWorker::~IGpuWorker() {
    // Release GPU VA mappings before any backing NVMap handle or process
    // memory is freed. A moved worker client retains shared FD ownership.
    for (const auto& [key, mapping] : m_mappings.Mappings()) {
        const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(
            static_cast<Nvidia::DeviceFD>(mapping.address_space));
        if (as && (mapping.sparse
                       ? as->ReleaseNativeSparse4K(mapping.address, mapping.size)
                       : as->UnmapNative4K(mapping.address, mapping.size)) !=
                      Nvidia::NvResult::Success)
            LOG_ERROR(Service_AM, "NS2 native GPU VA unmap failed as={} VA={:#x}",
                      mapping.address_space, mapping.address);
    }
    // Never force an unlock of memory still retained externally.
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    for (const u32 id : m_memory) {
        if (const auto info = nvmap.FreeHandle(id, false); info && info->can_unlock) {
            const auto result = m_process->GetPageTable().UnlockForDeviceAddressSpace(
                info->address, info->size);
            if (result.IsError()) {
                LOG_ERROR(Service_AM, "NS2 GPU memory unlock failed: {:#x}", result.raw);
            }
        }
    }
    if (m_nv_session)
        m_module->GetContainer().CloseSession(Nvidia::NvCore::SessionId{*m_nv_session});
    m_process->Close(kernel);
}

Result IGpuWorker::HandleSyncRequest(Kernel::KServerSession& session,
                                     HLERequestContext& ctx) {
    if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand())) {
        return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
    }
    const auto guard = LockService();
    InvokeRequestTipc(ctx);
    // 216 remaps bounded, verified NVMap handles into native sparse VA;
    // Ampere graphics queues and GPU submission remain unsupported.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
    return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
}

namespace GW = GpuWorkerProtocol;
namespace {
Result TranslateMappingStatus(GW::LedgerStatus status) {
    switch (status) {
    case GW::LedgerStatus::Success: return ResultSuccess;
    case GW::LedgerStatus::Invalid: return Kernel::ResultInvalidCombination;
    case GW::LedgerStatus::Overlap: return Kernel::ResultInvalidState;
    case GW::LedgerStatus::NotFound: return Kernel::ResultNotFound;
    case GW::LedgerStatus::OutOfResource: return Kernel::ResultOutOfResource;
    }
    return Kernel::ResultInvalidState;
}
} // namespace

void IGpuWorker::NormalMapGpuMemory4K(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    std::array<GW::U32, 18> command{};
    std::copy_n(packet, command.size(), command.begin());
    Result result = Kernel::ResultInvalidCombination;
    u32 selected{};
    if (const auto request = GW::DecodeMap(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->address_spaces.Owns(request->address_space) &&
            m_shared->devices.Get(request->device).has_value() &&
            m_shared->events[request->notification] &&
            std::find(m_memory.begin(), m_memory.end(), request->memory) != m_memory.end()) {
            auto& nvmap = m_module->GetContainer().GetNvMapFile();
            const auto backing = nvmap.GetHandle(request->memory);
            result = Kernel::ResultInvalidCombination;
            if (backing && request->memory_offset <= backing->orig_size &&
                request->size <= backing->orig_size - request->memory_offset) {
                const auto preflight = m_mappings.CanMap(*request);
                result = TranslateMappingStatus(preflight);
                if (preflight == GW::LedgerStatus::Success) {
                    result = Kernel::ResultInvalidState;
                    const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(
                        static_cast<Nvidia::DeviceFD>(request->address_space));
                    if (as && as->MapNative4K(request->memory, request->address,
                                                request->memory_offset, request->size,
                                                request->backend_kind) ==
                                      Nvidia::NvResult::Success) {
                        const auto committed = m_mappings.RecordMapped(*request);
                        if (committed == GW::LedgerStatus::Success) {
                            selected = request->selected_kind;
                            result = ResultSuccess;
                            m_shared->events[request->notification]->Signal(kernel);
                        } else {
                            as->UnmapNative4K(request->address, request->size);
                            result = TranslateMappingStatus(committed);
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(selected);
}

void IGpuWorker::NormalModifyGpuMemory4K(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    std::array<GW::U32, 11> command{};
    std::copy_n(packet, command.size(), command.begin());
    Result result = Kernel::ResultInvalidCombination;
    u32 selected{};
    if (const auto request = GW::DecodeModify(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->address_spaces.Owns(request->address_space) &&
            m_shared->events[request->notification]) {
            const auto preflight = m_mappings.CanModify(*request);
            result = TranslateMappingStatus(preflight);
            if (preflight == GW::LedgerStatus::Success) {
                result = Kernel::ResultInvalidState;
                const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(
                    static_cast<Nvidia::DeviceFD>(request->address_space));
                if (as && as->ModifyNativeKind(request->address, request->size,
                                                request->backend_kind) ==
                                  Nvidia::NvResult::Success) {
                    selected = request->selected_kind;
                    result = ResultSuccess;
                    m_shared->events[request->notification]->Signal(kernel);
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(selected);
}

void IGpuWorker::NormalUnmapGpuMemory4K(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    std::array<GW::U32, 9> command{};
    std::copy_n(packet, command.size(), command.begin());
    Result result = Kernel::ResultInvalidCombination;
    u64 unmapped{};
    if (const auto request = GW::DecodeUnmap(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->address_spaces.Owns(request->address_space) &&
            m_shared->events[request->notification]) {
            const auto preflight = m_mappings.CanUnmap(*request);
            result = TranslateMappingStatus(preflight);
            if (preflight == GW::LedgerStatus::Success) {
                result = Kernel::ResultInvalidState;
                const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(
                    static_cast<Nvidia::DeviceFD>(request->address_space));
                if (as && as->UnmapNative4K(request->address, request->size) ==
                                  Nvidia::NvResult::Success) {
                    m_mappings.RecordUnmapped(*request);
                    unmapped = request->size;
                    result = ResultSuccess;
                    m_shared->events[request->notification]->Signal(kernel);
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push(unmapped);
}

void IGpuWorker::CreateSparseRegion(HLERequestContext& ctx) {
    const auto request = DecodeGpuSparseRegion(
        std::span<const u32>{ctx.CommandBuffer(), 12});
    Result result = Kernel::ResultInvalidCombination;
    if (request) {
        result = Kernel::ResultNotImplemented;
        if (IsValidGpuSparseRegionRange(*request) && request->page_shift == 12 &&
            request->allocation_mode <= 1 && request->sync <= 1 &&
            request->notification < GW::MaxNotifications) {
            const std::scoped_lock lock{m_mutex, m_shared->mutex};
            result = Kernel::ResultNotFound;
            if (m_shared->events[request->notification] &&
                m_shared->address_spaces.Owns(request->address_space) &&
                m_shared->devices.Get(request->device).has_value()) {
                const u64 size = request->end - request->begin + 1;
                const auto preflight = m_mappings.CanReserveSparse(
                    request->address_space, request->device, request->begin, size);
                result = TranslateMappingStatus(preflight);
                if (preflight == GW::LedgerStatus::Success) {
                    result = Kernel::ResultInvalidState;
                    const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(
                        static_cast<Nvidia::DeviceFD>(request->address_space));
                    if (as && as->ReserveNativeSparse4K(request->begin, size) ==
                                  Nvidia::NvResult::Success) {
                        const auto committed = m_mappings.RecordSparse(
                            request->address_space, request->device, request->begin, size);
                        if (committed == GW::LedgerStatus::Success) {
                            result = ResultSuccess;
                            m_shared->events[request->notification]->Signal(kernel);
                        } else {
                            as->ReleaseNativeSparse4K(request->begin, size);
                            result = TranslateMappingStatus(committed);
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::DestroySparseRegion(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    std::array<GW::U32, 9> command{};
    std::copy_n(packet, command.size(), command.begin());
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = GW::DecodeReleaseSparse(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->events[request->notification] &&
            m_shared->address_spaces.Owns(request->address_space)) {
            const auto preflight = m_mappings.CanReleaseSparse(
                request->address_space, request->address, request->size);
            result = TranslateMappingStatus(preflight);
            if (preflight == GW::LedgerStatus::Success) {
                result = Kernel::ResultInvalidState;
                const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(
                    static_cast<Nvidia::DeviceFD>(request->address_space));
                if (as && as->ReleaseNativeSparse4K(request->address, request->size) ==
                              Nvidia::NvResult::Success) {
                    m_mappings.RecordReleasedSparse(
                        request->address_space, request->address, request->size);
                    result = ResultSuccess;
                    m_shared->events[request->notification]->Signal(kernel);
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::ExecuteSparseMapRequests(HLERequestContext& ctx) {
    std::array<GW::U32, 15> command{};
    std::copy_n(ctx.CommandBuffer(), command.size(), command.begin());
    u64 processed{};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = GW::DecodeSparseBatch(command)) {
        result = Kernel::ResultInvalidSize;
        const std::size_t bytes =
            static_cast<std::size_t>(request->count) * sizeof(GpuSparseMapRequest);
        // Bounds must be checked before reading untrusted guest memory.
        if (ctx.GetReadBufferSize() == bytes &&
            ctx.GetWriteBufferSize() >= request->count) {
            const auto input = ctx.ReadBuffer();
            if (input.size() == bytes) {
                const std::scoped_lock lock{m_mutex, m_shared->mutex};
                result = Kernel::ResultNotFound;
                if (m_shared->address_spaces.Owns(request->address_space) &&
                    m_shared->events[request->notification]) {
                    std::vector<Nvidia::Devices::NativeSparseRemapEntry> staged;
                    std::vector<u8> selected;
                    staged.reserve(request->count);
                    selected.reserve(request->count);
                    result = ResultSuccess;
                    for (std::size_t i = 0; i < request->count; ++i) {
                        GpuSparseMapRequest element{};
                        std::memcpy(&element, input.data() + i * sizeof(element),
                                    sizeof(element));
                        const auto entry = DecodeGpuSparseMap(element);
                        if (!entry) {
                            result = Kernel::ResultNotImplemented;
                            break;
                        }
                        const auto range = Nvidia::Devices::DecodeNativeSparseRemap(*entry);
                        if (!range || !m_mappings.ContainsSparse(
                                          request->address_space,
                                          range->address, range->size)) {
                            result = Kernel::ResultInvalidCurrentMemory;
                            break;
                        }
                        u8 effective = element.fallback_kind;
                        if (range->handle) {
                            if (std::find(m_memory.begin(), m_memory.end(),
                                          range->handle) == m_memory.end()) {
                                result = Kernel::ResultNotFound;
                                break;
                            }
                            const auto kind = Nvidia::Devices::SelectNativeMemoryKind(
                                element.kind, element.fallback_kind);
                            if (!kind) {
                                result = Kernel::ResultNotImplemented;
                                break;
                            }
                            auto& nvmap = m_module->GetContainer().GetNvMapFile();
                            const auto handle = nvmap.GetHandle(range->handle);
                            if (!handle || !handle->allocated ||
                                range->source_offset > handle->orig_size ||
                                range->size > handle->orig_size - range->source_offset) {
                                result = Kernel::ResultInvalidCurrentMemory;
                                break;
                            }
                            effective = kind->selected;
                        }
                        staged.push_back(*entry);
                        selected.push_back(effective);
                    }
                    if (result.IsSuccess()) {
                        result = Kernel::ResultInvalidState;
                        const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(
                            static_cast<Nvidia::DeviceFD>(request->address_space));
                        if (as && as->RemapNativeSparse(staged) == Nvidia::NvResult::Success) {
                            processed = request->count;
                            ctx.WriteBuffer(selected);
                            result = ResultSuccess;
                            m_shared->events[request->notification]->Signal(kernel);
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push(processed);
}

Result IGpuWorker::RegisterMemory(u64 begin, u64 end, u32 flags,
                                  u32 device, u32& id) {
    // Caller holds BOTH m_mutex and m_shared->mutex until NVMap commit.
    R_UNLESS(flags == 0, Kernel::ResultNotImplemented);
    R_UNLESS(device != 0 && device < GpuSessionProtocol::MaxDevices &&
             m_shared->devices.Get(device).has_value(), Kernel::ResultNotFound);
    R_UNLESS(end >= begin && end != std::numeric_limits<u64>::max(),
             Kernel::ResultInvalidSize);
    const u64 size = end - begin + 1;
    R_UNLESS(Common::IsAligned(begin, Kernel::PageSize),
             Kernel::ResultInvalidAddress);
    R_UNLESS(Common::IsAligned(size, Kernel::PageSize),
             Kernel::ResultInvalidSize);
    R_UNLESS(m_memory.size() < MaxOwnedGpuMemoryHandles,
             Kernel::ResultOutOfResource);

    auto& page_table = m_process->GetPageTable();
    R_UNLESS(page_table.Contains(begin, size),
             Kernel::ResultInvalidCurrentMemory);
    for (u64 address = begin; address <= end;) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page{};
        R_TRY(page_table.QueryInfo(&info, &page, address));
        // Encore's maintained kernel has no upstream-only GpuAccessible
        // attribute. Require precisely the aligned-device-map and referenced
        // page-state capabilities consumed by LockForMapDeviceAddressSpace,
        // and reject IPC/explicitly locked pages before taking a reference.
        const auto required = Kernel::KMemoryState::FlagCanAlignedDeviceMap |
                              Kernel::KMemoryState::FlagReferenceCounted;
        R_UNLESS((info.m_state & required) == required &&
                 False(info.m_attribute & (Kernel::KMemoryAttribute::IpcLocked |
                                           Kernel::KMemoryAttribute::Locked)),
                 Kernel::ResultInvalidCurrentMemory);
        // Check progress and avoid overflowing a corrupt memory-info range.
        R_UNLESS(info.m_size > 0 &&
                 info.m_address <= std::numeric_limits<u64>::max() - info.m_size,
                 Kernel::ResultInvalidCurrentMemory);
        const u64 next = info.m_address + info.m_size;
        R_UNLESS(next > address, Kernel::ResultInvalidCurrentMemory);
        address = next;
    }

    if (!m_nv_session)
        m_nv_session = m_module->GetContainer().OpenSession(m_process).id;
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    std::shared_ptr<Nvidia::NvCore::NvMap::Handle> handle;
    R_UNLESS(nvmap.CreateHandle(size, handle) == Nvidia::NvResult::Success,
             Kernel::ResultOutOfResource);

    bool is_io{};
    const Result locked = page_table.LockForMapDeviceAddressSpace(
        &is_io, begin, size, Kernel::KMemoryPermission::None, true, true);
    if (locked.IsError()) {
        nvmap.FreeHandle(handle->id, false);
        return locked;
    }

    const auto allocated = handle->Alloc({}, Kernel::PageSize, 0, begin,
                                         Nvidia::NvCore::SessionId{*m_nv_session});
    if (allocated != Nvidia::NvResult::Success) {
        const auto undone = page_table.UnlockForDeviceAddressSpace(begin, size);
        if (undone.IsError())
            LOG_ERROR(Service_AM, "NS2 GPU rollback unlock failed: {:#x}", undone.raw);
        nvmap.FreeHandle(handle->id, false);
        return Kernel::ResultOutOfResource;
    }
    id = handle->id;
    m_memory.push_back(id);
    return ResultSuccess;
}

Result IGpuWorker::ReleaseMemory(u32 id, u32 notification) {
    // Caller holds BOTH locks, so event ownership and GPU IDs stay stable.
    R_UNLESS(notification < m_shared->events.size() &&
             m_shared->events[notification], Kernel::ResultInvalidCombination);
    const auto it = std::find(m_memory.begin(), m_memory.end(), id);
    R_UNLESS(it != m_memory.end(), Kernel::ResultNotFound);
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    const auto handle = nvmap.GetHandle(id);
    R_UNLESS(handle, Kernel::ResultInvalidState);
    {
        const std::scoped_lock lock{handle->mutex};
        // A pinned/duplicated resource is not safe to release synchronously.
        // Delayed release + completion polling will be added with real native
        // 4K mappings; never signal completion for a still-retained handle.
        R_UNLESS(handle->dupes == 1 && handle->internal_dupes == 0 &&
                 handle->pins == 0, Kernel::ResultBusy);
    }

    const auto info = nvmap.FreeHandle(id, false);
    R_UNLESS(info && info->can_unlock, Kernel::ResultInvalidState);
    m_memory.erase(it);
    R_TRY(m_process->GetPageTable().UnlockForDeviceAddressSpace(
        info->address, info->size));
    m_shared->events[notification]->Signal(kernel);
    return ResultSuccess;
}

void IGpuWorker::CreateGpuAccessibleMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 8> command{
        packet[0], packet[1], packet[2], packet[3],
        packet[4], packet[5], packet[6], packet[7]};
    u32 id{};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto req = DecodeGpuMemoryRegistration(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = RegisterMemory(req->begin, req->end, req->flags, req->device, id);
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGpuWorker::DestroyGpuAccessibleMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 4> command{packet[0], packet[1], packet[2], packet[3]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto release = DecodeGpuMemoryRelease(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = ReleaseMemory(release->id, release->notification);
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
} // namespace Service::AM
