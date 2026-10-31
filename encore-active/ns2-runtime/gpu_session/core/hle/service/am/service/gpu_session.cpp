// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Research-only real HLE registration, not linked in the Encore release.
// Real bounded NVDRV address-space and kernel event owners. Unsupported
// worker, queue submission and native 4K mapping return explicit errors.
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include "common/swap.h"
#include <mutex>
#include "core/core.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/am/service/gpu_session.h"
#include "core/hle/service/am/service/gpu_worker.h"
#include "core/hle/service/am/service/gpu_worker_shared.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::AM {
namespace GP = GpuSessionProtocol;

IGpuSession::IGpuSession(Core::System& system_,
                         std::shared_ptr<Nvidia::Module> module,
                         Kernel::KProcess* process)
    : ServiceFramework{system_, "IGpuSession"}, m_module{std::move(module)},
      m_shared{std::make_shared<GpuWorkerSharedState>(system_, m_module, process)},
      m_process{process} {
    m_process->Open(kernel);
    static const FunctionInfo functions[] = {
        {11, &IGpuSession::GetNotificationRange, "GetNotificationRange"},
        {12, &IGpuSession::GetNotificationEvent, "GetNotificationEvent"},
        {21, &IGpuSession::RequestWorker, "RequestWorker"},
        {22, &IGpuSession::AcquireWorker, "AcquireWorker"},
        {33, &IGpuSession::CreateAddressSpace, "CreateAddressSpace"},
        {41, &IGpuSession::CreateGraphicsDevice, "CreateGraphicsDevice"},
        {42, &IGpuSession::DestroyGraphicsDevice, "DestroyGraphicsDevice"},
        {202, &IGpuSession::GetAddressSpacePartition, "GetGsvPartition"},
        {203, &IGpuSession::GetAddressSpacePartition, "GetUserPartition"},
        // Worker transport 21/22 is real; native GPU worker operations
        // 101/102 and 211-216 remain explicitly unsupported.
    };
    RegisterHandlersTipc(functions);
}
IGpuSession::~IGpuSession() {
    if (m_worker) m_worker->Close(kernel);
    // Drop our shared reference while the process is still alive. Any guest
    // worker client continues holding the process and the NVDRV FD owner.
    m_shared.reset();
    m_process->Close(kernel);
}

Result IGpuSession::HandleSyncRequest(Kernel::KServerSession& session,
                                     HLERequestContext& ctx) {
    if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand())) {
        return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
    }
    const auto guard=LockService();
    InvokeRequestTipc(ctx);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
    return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
}

static Result Translate(GP::Status status) {
    switch (status) {
    case GP::Status::Success:       return ResultSuccess;
    case GP::Status::InvalidPacket: return Kernel::ResultInvalidCombination;
    case GP::Status::Unsupported:   return Kernel::ResultNotImplemented;
    case GP::Status::NotFound:      return Kernel::ResultNotFound;
    case GP::Status::OutOfResource: return Kernel::ResultOutOfResource;
    case GP::Status::InvalidState:  return Kernel::ResultInvalidState;
    }
    return Kernel::ResultNotImplemented;
}


// Actual worker KSession, not a dummy IPC object. The client reference is
// retained until IGpuSession destruction and acquired separately by guests.
static Result CreateWorkerKernelSession(Core::System& system, HLERequestContext& ctx,
                                        std::shared_ptr<IGpuWorker> handler,
                                        Kernel::KClientSession** out_client) {
    auto& kernel = system.Kernel();
    Kernel::KScopedResourceReservation reservation{
        kernel, Kernel::GetCurrentProcessPointer(kernel),
        Kernel::LimitableResource::SessionCountMax};
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

void IGpuSession::RequestWorker(HLERequestContext& ctx) {
    const std::scoped_lock lock{m_lock, m_shared->mutex};
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 3> command{packet[0], packet[1], packet[2]};
    Result result = Kernel::ResultInvalidCombination;
    const auto index = GP::DecodeEventIndex(command, 0x25);
    if (index) {
        result = Kernel::ResultInvalidState;
        if (m_shared->events[*index]) {
            if (m_worker) {
                result = ResultSuccess;
            } else {
                auto handler = std::make_shared<IGpuWorker>(system, m_module, m_process, m_shared);
                result = CreateWorkerKernelSession(system, ctx, std::move(handler), &m_worker);
            }
            if (result.IsSuccess()) m_shared->events[*index]->Signal(kernel);
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuSession::AcquireWorker(HLERequestContext& ctx) {
    const std::scoped_lock lock{m_lock};
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 2> command{packet[0], packet[1]};
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    if (GP::IsAcquireWorker(command)) {
        result = Kernel::ResultInvalidState;
        if (m_worker) {
            client = m_worker;
            client->Open(kernel);
            result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}

void IGpuSession::GetNotificationRange(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 2> command{packet[0], packet[1]};
    const Result result = GP::IsGetNotificationRange(command)
        ? ResultSuccess : Kernel::ResultInvalidCombination;
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push<u32>(0);
    rb.Push<u32>(GP::MaxNotificationEvents - 1);
}

void IGpuSession::GetNotificationEvent(HLERequestContext& ctx) {
    const std::scoped_lock lock{m_shared->mutex};
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 3> command{packet[0], packet[1], packet[2]};
    const auto index = GP::DecodeEventIndex(command, 0x1c);
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KReadableEvent* readable{};
    if (index) {
        auto*& event = m_shared->events[*index];
        if (!event)
            event = m_shared->event_context.CreateEvent("GpuNotification");
        if (event) {
            readable = &event->GetReadableEvent();
            readable->Open(kernel); // Move handle retains a real reference.
            result = ResultSuccess;
        } else {
            result = Kernel::ResultOutOfResource;
        }
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(readable);
    // No signal is sent: notification events are only signalled by real
    // completed GPU work, and native GPU workers are still unsupported.
}

Result IGpuSession::RegisterAddressSpace(u32 id) {
    const std::scoped_lock lock{m_shared->mutex};
    return Translate(m_shared->address_spaces.Register(id));
}

void IGpuSession::CreateAddressSpace(HLERequestContext& ctx) {
    const std::scoped_lock lock{m_shared->mutex};
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 3> command{packet[0], packet[1], packet[2]};
    Result result = Kernel::ResultInvalidCombination;
    u32 id{};
    if (GP::IsCreateAddressSpace(command)) {
        result = Kernel::ResultOutOfResource;
        if (m_shared->address_spaces.HasCapacity()) {
            if (!m_shared->nv_session)
                m_shared->nv_session = m_module->GetContainer().OpenSession(m_process).id;
            const auto fd = m_module->Open("/dev/nvhost-as-gpu",
                                           Nvidia::NvCore::SessionId{*m_shared->nv_session});
            if (fd != Nvidia::INVALID_NVDRV_FD && fd > 0) {
                // Initialise an actual NVDRV address-space FD. Native 4K
                // shader/queue mappings are not provided by this path.
                std::array<u8, 40> input{};
                std::array<u8, 40> output{};
                const u32_le big_page_size{0x20000};
                std::memcpy(input.data() + 8, &big_page_size, sizeof(big_page_size));
                Nvidia::Ioctl ioctl{};
                ioctl.raw = 0x40284109;
                if (m_module->Ioctl1(fd, ioctl, input, output) == Nvidia::NvResult::Success) {
                    const auto status = m_shared->address_spaces.Add(static_cast<u32>(fd));
                    if (status == GP::Status::Success) {
                        id = static_cast<u32>(fd);
                        result = ResultSuccess;
                    } else {
                        result = Translate(status);
                        m_module->Close(fd);
                    }
                } else {
                    m_module->Close(fd);
                    result = Kernel::ResultInvalidState;
                }
            } else if (fd != Nvidia::INVALID_NVDRV_FD) {
                m_module->Close(fd);
                result = Kernel::ResultInvalidState;
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGpuSession::GetAddressSpacePartition(HLERequestContext& ctx) {
    const std::scoped_lock lock{m_shared->mutex};
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 3> command{packet[0], packet[1], packet[2]};
    const bool user = ctx.GetCommand() == 203;
    Result result = Kernel::ResultInvalidCombination;
    std::array<u64, 9> values{};
    if (!user) values[0] = ~u64{0}; // No supervisor-owned VA range.
    const auto request = GP::DecodeAddressSpacePartition(command, ctx.GetCommand());
    if (request) {
        result = Kernel::ResultNotFound;
        if (m_shared->address_spaces.Owns(request->address_space)) {
            result = ResultSuccess;
            if (user) {
                std::array<u8, 64> input{};
                std::array<u8, 64> output{};
                const u32_le buffer_size{48};
                std::memcpy(input.data() + 8, &buffer_size, sizeof(buffer_size));
                Nvidia::Ioctl ioctl{};
                ioctl.raw = 0xc0404108;
                if (m_module->Ioctl1(static_cast<Nvidia::DeviceFD>(request->address_space),
                                     ioctl, input, output) != Nvidia::NvResult::Success) {
                    result = Kernel::ResultInvalidState;
                } else {
                    std::array<Nvidia::Devices::nvhost_as_gpu::VaRegion, 2> regions{};
                    std::memcpy(regions.data(), output.data() + 16, sizeof(regions));
                    const auto end = GP::AddressSpaceRegionEnd(
                        regions[1].offset, regions[1].pages, regions[1].page_size);
                    if (!end || regions[0].offset > *end) {
                        result = Kernel::ResultInvalidState;
                    } else {
                        values[0] = 1;
                        values[1] = regions[0].offset;
                        values[2] = *end;
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, user ? 20U : 6U};
    rb.Push(result.raw);
    for (size_t i = 0; i < (user ? values.size() : size_t{2}); ++i)
        rb.Push(values[i]);
}

void IGpuSession::CreateGraphicsDevice(HLERequestContext& ctx) {
    const std::scoped_lock lock{m_shared->mutex};
    const auto* packet=ctx.CommandBuffer();
    const std::array<GP::U32,8> command{
        packet[0],packet[1],packet[2],packet[3],
        packet[4],packet[5],packet[6],packet[7]};
    const auto outcome=m_shared->devices.Create(command);
    IPC::ResponseBuilder rb{ctx,3};
    rb.Push(Translate(outcome.status).raw);
    rb.Push(outcome.id);
}
void IGpuSession::DestroyGraphicsDevice(HLERequestContext& ctx) {
    const std::scoped_lock lock{m_shared->mutex};
    const auto* packet=ctx.CommandBuffer();
    const std::array<GP::U32,3> command{packet[0],packet[1],packet[2]};
    const auto result=m_shared->devices.Destroy(command);
    IPC::ResponseBuilder rb{ctx,2};
    rb.Push(Translate(result).raw);
}
} // namespace Service::AM
