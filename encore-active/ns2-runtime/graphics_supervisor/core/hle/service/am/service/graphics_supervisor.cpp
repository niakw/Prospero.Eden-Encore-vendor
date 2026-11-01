// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Successful NS2 supervisor commands have real kernel IPC backing, but
// NO native NVDRV address space, event, worker, queue or PS5 rendering path.
#include <algorithm>
#include <array>
#include "common/logging.h"
#include <memory>
#include <limits>
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/service/sm/sm.h"
#include <span>
#include "core/core.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/gpu_session.h"
#include "core/hle/service/am/service/graphics_supervisor.h"
#include "core/hle/service/am/service/native_image_layout.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"

namespace Service::AM {
namespace GB = GraphicsSupervisorBootstrap;
static Result TranslateBootstrap(GB::Status status) {
    switch (status) {
    case GB::Status::Success:         return ResultSuccess;
    case GB::Status::InvalidPacket:   return Kernel::ResultInvalidCombination;
    case GB::Status::InvalidHandle:   return Kernel::ResultInvalidHandle;
    case GB::Status::InvalidState:    return Kernel::ResultInvalidState;
    case GB::Status::InvalidArgument: return Kernel::ResultInvalidArgument;
    case GB::Status::Unsupported:     return Kernel::ResultNotImplemented;
    }
    return Kernel::ResultNotImplemented;
}
static Result CreateMetadataSession(Core::System& system, HLERequestContext& ctx,
                                    std::shared_ptr<IGpuSession> handler,
                                    Kernel::KClientSession** out_client) {
    auto& kernel = system.Kernel();
    Kernel::KScopedResourceReservation reservation{
        kernel, Kernel::GetCurrentProcessPointer(kernel), Kernel::LimitableResource::SessionCountMax};
    R_UNLESS(reservation.Succeeded(), Kernel::ResultLimitReached);
    auto* session = Kernel::KSession::Create(kernel);
    R_UNLESS(session != nullptr, Kernel::ResultOutOfResource);
    session->Initialize(kernel, nullptr, 0);
    reservation.Commit();
    Kernel::KSession::Register(kernel, session);
    auto& server = ctx.GetManager()->GetServerManager();
    auto manager = std::make_shared<SessionRequestManager>(kernel, server);
    manager->SetSessionHandler(std::move(handler));
    R_TRY(server.RegisterSession(&session->GetServerSession(), std::move(manager)));
    *out_client = &session->GetClientSession();
    R_SUCCEED();
}
IGraphicsSupervisor::IGraphicsSupervisor(Core::System& system_, Kernel::KProcess* process,
                                         u64 aruid)
    : ServiceFramework{system_, "IGraphicsSupervisor"}, m_process{process},
      m_bootstrap{aruid} {
    m_process->Open(kernel);
    static const FunctionInfo functions[] = {
        {4, &IGraphicsSupervisor::Initialize, "Initialize"},
        {11, &IGraphicsSupervisor::BindProcess, "BindProcess"},
        {101, &IGraphicsSupervisor::CompleteBootstrap, "CompleteBootstrap"},
        {401, &IGraphicsSupervisor::InitializeGpuContext, "InitializeGpuContext"},
        {402, &IGraphicsSupervisor::InitializeGpuContext, "ActivateGpuContext"},
        {434, &IGraphicsSupervisor::RegisterGpuAddressSpace, "RegisterGpuAddressSpace"},
        {497, &IGraphicsSupervisor::OpenGpuSession, "OpenGpuSession"},
        {111, &IGraphicsSupervisor::CreateNativeMemory, "CreateNativeMemory"},
        {112, &IGraphicsSupervisor::DestroyNativeMemory, "DestroyNativeMemory"},
        {114, &IGraphicsSupervisor::BindNativeMemory, "BindNativeMemory"},
        {429, &IGraphicsSupervisor::CreateNativeImage, "CreateNativeImage"},
        {430, &IGraphicsSupervisor::DestroyNativeImage, "DestroyNativeImage"},
        {435, &IGraphicsSupervisor::CalculateNativeImageSize, "CalculateNativeImageSize"},
        {1510, &IGraphicsSupervisor::CalculateNativeImageRequirements, "CalculateRequirements"},
    };
    RegisterHandlersTipc(functions);
}
IGraphicsSupervisor::~IGraphicsSupervisor() {
    // Release the real NVMap backing and page-table locks before the native
    // NVDRV session and the application KProcess lose their final owner.
    const std::scoped_lock lock{m_native_memory_mutex};
    if (m_native_module) {
        auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
        // Retire internal image duplicates before their underlying NVMap
        // allocations; otherwise the backing remains busy at shutdown.
        for (const auto& [image_id, image] : m_native_images.Records())
            nvmap.FreeHandle(image.backing, true);
        for (const u32 id : m_native_memory.OwnedIds()) {
            auto handle = nvmap.GetHandle(id);
            if (!handle) continue;
            bool busy{};
            {
                const std::scoped_lock handle_lock{handle->mutex};
                busy = handle->dupes != 1 || handle->internal_dupes != 0 || handle->pins != 0;
            }
            if (busy) continue; // Borrowed resources must never be unlocked.
            handle.reset();
            const auto info = nvmap.FreeHandle(id, false);
            if (info && info->can_unlock && info->address != 0) {
                const auto status = m_process->GetPageTable().UnlockForDeviceAddressSpace(
                    info->address, info->size);
                if (status.IsError())
                    LOG_ERROR(Service_AM, "NS2 native supervisor teardown unlock failed: {:#x}",
                              status.raw);
            }
        }
        if (m_native_memory_session)
            m_native_module->GetContainer().CloseSession(
                Nvidia::NvCore::SessionId{*m_native_memory_session});
    }
    m_process->Close(kernel);
}
Result IGraphicsSupervisor::HandleSyncRequest(Kernel::KServerSession& session,
                                               HLERequestContext& ctx) {
    if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand()))
        return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
    const auto guard = LockService();
    InvokeRequestTipc(ctx);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
    return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
}
void IGraphicsSupervisor::Initialize(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    const auto request = GraphicsSupervisorProtocol::DecodeBootstrap(packet);
    bool matches = false;
    if (request) {
        const auto process = ctx.GetObjectFromHandle<Kernel::KProcess>(request->process_handle);
        matches = !process.IsNull() && process.GetPointerUnsafe() == m_process;
    }
    const Result result = TranslateBootstrap(m_bootstrap.Initialize(packet, matches));
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
void IGraphicsSupervisor::BindProcess(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    bool matches = false;
    if (packet[0] == 0x1b && packet[1] == 0x80000000 && packet[2] == 2) {
        const auto process = ctx.GetObjectFromHandle<Kernel::KProcess>(ctx.GetCopyHandle(0));
        matches = !process.IsNull() && process.GetPointerUnsafe() == m_process;
    }
    const Result result = TranslateBootstrap(m_bootstrap.BindProcess(packet, matches));
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
void IGraphicsSupervisor::CompleteBootstrap(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    const Result result = TranslateBootstrap(m_bootstrap.Complete(packet));
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
void IGraphicsSupervisor::InitializeGpuContext(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    const Result result = TranslateBootstrap(
        m_bootstrap.InitializeGpuContext(packet, ctx.GetCommand()));
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::RegisterGpuAddressSpace(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 3> command{packet[0], packet[1], packet[2]};
    const auto id = GpuSessionProtocol::DecodeRegisterAddressSpace(command);
    Result result = Kernel::ResultInvalidCombination;
    if (id) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            result = Kernel::ResultNotFound;
            std::erase_if(m_gpu_sessions, [](const auto& weak) { return weak.expired(); });
            for (const auto& weak : m_gpu_sessions) {
                if (auto handler = weak.lock()) {
                    result = handler->RegisterAddressSpace(*id);
                    if (result.IsSuccess()) break;
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    // Only actual NVDRV service modules can back address-space FD creation.
    const auto source = system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
    const auto module = source ? source->GetModule() : nullptr;
    Result result = TranslateBootstrap(m_bootstrap.CanOpenGpuSession(packet, module != nullptr));
    Kernel::KClientSession* client{};
    if (result.IsSuccess()) {
        auto handler = std::make_shared<IGpuSession>(system, module, m_process);
        result = CreateMetadataSession(system, ctx, handler, &client);
        if (result.IsSuccess()) {
            std::erase_if(m_gpu_sessions, [](const auto& weak) { return weak.expired(); });
            m_gpu_sessions.emplace_back(handler);
        }
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}

static Result NativeMemoryError(GraphicsSupervisorProtocol::NativeMemoryStatus status) {
    using S = GraphicsSupervisorProtocol::NativeMemoryStatus;
    switch (status) {
    case S::Success:       return ResultSuccess;
    case S::InvalidSize:   return Kernel::ResultInvalidSize;
    case S::InvalidAddress:return Kernel::ResultInvalidAddress;
    case S::InvalidHandle: return Kernel::ResultInvalidHandle;
    case S::AlreadyBound:  return Kernel::ResultInvalidState;
    case S::OutOfResource: return Kernel::ResultOutOfResource;
    }
    return Kernel::ResultNotImplemented;
}
static u64 NativeU64(const u32* packet, size_t word) {
    return u64{packet[word]} | (u64{packet[word + 1]} << 32);
}
void IGraphicsSupervisor::CreateNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 id{};
    if (packet[0] == 0x7f && packet[1] == 3 && packet[2] <= 3) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            const u64 size = NativeU64(packet, 3);
            result = NativeMemoryError(
                size == 0 || size > GraphicsSupervisorProtocol::NativeMemoryLedger::MaxGuestMemory ||
                (size & (Kernel::PageSize - 1))
                    ? GraphicsSupervisorProtocol::NativeMemoryStatus::InvalidSize
                    : GraphicsSupervisorProtocol::NativeMemoryStatus::Success);
            if (result.IsSuccess()) {
                const std::scoped_lock lock{m_native_memory_mutex};
                result = Kernel::ResultOutOfResource;
                if (m_native_memory.Count() <
                    GraphicsSupervisorProtocol::NativeMemoryLedger::MaxEntries) {
                    if (!m_native_module) {
                        const auto service =
                            system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
                        m_native_module = service ? service->GetModule() : nullptr;
                    }
                    if (m_native_module) {
                        auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                        std::shared_ptr<Nvidia::NvCore::NvMap::Handle> handle;
                        if (nvmap.CreateHandle(size, handle) == Nvidia::NvResult::Success) {
                            id = handle->id;
                            const auto status = m_native_memory.Adopt(id, size);
                            if (status == GraphicsSupervisorProtocol::NativeMemoryStatus::Success) {
                                result = ResultSuccess;
                            } else {
                                handle.reset();
                                nvmap.FreeHandle(id, false);
                                id = 0;
                                result = NativeMemoryError(status);
                            }
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGraphicsSupervisor::DestroyNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x80 && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        const auto record = m_native_memory.Get(packet[2]);
        result = Kernel::ResultNotFound;
        if (record && m_native_module) {
            auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
            auto handle = nvmap.GetHandle(packet[2]);
            result = Kernel::ResultInvalidState;
            if (handle) {
                bool busy{};
                {
                    const std::scoped_lock handle_lock{handle->mutex};
                    busy = handle->dupes != 1 || handle->internal_dupes != 0 ||
                           handle->pins != 0;
                }
                result = Kernel::ResultBusy;
                if (!busy) {
                    handle.reset(); // FreeHandle's weak lifetime determines can_unlock.
                    const auto freed = nvmap.FreeHandle(packet[2], false);
                    result = Kernel::ResultInvalidState;
                    if (freed && freed->can_unlock) {
                        const auto removed = m_native_memory.Destroy(packet[2]);
                        result = NativeMemoryError(removed);
                        if (result.IsSuccess() && record->bound) {
                            result = m_process->GetPageTable().UnlockForDeviceAddressSpace(
                                freed->address, freed->size);
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::BindNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x82 && packet[1] == 9) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            const u32 id = packet[2];
            const u64 address = NativeU64(packet, 3);
            const u64 size = NativeU64(packet, 5);
            const u64 flags = NativeU64(packet, 7);
            const u64 offset = NativeU64(packet, 9);
            result = Kernel::ResultNotImplemented;
            if ((flags == 0 || flags == 2) && offset == 0) {
                const std::scoped_lock lock{m_native_memory_mutex};
                const auto record = m_native_memory.Get(id);
                result = NativeMemoryError(m_native_memory.ValidateBind(id, address, size));
                if (result.IsSuccess()) {
                    result = Kernel::ResultInvalidSize;
                    if (record && record->size == size && address != 0 &&
                        m_native_module) {
                        auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                        const auto handle = nvmap.GetHandle(id);
                        result = Kernel::ResultInvalidState;
                        if (handle && !handle->allocated) {
                            auto& table = m_process->GetPageTable();
                            result = Kernel::ResultInvalidCurrentMemory;
                            bool valid = table.Contains(address, size);
                            const u64 end = address + size; // Validated by ledger.
                            for (u64 cursor = address; valid && cursor < end;) {
                                Kernel::KMemoryInfo info{};
                                Kernel::Svc::PageInfo page{};
                                const Result status = table.QueryInfo(&info, &page, cursor);
                                if (status.IsError() || info.m_size == 0 ||
                                    info.m_address > cursor ||
                                    info.m_size > std::numeric_limits<u64>::max() - info.m_address ||
                                    (info.m_state & Kernel::KMemoryState::FlagMapped) !=
                                        Kernel::KMemoryState::FlagMapped ||
                                    (flags == 2 &&
                                     !True(info.m_attribute & Kernel::KMemoryAttribute::GpuAccessible))) {
                                    valid = false;
                                    break;
                                }
                                const u64 next = info.m_address + info.m_size;
                                if (next <= cursor) { valid = false; break; }
                                cursor = next;
                            }
                            if (valid) {
                                bool is_io{};
                                result = table.LockForMapDeviceAddressSpace(
                                    &is_io, address, size, Kernel::KMemoryPermission::None,
                                    true, true);
                                if (result.IsSuccess()) {
                                    if (is_io) {
                                        table.UnlockForDeviceAddressSpace(address, size);
                                        result = Kernel::ResultNotImplemented;
                                    } else {
                                        if (!m_native_memory_session)
                                            m_native_memory_session =
                                                m_native_module->GetContainer().OpenSession(
                                                    m_process).id;
                                        const auto alloc = handle->Alloc(
                                            {}, Kernel::PageSize, 0, address,
                                            Nvidia::NvCore::SessionId{*m_native_memory_session});
                                        if (alloc == Nvidia::NvResult::Success) {
                                            result = NativeMemoryError(
                                                m_native_memory.Bind(id, address, size));
                                        } else {
                                            table.UnlockForDeviceAddressSpace(address, size);
                                            result = Kernel::ResultOutOfResource;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

// Image IDs are real logical resources: each holds one NVMap internal
// duplicate on a previously bound guest allocation. Creating an ID does
// not claim that a host Vulkan image has been instantiated.
void IGraphicsSupervisor::CreateNativeImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 id{};
    if (packet[0] == 0x1bd && packet[1] == 18 && packet[3] == 1) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            const std::span<const u32> preferences{packet + 2, 16};
            const auto requirements = CalculateNativeImageRequirements(preferences, packet[18]);
            result = Kernel::ResultNotImplemented;
            if (requirements) {
                const std::scoped_lock lock{m_native_memory_mutex};
                result = Kernel::ResultNotFound;
                const u32 backing = packet[19];
                const auto owned = m_native_memory.Get(backing);
                if (owned && owned->bound && m_native_module) {
                    auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                    const auto handle = nvmap.GetHandle(backing);
                    result = Kernel::ResultInvalidSize;
                    if (handle && handle->allocated && handle->size >= requirements->size) {
                        result = Kernel::ResultOutOfResource;
                        if (m_native_images.Count() < NativeImageLedger::MaxEntries &&
                            handle->Duplicate(true) == Nvidia::NvResult::Success) {
                            if (const auto created =
                                    m_native_images.Create(backing, handle->size,
                                                           *requirements, preferences)) {
                                id = *created;
                                result = ResultSuccess;
                            } else {
                                nvmap.FreeHandle(backing, true);
                            }
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGraphicsSupervisor::DestroyNativeImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1be && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        const auto image = m_native_images.Get(packet[2]);
        if (image && m_native_module) {
            const auto freed = m_native_module->GetContainer().GetNvMapFile().FreeHandle(
                image->backing, true);
            result = Kernel::ResultInvalidState;
            if (freed && m_native_images.Destroy(packet[2]))
                result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

static std::optional<NativeImageRequirements> ParseNativeImageRequest(HLERequestContext& ctx) {
    // Native image preferences are exactly sixteen u32 words followed by
    // a single image count. The maintained layout helper verifies known
    // image types, width, height, pitch, block alignment and multiplication.
    // Reject malformed TIPC packets rather than returning fabricated sizes.
    const u32* const data = ctx.CommandBuffer();
    if (data[1] != 17) return {};
    return CalculateNativeImageRequirements(std::span<const u32>{data + 2, 16}, data[18]);
}
void IGraphicsSupervisor::CalculateNativeImageSize(HLERequestContext& ctx) {
    const auto request = ParseNativeImageRequest(ctx);
    const Result result = request ? ResultSuccess : Kernel::ResultInvalidCombination;
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push(request ? static_cast<u32>(request->size) : 0u);
    rb.Push(request ? static_cast<u32>(request->size >> 32) : 0u);
}
void IGraphicsSupervisor::CalculateNativeImageRequirements(HLERequestContext& ctx) {
    const auto request = ParseNativeImageRequest(ctx);
    const Result result = request ? ResultSuccess : Kernel::ResultInvalidCombination;
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result.raw);
    rb.Push(request ? static_cast<u32>(request->size) : 0u);
    rb.Push(request ? static_cast<u32>(request->size >> 32) : 0u);
    rb.Push(request ? static_cast<u32>(request->alignment) : 0u);
    rb.Push(request ? static_cast<u32>(request->alignment >> 32) : 0u);
}
} // namespace Service::AM
