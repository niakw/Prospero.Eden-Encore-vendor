// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Successful NS2 supervisor commands have real kernel IPC backing, but
// NO native NVDRV address space, event, worker, queue or PS5 rendering path.
#include <algorithm>
#include <array>
#include <memory>
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/sm/sm.h"
#include <span>
#include "core/core.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/gpu_session.h"
#include "core/hle/service/am/service/graphics_supervisor.h"
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
    };
    RegisterHandlersTipc(functions);
}
IGraphicsSupervisor::~IGraphicsSupervisor() { m_process->Close(kernel); }
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
} // namespace Service::AM
