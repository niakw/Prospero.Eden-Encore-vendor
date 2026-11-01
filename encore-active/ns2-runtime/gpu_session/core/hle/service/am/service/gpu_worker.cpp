// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Native GPU registration and selected dense 214 backing own real guest
// page-table / NVMap resources. Ampere command submission is separate.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "common/alignment.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_page_group.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_system_resource.h"
#include "video_core/gpu.h"
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
static void TraceNs2WorkerResult(unsigned command, u32 result) {
#ifdef PS5_NATIVE
    // Failures are actionable only with their actual guest-visible result.
    // Bound the trace even if the guest retries an unsupported operation.
    static std::atomic<unsigned> responses{0};
    const auto n = responses.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 64 || (n & (n - 1)) == 0)
        std::fprintf(stderr,
                     "EDEN_NS2_GPU_WORKER_RESULT cmd=%u result=%08x count=%u\n",
                     command, static_cast<unsigned>(result), n);
#else
    (void)command;
    (void)result;
#endif
}

// 214 may be a sparse reservation without any physical pages. Expose the
// precise request shape once per bounded sample; a successful reservation
// MUST NOT be mistaken for resident render-target memory or a guest frame.
static void TraceNs2GpuRegion214(const u32* packet,
                                  const std::optional<GpuSparseRegionRequest>& request,
                                  u32 result, bool has_physical_backing) {
#ifdef PS5_NATIVE
    static std::atomic<unsigned> seen{0};
    const auto n = seen.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 24 || (n & (n - 1)) == 0) {
        std::fprintf(stderr,
                     "EDEN_NS2_GPU_214 decoded=%u hdr=%08x/%08x/%08x "
                     "field7=%08x field8=%08x field9=%08x field10=%08x "
                     "as=%u device=%u mode=%u page_shift=%u "
                     "begin=%llx end=%llx result=%08x backing=%s count=%u\n",
                     request ? 1U : 0U,
                     packet[0], packet[1], packet[2], packet[7], packet[8],
                     packet[9], packet[10],
                     request ? request->address_space : 0U,
                     request ? unsigned(request->device) : 0U,
                     request ? unsigned(request->allocation_mode) : 0U,
                     request ? unsigned(request->page_shift) : 0U,
                     static_cast<unsigned long long>(request ? request->begin : 0),
                     static_cast<unsigned long long>(request ? request->end : 0),
                     result, has_physical_backing ? "dense_nvmap" : "unbound", n);
    }
#else
    (void)packet;
    (void)request;
    (void)result;
    (void)has_physical_backing;
#endif
}
static void TraceNs2GpuSparse216(u64 requested, u64 processed, u64 physical_entries, u32 result) {
#ifdef PS5_NATIVE
    static std::atomic<unsigned> seen{0};
    const auto n = seen.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 24 || (n & (n - 1)) == 0) {
        std::fprintf(stderr,
                     "EDEN_NS2_GPU_216 requested=%llu processed=%llu physical_entries=%llu result=%08x count=%u\n",
                     static_cast<unsigned long long>(requested),
                     static_cast<unsigned long long>(processed),
                     static_cast<unsigned long long>(physical_entries), result, n);
    }
#else
    (void)requested;
    (void)processed;
    (void)physical_entries;
    (void)result;
#endif
}
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
    // Drain guest GPU work before removing any memory that can be consumed
    // by the native engine; do not release backing while it is still mapped.
    if (!m_dense_regions.empty()) system.GPU().WaitForIdle();
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
    // The sparse reservations are gone: it is now safe to drop the NVMap pins,
    // unmap CPU aliases and close the page-group physical allocations.
    while (!m_dense_regions.empty()) {
        const auto key = m_dense_regions.begin()->first;
        if (!RetireGpuOnlyRegion(key.first, key.second)) {
            LOG_ERROR(Service_AM, "NS2 dense backing retire failed as={} VA={:#x}",
                      key.first, key.second);
            // Never free live physical memory after a failed guest unmap.
            // A safe process-level quarantine is preferable to a GPU UAF.
            auto leaked = std::move(m_dense_regions.begin()->second.pages);
            (void)leaked.release();
            m_dense_regions.erase(m_dense_regions.begin());
        }
    }
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

Result IGpuWorker::BackGpuOnlyRegion(u32 address_space, u64 address, u64 size) {
    // Called only after the exact native sparse VA has been reserved.
    // Keep a bounded physical budget and leave room for the guest/application.
    auto& memory = kernel.MemoryManager();
    using Pool = Kernel::KMemoryManager::Pool;
    using Direction = Kernel::KMemoryManager::Direction;
    if (m_dense_regions.contains({address_space, address}) ||
        !CanBackNativeDenseGpuOnly(0, size, m_dense_bytes,
                                    memory.GetFreeSize(Pool::SystemNonSecure)))
        return Kernel::ResultOutOfResource;

    auto pages = std::make_unique<Kernel::KPageGroup>(
        kernel, std::addressof(kernel.GetSystemSystemResource().GetBlockInfoManager()));
    const auto allocated = memory.AllocateAndOpen(
        pages.get(), size / Kernel::PageSize,
        Kernel::KMemoryManager::EncodeOption(Pool::SystemNonSecure, Direction::FromBack));
    if (allocated.IsError()) return allocated;

    auto& table = m_process->GetPageTable();
    Kernel::KProcessAddress alias{};
    // KProcessPageTable exposes only the fixed-address four-argument
    // MapPageGroup overload, unlike KPageTableBase. Search bounded 2 MiB
    // aligned slots instead of guessing an address or spinning indefinitely.
    constexpr u64 AliasAlignment = 2ULL << 20;
    constexpr u64 MaxAliasAttempts = 4096;
    const u64 start = GetInteger(table.GetAliasCodeRegionStart());
    const u64 region_size = table.GetAliasCodeRegionSize();
    const u64 slots = region_size / AliasAlignment;
    bool mapped = false;
    if (slots && size <= region_size) {
        const u64 attempts = std::min(slots, MaxAliasAttempts);
        for (u64 step = 0; step < attempts; ++step) {
            const u64 slot = (m_alias_slot_hint + step) % slots;
            const u64 offset = slot * AliasAlignment;
            if (offset > region_size || size > region_size - offset) continue;
            const Kernel::KProcessAddress candidate{start + offset};
            if (table.MapPageGroup(candidate, *pages, Kernel::KMemoryState::IoMemory,
                                   Kernel::KMemoryPermission::UserReadWrite).IsSuccess()) {
                alias = candidate;
                m_alias_slot_hint = (slot + (size + AliasAlignment - 1) /
                                    AliasAlignment) % slots;
                mapped = true;
                break;
            }
        }
    }
    if (!mapped) {
        pages->Close(kernel);
        return Kernel::ResultOutOfResource;
    }

    if (!m_nv_session)
        m_nv_session = m_module->GetContainer().OpenSession(m_process).id;
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    std::shared_ptr<Nvidia::NvCore::NvMap::Handle> handle;
    auto status = nvmap.CreateHandle(size, handle);
    if (status == Nvidia::NvResult::Success) {
        status = handle->Alloc({}, Kernel::PageSize, 0, GetInteger(alias),
                               Nvidia::NvCore::SessionId{*m_nv_session});
    }
    bool committed = false;
    if (status == Nvidia::NvResult::Success) {
        Nvidia::Devices::NativeSparseRemapEntry entry{};
        entry.flags = 0x1000;
        entry.handle = handle->id;
        entry.destination_pages = address / Kernel::PageSize;
        entry.pages = size / Kernel::PageSize;
        const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(
            static_cast<Nvidia::DeviceFD>(address_space));
        committed = as && as->RemapNativeSparse(std::span{&entry, size_t{1}}) ==
                               Nvidia::NvResult::Success;
    }
    if (!committed) {
        if (handle) {
            const auto id = handle->id;
            handle.reset();
            nvmap.FreeHandle(id, false);
        }
        const auto result = table.UnmapPageGroup(alias, *pages, Kernel::KMemoryState::IoMemory);
        if (result.IsError()) {
            LOG_ERROR(Service_AM, "NS2 GPU-only rollback CPU alias failed: {:#x}", result.raw);
            // Never free pages still mapped in the process.
            (void)pages.release();
        } else {
            pages->Close(kernel);
        }
        return Kernel::ResultInvalidState;
    }
    // Do not retain the preflight shared_ptr: FreeHandle needs to be able
    // to release the NVMap object after GMMU unpins on 215.
    const u32 handle_id = handle->id;
    handle.reset();
    m_dense_regions.emplace(std::make_pair(address_space, address),
        DenseRegion{address, size, GetInteger(alias), handle_id, std::move(pages)});
    m_dense_bytes += size;
    return ResultSuccess;
}

bool IGpuWorker::RetireGpuOnlyRegion(u32 address_space, u64 address) {
    const auto it = m_dense_regions.find({address_space, address});
    if (it == m_dense_regions.end()) return true;
    auto& region = it->second;
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    const auto info = nvmap.FreeHandle(region.nvmap_handle, false);
    if (!info || !info->can_unlock) return false;
    const auto result = m_process->GetPageTable().UnmapPageGroup(
        Kernel::KProcessAddress{region.alias}, *region.pages, Kernel::KMemoryState::IoMemory);
    if (result.IsError()) return false;
    region.pages->Close(kernel);
    m_dense_bytes -= region.size;
    m_dense_regions.erase(it);
    return true;
}

Result IGpuWorker::HandleSyncRequest(Kernel::KServerSession& session,
                                     HLERequestContext& ctx) {
    #ifdef PS5_NATIVE
    if (ctx.IsTipc()) {
        static std::atomic<unsigned> requests{0};
        const auto n = requests.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 48 || (n & (n - 1)) == 0)
            std::fprintf(stderr, "EDEN_NS2_IPC owner=gpu_worker cmd=%u known=%u count=%u\n",
                         static_cast<unsigned>(ctx.GetCommand()),
                         static_cast<unsigned>(handlers_tipc.contains(ctx.GetCommand())), n);
    }
    #endif
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
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
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
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
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
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push(unmapped);
}

void IGpuWorker::CreateSparseRegion(HLERequestContext& ctx) {
    const auto request = DecodeGpuSparseRegion(
        std::span<const u32>{ctx.CommandBuffer(), 12});
    Result result = Kernel::ResultInvalidCombination;
    bool dense_backed = false;
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
                            // Mode 0 requests physical GPU-only render storage.
                            // Mode 1 remains an explicitly unbacked sparse VA
                            // until the guest submits physical 216 mappings.
                            if (request->allocation_mode == 0) {
                                result = BackGpuOnlyRegion(
                                    request->address_space, request->begin, size);
                                dense_backed = result.IsSuccess();
                                if (!dense_backed) {
                                    as->ReleaseNativeSparse4K(request->begin, size);
                                    m_mappings.RecordReleasedSparse(
                                        request->address_space, request->begin, size);
                                }
                            } else {
                                result = ResultSuccess;
                            }
                            if (result.IsSuccess())
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
    TraceNs2GpuRegion214(ctx.CommandBuffer(), request, result.raw, dense_backed);
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
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
                if (as) {
                    const bool physical = m_dense_regions.contains(
                        {request->address_space, request->address});
                    if (physical) system.GPU().WaitForIdle();
                    if (as->ReleaseNativeSparse4K(request->address, request->size) ==
                            Nvidia::NvResult::Success) {
                        const bool retired = !physical || RetireGpuOnlyRegion(
                            request->address_space, request->address);
                        if (retired) {
                            m_mappings.RecordReleasedSparse(
                                request->address_space, request->address, request->size);
                            result = ResultSuccess;
                            m_shared->events[request->notification]->Signal(kernel);
                        }
                    }
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::ExecuteSparseMapRequests(HLERequestContext& ctx) {
    std::array<GW::U32, 15> command{};
    std::copy_n(ctx.CommandBuffer(), command.size(), command.begin());
    u64 processed{};
    u64 physical_entries{};
    Result result = Kernel::ResultInvalidCombination;
    const auto request = GW::DecodeSparseBatch(command);
    if (request) {
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
                            physical_entries = std::count_if(staged.begin(), staged.end(),
                                [](const auto& entry) { return entry.handle != 0; });
                            ctx.WriteBuffer(selected);
                            result = ResultSuccess;
                            m_shared->events[request->notification]->Signal(kernel);
                        }
                    }
                }
            }
        }
    }
    TraceNs2GpuSparse216(request ? request->count : 0, processed, physical_entries, result.raw);
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
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
        // NS2 svc 0x80 records the GPU-accessible bit in the actual guest
        // KMemoryBlockManager. A native worker must not pin ordinary guest
        // pages that the game never registered for GPU use. The physical
        // mapping capabilities and lock guards remain mandatory as well.
        const auto required = Kernel::KMemoryState::FlagCanAlignedDeviceMap |
                              Kernel::KMemoryState::FlagReferenceCounted;
        R_UNLESS((info.m_state & required) == required &&
                 True(info.m_attribute & Kernel::KMemoryAttribute::GpuAccessible) &&
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
    auto handle = nvmap.GetHandle(id);
    R_UNLESS(handle, Kernel::ResultInvalidState);
    {
        const std::scoped_lock lock{handle->mutex};
        // A pinned/duplicated resource is not safe to release synchronously.
        // Do not signal completion while external GPU references remain.
        R_UNLESS(handle->dupes == 1 && handle->internal_dupes == 0 &&
                 handle->pins == 0, Kernel::ResultBusy);
    }
    // NvMap::FreeHandle reports can_unlock=false while ANY external
    // shared_ptr keeps the handle alive. Release our preflight reference
    // first, otherwise every valid destroy falsely fails and leaks the
    // guest page-table device lock.
    handle.reset();
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
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
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
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
} // namespace Service::AM
