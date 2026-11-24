// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Research-only real HLE registration, not linked in the Encore release.
// Real bounded NVDRV address-space and kernel event owners. Unsupported
// worker, queue submission and native 4K mapping return explicit errors.
#include <array>
#include <atomic>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include "common/scope_exit.h"
#include "common/swap.h"
#include <mutex>
#include <new>
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
#include "core/hle/service/nvdrv/devices/native_gpu_va_layout.h"
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"
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
        {0, &IGpuSession::Handshake, "Handshake"},
        {11, &IGpuSession::GetNotificationRange, "GetNotificationRange"},
        {12, &IGpuSession::GetNotificationEvent, "GetNotificationEvent"},
        {21, &IGpuSession::RequestWorker, "RequestWorker"},
        {22, &IGpuSession::AcquireWorker, "AcquireWorker"},
        // Lichii nx2 documents these IDs as unknown zero-reply stubs.
        // Name them explicitly for structural parity, but fail closed:
        // their ABI/side effects are not known and must not be faked.
        {23, &IGpuSession::RejectOpaqueNx2Command, "Unknown23"},
        {24, &IGpuSession::RejectOpaqueNx2Command, "Unknown24"},
        {25, &IGpuSession::RejectOpaqueNx2Command, "Unknown25"},
        {34, &IGpuSession::RejectOpaqueNx2Command, "Unknown34"},
        {51, &IGpuSession::RejectOpaqueNx2Command, "Unknown51"},
        {8001, &IGpuSession::RejectOpaqueNx2Command, "Unknown8001"},
        {8002, &IGpuSession::RejectOpaqueNx2Command, "Unknown8002"},
        {8003, &IGpuSession::RejectOpaqueNx2Command, "Unknown8003"},
        {8004, &IGpuSession::RejectOpaqueNx2Command, "Unknown8004"},
        {33, &IGpuSession::CreateAddressSpace, "CreateAddressSpace"},
        {41, &IGpuSession::CreateGraphicsDevice, "CreateGraphicsDevice"},
        {42, &IGpuSession::DestroyGraphicsDevice, "DestroyGraphicsDevice"},
        {200, &IGpuSession::SynchronizeAddressSpace, "SynchronizeGpuAddressSpace"},
        // Newer NS2 SDK contract (Lichii nx2_gsv): 201=user, 202=GSV,
        // 203=full GPU VA range. Keeping the old shifted IDs makes NVN close
        // the GPU device and can surface later as a null guest branch.
        {201, &IGpuSession::GetUserPartition, "GetUserPartition"},
        {202, &IGpuSession::GetGsvPartition, "GetGsvPartition"},
        {203, &IGpuSession::GetVaRange, "GetVaRange"},
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

void IGpuSession::Handshake(HLERequestContext& ctx) {
    // Newer NS2 SDKs send TIPC 0 with a two-word token before GPU setup.
    // Reply with ONE u32 result followed by the exact token words; a CMIF
    // Result would shift them and an unrecognized command aborts initialization.
    // ResponseBuilder zeroes the shared command buffer on construction.
    // Capture both input words before creating it or the handshake echoes 0.
    const auto* command = ctx.CommandBuffer();
    const u32 token_lo = command[2];
    const u32 token_hi = command[3];
    IPC::ResponseBuilder rb{ctx, 4};
    rb.PushRaw<u32>(ResultSuccess.raw);
    rb.PushRaw<u32>(token_lo);
    rb.PushRaw<u32>(token_hi);
}

void IGpuSession::RejectOpaqueNx2Command(HLERequestContext& ctx) {
    // These nine documented Lichii command IDs have no verified ABI.
    // Keep explicit TIPC routing without reading untrusted payload words
    // or reporting synthetic success / allocating guest-visible objects.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
}

Result IGpuSession::HandleSyncRequest(Kernel::KServerSession& session,
                                     HLERequestContext& ctx) {
    #ifdef PS5_NATIVE
    if (ctx.IsTipc()) {
        static std::atomic<unsigned> requests{0};
        const auto n = requests.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 48 || (n & (n - 1)) == 0)
            std::fprintf(stderr, "EDEN_NS2_IPC owner=gpu_session cmd=%u known=%u count=%u\n",
                         static_cast<unsigned>(ctx.GetCommand()),
                         static_cast<unsigned>(handlers_tipc.contains(ctx.GetCommand())), n);
    }
    #endif
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
    auto& server = ctx.GetManager()->GetServerManager();
    // A host request manager can allocate. Reserve it before committing
    // the kernel session so OOM cannot orphan a live session/guest handle.
    std::shared_ptr<SessionRequestManager> manager;
    try {
        manager = std::make_shared<SessionRequestManager>(kernel, server);
        manager->SetSessionHandler(std::move(handler));
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }
    auto* session = Kernel::KSession::Create(kernel);
    R_UNLESS(session != nullptr, Kernel::ResultOutOfResource);
    session->Initialize(kernel, nullptr, 0);
    reservation.Commit();
    Kernel::KSession::Register(kernel, session);
    // Initialize() owns both client/server endpoints. RegisterSession() can
    // throw bad_alloc while creating its host Session, before taking the
    // server endpoint. Close both endpoints on failure to release the
    // committed kernel session quota; never publish a guest handle.
    bool transferred_to_server = false;
    SCOPE_EXIT {
        if (!transferred_to_server) {
            session->GetClientSession().Close(kernel);
            session->GetServerSession().Close(kernel);
        }
    };
    // RegisterSession can allocate its host request queue after the kernel
    // KSession was committed. Convert host OOM into an IPC result while the
    // scope guard still owns both endpoints and their session quota.
    Result registered = Kernel::ResultOutOfResource;
    try {
        registered = server.RegisterSession(&session->GetServerSession(), std::move(manager));
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }
    R_TRY(registered);
    transferred_to_server = true;
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
                try {
                    auto handler = std::make_shared<IGpuWorker>(
                        system, m_module, m_process, m_shared);
                    result = CreateWorkerKernelSession(
                        system, ctx, std::move(handler), &m_worker);
                } catch (const std::bad_alloc&) {
                    result = Kernel::ResultOutOfResource;
                }
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
    if (GP::IsAcquireWorker(command)) {
        result = Kernel::ResultInvalidState;
        if (m_worker) {
            // libounce gsvGpuOpenWorker requests a HIPC MOVE descriptor for
            // TIPC 22. Keep one service-owned KClientSession reference so the
            // shared worker remains valid for teardown/repeated acquisition;
            // WriteToOutgoingCommandBuffer consumes only the extra reference.
            if (m_worker->Open(kernel))
                result = ResultSuccess;
        }
    }
    if (result.IsSuccess()) {
        // AddMoveObject can allocate. Roll back its transfer reference while
        // preserving the service-owned worker on a failed MOVE publication.
        try {
            ctx.AddMoveObject(m_worker);
        } catch (const std::bad_alloc&) {
            m_worker->Close(kernel);
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsSuccess()) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(result.raw);
    } else {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
}

void IGpuSession::GetNotificationRange(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 2> command{packet[0], packet[1]};
    const Result result = GP::IsGetNotificationRange(command)
        ? ResultSuccess : Kernel::ResultInvalidCombination;
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push<u32>(0);
    // An invalid request must not advertise a valid guest event index.
    rb.Push<u32>(result.IsSuccess() ? GP::MaxNotificationEvents - 1 : 0);
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
        if (!event) {
            // Creating a kernel notification may allocate host event state.
            // OOM must return a normal TIPC resource error with no handle and
            // no published half-created event.
            try {
                event = m_shared->event_context.CreateEvent("GpuNotification");
            } catch (const std::bad_alloc&) {
                event = nullptr;
            }
        }
        if (event) {
            readable = &event->GetReadableEvent();
            result = ResultSuccess;
        } else {
            result = Kernel::ResultOutOfResource;
        }
    }
    // libounce gsvGpuCreateSlotEvent requests a HIPC MOVE descriptor for
    // TIPC 12. ServiceContext later closes its original readable reference;
    // retain that owner and move an explicitly opened transfer reference.
    if (result.IsSuccess() && !readable->Open(kernel))
        result = Kernel::ResultInvalidState;
    if (result.IsSuccess()) {
        // The event remains owned by the service. A failed MOVE publication
        // must close only the extra readable-event transfer reference.
        try {
            ctx.AddMoveObject(readable);
        } catch (const std::bad_alloc&) {
            readable->Close(kernel);
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsSuccess()) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(result.raw);
    } else {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    // Notification events signal only after real completed GPU work.
}

std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
IGpuSession::ResolveOpenedAddressSpace(u32 id) {
    // VM ids are GSV-owned opaque tokens. Resolution succeeds only after a
    // separately opened NVDRV address-space descriptor imports that token.
    return m_shared->FindOwnedAddressSpace(id);
}

Result IGpuSession::RegisterAddressSpace(u32 id) {
    const std::scoped_lock lock{m_shared->mutex};
    if (!m_shared->address_spaces.Owns(id))
        return Kernel::ResultNotFound;
    if (!Nvidia::Devices::NativeGpuProcessRegistry::Instance().RegisterVm(m_process, id))
        return Kernel::ResultInvalidState;
    return Translate(m_shared->address_spaces.Register(id));
}

std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
IGpuSession::GetRegisteredAddressSpace(u32 id) {
    const std::scoped_lock lock{m_shared->mutex};
    if (!m_shared->address_spaces.Registered(id))
        return {};
    return ResolveOpenedAddressSpace(id);
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
            const auto candidate =
                Nvidia::Devices::NativeGpuProcessRegistry::Instance().AllocateVm(
                    m_process, m_shared);
            if (candidate) {
                const auto status = m_shared->address_spaces.Add(*candidate);
                if (status == GP::Status::Success) {
                    id = *candidate;
                    result = ResultSuccess;
                } else {
                    (void)Nvidia::Devices::NativeGpuProcessRegistry::Instance().RemoveVm(
                        m_process, *candidate, m_shared.get());
                    result = Translate(status);
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGpuSession::SynchronizeAddressSpace(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 4> command{packet[0], packet[1], packet[2], packet[3]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = GP::DecodeSynchronizeAddressSpace(command)) {
        const std::scoped_lock lock{m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->address_spaces.Owns(request->address_space)) {
            result = Kernel::ResultInvalidState;
            // Native worker map/unmap/remap transactions are synchronous:
            // by the time this parent-session request is handled their GMMU
            // mutation has either committed or returned an error. Completion
            // therefore means signalling the SDK job event; a global GPU idle
            // would serialize unrelated graphics work and is not required.
            if (ResolveOpenedAddressSpace(request->address_space) &&
                m_shared->events[request->notification]) {
                m_shared->events[request->notification]->Signal(kernel);
                result = ResultSuccess;
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuSession::GetUserPartition(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 3> command{packet[0], packet[1], packet[2]};
    Result result = Kernel::ResultInvalidCombination;
    u64 first{};
    u64 size{};
    if (const auto request = GP::DecodeAddressSpacePartition(command, 201)) {
        result = Kernel::ResultNotFound;
        const std::scoped_lock lock{m_shared->mutex};
        if (m_shared->address_spaces.Owns(request->address_space)) {
            // Lichii's current newer-SDK contract: the user partition is the
            // range published to NVN. Unlike the 1-TiB GMMU page-table capacity,
            // the guest-visible user partition ends at 0xC000000000
            // (768 GiB). Do not return the 1-TiB GMMU ceiling as the
            // end/size: it advertises VA outside the proven NVN range.
            first = Nvidia::Devices::NativeNs2GpuVaStart;
            size = Nvidia::Devices::NativeNs2GpuVaSmallSplit - first;
            result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result.raw);
    rb.Push(first);
    rb.Push(size);
}

void IGpuSession::GetGsvPartition(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 3> command{packet[0], packet[1], packet[2]};
    Result result = Kernel::ResultInvalidCombination;
    u64 first{};
    u64 last{};
    if (const auto request = GP::DecodeAddressSpacePartition(command, 202)) {
        result = Kernel::ResultNotFound;
        const std::scoped_lock lock{m_shared->mutex};
        if (m_shared->address_spaces.Owns(request->address_space)) {
            // Empty partition is min > max. Returning 0,0 is a one-byte
            // partition at address zero to newer SDKs and makes them abort.
            first = ~u64{0};
            last = 0;
            result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result.raw);
    rb.Push(first);
    rb.Push(last);
}

void IGpuSession::GetVaRange(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<GP::U32, 3> command{packet[0], packet[1], packet[2]};
    Result result = Kernel::ResultInvalidCombination;
    // libounce gsvGpuGetVmUserRanges consumes:
    //   u64 count; GsvGpuVaRange ranges[4] ({u64 first,u64 last}).
    // Keep the unused three records zero and publish the one proven NVN
    // user interval with an inclusive end.
    std::array<u64_le, 9> payload{};
    if (const auto request = GP::DecodeAddressSpacePartition(command, 203)) {
        result = Kernel::ResultNotFound;
        const std::scoped_lock lock{m_shared->mutex};
        if (m_shared->address_spaces.Owns(request->address_space)) {
            payload[0] = 1;
            payload[1] = Nvidia::Devices::NativeNs2GpuVaStart;
            payload[2] = Nvidia::Devices::NativeNs2GpuVaSmallSplit - 1;
            result = ResultSuccess;
        }
    }
    // Result word + 72-byte output payload = 19 TIPC words.
    IPC::ResponseBuilder rb{ctx, 20};
    rb.Push(result.raw);
    rb.PushRaw(payload);
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
