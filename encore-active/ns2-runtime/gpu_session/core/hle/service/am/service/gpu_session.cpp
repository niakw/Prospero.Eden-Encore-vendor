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
    R_TRY(server.RegisterSession(&session->GetServerSession(), std::move(manager)));
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
    Kernel::KClientSession* client{};
    if (GP::IsAcquireWorker(command)) {
        result = Kernel::ResultInvalidState;
        if (m_worker) {
            client = m_worker;
            client->Open(kernel);
            // AddMoveObject can allocate its outgoing descriptor list.
            // Stage the move BEFORE the success reply; roll back the
            // independently opened KClientSession reference on OOM.
            try {
                ctx.AddMoveObject(client);
                result = ResultSuccess;
            } catch (const std::bad_alloc&) {
                client->Close(kernel);
                result = Kernel::ResultOutOfResource;
            }
        }
    }
    // The moved client belongs to the IPC response only after its
    // descriptor was successfully staged; error replies transfer nothing.
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
        if (!event) {
            // Creating a kernel notification may allocate host event state.
            // OOM must return a normal TIPC resource error with no COPY
            // handle and no published half-created event.
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
    // GPU session cmd 12 returns a COPY handle, not a moved handle.
    // Stage the outgoing object before writing a success descriptor.
    // Its host vector can allocate; an OOM must return an ordinary error
    // with zero COPY handles rather than leaving a success reply behind.
    if (result.IsSuccess()) {
        try {
            ctx.AddCopyObject(readable);
        } catch (const std::bad_alloc&) {
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsSuccess()) {
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(result.raw);
    } else {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    // Notification events signal only after real completed GPU work.
}

std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
IGpuSession::ResolveOpenedAddressSpace(u32 id) {
    // The worker's independent KSession uses the same shared FD identity.
    return m_shared->FindOwnedAddressSpace(id);
}

Result IGpuSession::RegisterAddressSpace(u32 id) {
    const std::scoped_lock lock{m_shared->mutex};
    if (!m_shared->address_spaces.Owns(id))
        return Kernel::ResultNotFound;
    if (!ResolveOpenedAddressSpace(id))
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
            if (!m_shared->nv_session) {
                // NvCore::Container::OpenSession can allocate a new session
                // slot. Do not unwind through the HLE IPC dispatcher on host
                // OOM; no GPU FD or guest-visible address-space ID exists yet.
                try {
                    m_shared->nv_session =
                        m_module->GetContainer().OpenSession(m_process).id;
                } catch (const std::bad_alloc&) {
                    IPC::ResponseBuilder rb{ctx, 3};
                    rb.Push(Kernel::ResultOutOfResource.raw);
                    rb.Push<u32>(0);
                    return;
                }
            }
            // Module::Open constructs a guest FD through an allocating map.
            // Do not unwind through TIPC when that allocation fails: no
            // address-space slot or guest-visible FD has been published.
            Nvidia::DeviceFD fd = Nvidia::INVALID_NVDRV_FD;
            try {
                fd = m_module->Open("/dev/nvhost-as-gpu",
                                    Nvidia::NvCore::SessionId{*m_shared->nv_session});
            } catch (const std::bad_alloc&) {
                // result is already ResultOutOfResource.
            }
            if (fd != Nvidia::INVALID_NVDRV_FD && fd > 0) {
                // Initialise an actual NVDRV address-space FD. Native 4K
                // shader/queue mappings are not provided by this path.
                std::array<u8, 40> input{};
                std::array<u8, 40> output{};
                const u32_le big_page_size{0x20000};
                std::memcpy(input.data() + 8, &big_page_size, sizeof(big_page_size));
                // The inherited 37-bit GPU VA defaults reject native NS2
                // GPU-only render targets at 0x81_0000_0000 (above 512 GiB).
                // Explicitly request 40-bit VA for THIS NS2 TIPC-created FD.
                // MapNative4K and ReserveNativeSparse4K must still enforce
                // live ownership and a real backing before a render succeeds.
                const u64_le range_start{Nvidia::Devices::NativeNs2GpuVaStart};
                const u64_le range_end{Nvidia::Devices::NativeNs2GpuVaEnd};
                const u64_le range_split{Nvidia::Devices::NativeNs2GpuVaSmallSplit};
                std::memcpy(input.data() + 16, &range_start, sizeof(range_start));
                std::memcpy(input.data() + 24, &range_end, sizeof(range_end));
                std::memcpy(input.data() + 32, &range_split, sizeof(range_split));
                Nvidia::Ioctl ioctl{};
                ioctl.raw = 0x40284109;
                // Allocation inside the GMMU address-space initialization is
                // fallible. Close the just-opened descriptor on OOM; no
                // partially initialized VA object may escape through IPC.
                Nvidia::NvResult ioctl_result = Nvidia::NvResult::InvalidState;
                try {
                    ioctl_result = m_module->Ioctl1(fd, ioctl, input, output);
                } catch (const std::bad_alloc&) {
                    m_module->Close(fd);
                    IPC::ResponseBuilder rb{ctx, 3};
                    rb.Push(Kernel::ResultOutOfResource.raw);
                    rb.Push<u32>(0);
                    return;
                }
                if (ioctl_result == Nvidia::NvResult::Success) {
                    // This FD comes from the explicit nvhost-as-gpu open above.
                    // Capture its concrete identity once; every later lookup
                    // compares a safe nvdevice base, never a foreign static cast.
                    const auto as = m_module->GetDevice<Nvidia::Devices::nvhost_as_gpu>(fd);
                    if (!as || !as->OwnsNativeProcess(m_process)) {
                        m_module->Close(fd);
                        result = Kernel::ResultInvalidState;
                    } else {
                        // The identity map may allocate, while AddressSpaceSlots
                        // is a fixed-size, non-allocating registry. Reserve the
                        // fallible map node FIRST: an OOM must not leave a live
                        // NVDRV FD in the process registry without an owner.
                        const u32 candidate = static_cast<u32>(fd);
                        try {
                            const auto [it, inserted] =
                                m_shared->opened_gpu_as.try_emplace(candidate, as);
                            if (!inserted) {
                                result = Kernel::ResultInvalidState;
                            } else {
                                const auto status = m_shared->address_spaces.Add(candidate);
                                if (status == GP::Status::Success) {
                                    id = candidate;
                                    result = ResultSuccess;
                                } else {
                                    m_shared->opened_gpu_as.erase(it);
                                    result = Translate(status);
                                }
                            }
                        } catch (const std::bad_alloc&) {
                            result = Kernel::ResultOutOfResource;
                        }
                        if (result.IsError()) m_module->Close(fd);
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
        // A guest may have closed the NVDRV descriptor while the TIPC
        // session kept its numeric ID. Validate the exact opened device
        // identity before either returning a success partition or invoking
        // an IOCTL that might otherwise target a recycled foreign FD.
        const bool owned = m_shared->address_spaces.Owns(request->address_space);
        if (owned && ResolveOpenedAddressSpace(request->address_space)) {
            result = ResultSuccess;
            if (user) {
                std::array<u8, 64> input{};
                std::array<u8, 64> output{};
                const u32_le buffer_size{48};
                std::memcpy(input.data() + 8, &buffer_size, sizeof(buffer_size));
                Nvidia::Ioctl ioctl{};
                ioctl.raw = 0xc0404108;
                // The partition query is an allocating NVDRV ioctl on
                // some driver paths. Never unwind through IPC or return an
                // apparently valid VA partition on host OOM.
                Nvidia::NvResult ioctl_result = Nvidia::NvResult::InvalidState;
                bool ioctl_oom = false;
                try {
                    ioctl_result = m_module->Ioctl1(
                        static_cast<Nvidia::DeviceFD>(request->address_space),
                        ioctl, input, output);
                } catch (const std::bad_alloc&) {
                    ioctl_oom = true;
                }
                if (ioctl_oom) {
                    result = Kernel::ResultOutOfResource;
                } else if (ioctl_result != Nvidia::NvResult::Success) {
                    result = Kernel::ResultInvalidState;
                } else {
                    std::array<Nvidia::Devices::nvhost_as_gpu::VaRegion, 2> regions{};
                    std::memcpy(regions.data(), output.data() + 16, sizeof(regions));
                    // Both NVDRV regions must describe finite, non-empty
                    // spans. The first descriptor is the published lower
                    // bound: validating only the second permitted malformed
                    // first-page counts to produce a successful guest reply.
                    const auto first_end = GP::AddressSpaceRegionEnd(
                        regions[0].offset, regions[0].pages, regions[0].page_size);
                    const auto end = GP::AddressSpaceRegionEnd(
                        regions[1].offset, regions[1].pages, regions[1].page_size);
                    if (!first_end || !end || regions[0].offset > regions[1].offset ||
                        *first_end > *end) {
                        result = Kernel::ResultInvalidState;
                    } else {
                        values[0] = 1;
                        values[1] = regions[0].offset;
                        values[2] = *end;
                    }
                }
            }
        } else if (owned) {
            result = Kernel::ResultInvalidState;
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
