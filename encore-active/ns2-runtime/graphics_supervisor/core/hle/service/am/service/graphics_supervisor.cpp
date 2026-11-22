// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Successful NS2 supervisor commands have real kernel IPC backing, but
// NO native NVDRV address space, event, worker, queue or PS5 rendering path.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "common/logging.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include <memory>
#include <limits>
#include <chrono>
#include <mutex>
#include <new>
#include <vector>
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/service/sm/sm.h"
#include <span>
#include "core/core.h"
#include "video_core/gpu.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/gpu_session.h"
#include "core/hle/service/am/service/graphics_supervisor.h"
#include "core/hle/service/am/service/graphics_video_decoder_session.h"
#include "core/hle/service/am/service/native_image_layout.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/core_timing.h"
#include "core/memory.h"
// Lichii nx2_gsv: calls through Core::System::Host1x() require the complete Host1x type.
// Keep this dependency direct; do not rely on unrelated transitive GPU headers.
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/syncpoint_manager.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/nvnflinger/buffer_queue_producer.h"
#include "core/hle/service/nvnflinger/ui/graphic_buffer.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_root_service.h"
#include "core/hle/service/vi/vi_types.h"

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
                                    std::shared_ptr<ServiceFrameworkBase> handler,
                                    Kernel::KClientSession** out_client) {
    auto& kernel = system.Kernel();
    Kernel::KScopedResourceReservation reservation{
        kernel, Kernel::GetCurrentProcessPointer(kernel), Kernel::LimitableResource::SessionCountMax};
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
    // RegisterSession may fail or allocate after the quota was committed.
    // Until the server accepts its endpoint, close BOTH endpoints so that
    // guest session quota and the process-owned kernel objects are returned.
    // Match the already-reviewed GPU worker session rollback contract.
    bool transferred_to_server = false;
    SCOPE_EXIT {
        if (!transferred_to_server) {
            session->GetClientSession().Close(kernel);
            session->GetServerSession().Close(kernel);
        }
    };
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

static void NativeTipcReplyWords(HLERequestContext& ctx, std::initializer_list<u32> words) {
    IPC::ResponseBuilder rb{ctx, static_cast<u32>(2 + words.size())};
    rb.PushRaw<u32>(ResultSuccess.raw);
    for (const u32 word : words) rb.PushRaw<u32>(word);
}


static std::optional<NativeVideoImageLayout> CalculateChatCameraImageLayout(
    std::span<const u32> preference, u32 count) {
    // MKW 1.8.0 asks for three 384x216 local-camera images with format 0x38.
    // This is a block-linear four-byte image in nn::chat::gsv, not one of
    // the decoder-pool pitch-linear formats handled by command 911.
    if (preference.size() != 16 || count == 0 || count > NativeVideoPoolLedger::MaxImages ||
        preference[6] != 0x38) {
        return {};
    }
    const u64 width = (preference[2] & 0xffffU) + 1;
    const u64 height = (preference[2] >> 16) + 1;
    const u32 requested_block_log2 = preference[4];
    const u64 requested_pitch_gobs = preference[5];
    constexpr u64 bytes_per_pixel = 4;
    const u64 minimum_gobs = (width * bytes_per_pixel + 63) / 64;

    u32 block_log2 = requested_block_log2;
    u64 pitch{};
    if (block_log2 <= 8 && requested_pitch_gobs >= minimum_gobs &&
        requested_pitch_gobs <= 4096) {
        pitch = requested_pitch_gobs * 64;
    } else {
        block_log2 = 7;
        pitch = (width * bytes_per_pixel + 63) & ~u64{63};
    }
    const u64 block_rows = u64{1} << block_log2;
    if (height > std::numeric_limits<u64>::max() - (block_rows - 1)) {
        return {};
    }
    const u64 aligned_height = (height + block_rows - 1) & ~(block_rows - 1);
    if (pitch == 0 || aligned_height > std::numeric_limits<u64>::max() / pitch) {
        return {};
    }
    const u64 raw_bytes = pitch * aligned_height;
    const u64 alignment = std::max<u64>(4096, 64 * block_rows);
    if (raw_bytes > std::numeric_limits<u64>::max() - (alignment - 1)) {
        return {};
    }
    const u64 image_bytes = (raw_bytes + alignment - 1) & ~(alignment - 1);
    if (image_bytes == 0 || image_bytes > std::numeric_limits<u32>::max() ||
        image_bytes > std::numeric_limits<u64>::max() / count) {
        return {};
    }
    return NativeVideoImageLayout{
        image_bytes * count,
        static_cast<u32>(image_bytes),
        static_cast<u32>(image_bytes),
        static_cast<u32>(std::min<u64>(pitch, std::numeric_limits<u32>::max())),
        false,
        0,
        8,
        true,
        preference[1],
        block_log2,
        static_cast<u32>(pitch / 64),
    };
}

class INativeSyncptSession final : public ServiceFramework<INativeSyncptSession> {
public:
    explicit INativeSyncptSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:syncpt"}, m_context{system_, "nx2:syncpt"} {
        static const FunctionInfo f[] = {
            {0, &INativeSyncptSession::Handshake, "Handshake"},
            {1, &INativeSyncptSession::GetCount, "GetCount"},
            {21, &INativeSyncptSession::Zero, "Unknown21"},
            {22, &INativeSyncptSession::GetSlotEvent, "GetSlotEvent"},
            {103, &INativeSyncptSession::QueryCounter, "QueryCounter"},
            {201, &INativeSyncptSession::RequestSlotEvent, "RequestSlotEvent"},
            {202, &INativeSyncptSession::CancelSlotEvent, "CancelSlotEvent"},
        };
        RegisterHandlersTipc(f);
    }
    ~INativeSyncptSession() override {
        {
            std::scoped_lock lk{m_shared->mutex};
            m_shared->alive = false;
        }
        for (auto& request : m_requests) Cancel(request);
        for (auto* event : m_events) if (event) m_context.CloseEvent(event);
    }
    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override {
#ifdef PS5_NATIVE
        if (ctx.IsTipc()) {
            const auto n = m_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 32 || (n & (n - 1)) == 0) {
                std::fprintf(stderr,
                    "EDEN_NS2_IPC owner=syncpt cmd=%u known=%u count=%u\n",
                    static_cast<unsigned>(ctx.GetCommand()),
                    static_cast<unsigned>(handlers_tipc.contains(ctx.GetCommand())), n);
            }
        }
#endif
        return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
    }
private:
    static constexpr u32 HostSyncpoints = Tegra::Host1x::SyncpointManager::NUM_MAX_SYNCPOINTS;
    static constexpr u32 MaxSlots = 64;
    struct SlotRequest {
        u32 syncpoint{};
        std::atomic<bool> pending{};
        Tegra::Host1x::SyncpointManager::ActionHandle handle{};
    };
    struct SharedState {
        std::mutex mutex;
        bool alive{true};
    };
    Kernel::KEvent* EventFor(u32 slot) {
        if (slot >= MaxSlots) return nullptr;
        auto*& event = m_events[slot];
        if (event) return event;
        char name[48]{};
        std::snprintf(name, sizeof(name), "nx2:syncpt:Slot%u", slot);
        try {
            event = m_context.CreateEvent(name);
        } catch (const std::bad_alloc&) {
            event = nullptr;
        }
        return event;
    }
    void Handshake(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        NativeTipcReplyWords(ctx, {w[2], w[3]});
    }
    void GetCount(HLERequestContext& ctx) {
        NativeTipcReplyWords(ctx, {32});
    }
    void Zero(HLERequestContext& ctx) {
        NativeTipcReplyWords(ctx, {0});
    }
    void QueryCounter(HLERequestContext& ctx) {
        const u32 syncpoint = ~ctx.CommandBuffer()[2];
        const u64 value = syncpoint < HostSyncpoints
            ? system.Host1x().GetSyncpointManager().GetGuestSyncpointValue(syncpoint) : 0;
        NativeTipcReplyWords(ctx, {static_cast<u32>(value), static_cast<u32>(value >> 32), 0});
    }
    void GetSlotEvent(HLERequestContext& ctx) {
        const u32 slot = ctx.CommandBuffer()[2];
        auto* event = EventFor(slot);
        if (!event) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultOutOfResource.raw);
            return;
        }
        auto* readable = &event->GetReadableEvent();
        // libounce gsvSyncCreateEvent requests a HIPC MOVE handle. The
        // SyncptSession still owns/signals this event, so transfer one extra
        // readable-event reference and retain the original until teardown.
        if (!readable->Open(system.Kernel())) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidState.raw);
            return;
        }
        try {
            ctx.AddMoveObject(readable);
        } catch (const std::bad_alloc&) {
            readable->Close(system.Kernel());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultOutOfResource.raw);
            return;
        }
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess.raw);
    }
    void RequestSlotEvent(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const u32 slot = w[2];
        const u32 syncpoint = ~w[3];
        const u64 value = u64{w[4]} | (u64{w[5]} << 32);
        auto* event = EventFor(slot);
        if (!event) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(slot >= MaxSlots ? Kernel::ResultInvalidArgument.raw
                                    : Kernel::ResultOutOfResource.raw);
            return;
        }
        if (syncpoint >= HostSyncpoints) {
            event->Signal(system.Kernel());
            NativeTipcReplyWords(ctx, {});
            return;
        }
        auto& request = m_requests[slot];
        Cancel(request);
        event->Clear(system.Kernel());
        request.syncpoint = syncpoint;
        request.pending.store(true, std::memory_order_release);
        auto& manager = system.Host1x().GetSyncpointManager();
        request.handle = manager.RegisterGuestAction(
            syncpoint, static_cast<u32>(value),
            [shared=m_shared, &kernel=system.Kernel(), request=&request, event] {
                std::scoped_lock lk{shared->mutex};
                if (!shared->alive) return;
                request->pending.store(false, std::memory_order_release);
                event->Signal(kernel);
            });
        NativeTipcReplyWords(ctx, {});
    }
    void CancelSlotEvent(HLERequestContext& ctx) {
        const u32 slot = ctx.CommandBuffer()[2];
        if (slot >= MaxSlots) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidArgument.raw);
            return;
        }
        Cancel(m_requests[slot]);
        auto* event = EventFor(slot);
        if (!event) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultOutOfResource.raw);
            return;
        }
        event->Signal(system.Kernel());
        NativeTipcReplyWords(ctx, {});
    }
    bool Cancel(SlotRequest& request) {
        if (!request.pending.exchange(false, std::memory_order_acq_rel)) return false;
        system.Host1x().GetSyncpointManager().DeregisterGuestAction(request.syncpoint,
                                                                    request.handle);
        request.handle = 0;
        return true;
    }
    KernelHelpers::ServiceContext m_context;
    std::array<Kernel::KEvent*, MaxSlots> m_events{};
    std::array<SlotRequest, MaxSlots> m_requests{};
    std::shared_ptr<SharedState> m_shared = std::make_shared<SharedState>();
    std::atomic<u32> m_trace_count{};
};

struct NativeSyncptState : std::enable_shared_from_this<NativeSyncptState> {
    static constexpr u32 ValuesOffset = 0x40;
    static constexpr u32 RoSummaryOffset = 0x1040;
    static constexpr u32 RoBitsOffset = 0x1100;
    static constexpr u32 RwSummaryOffset = 0x0;
    static constexpr u32 RwBitsOffset = 0x100;
    static constexpr u32 HostSyncpoints = Tegra::Host1x::SyncpointManager::NUM_MAX_SYNCPOINTS;
    static constexpr std::chrono::nanoseconds PollInterval{std::chrono::microseconds{500}};

    NativeSyncptState(Core::System& system_, Kernel::KProcess* process_)
        : system{system_}, process{process_}, context{system_, "nx2:gsv:syncpt"} {}
    ~NativeSyncptState() {
        Shutdown();
        for (auto* event : events) if (event) context.CloseEvent(event);
    }
    void Shutdown() {
        if (stopped.exchange(true, std::memory_order_acq_rel)) return;
        system.Host1x().GetSyncpointManager().SetGuestMirror({});
        if (poll) {
            system.CoreTiming().UnscheduleEvent(poll);
            poll.reset();
        }
    }
    static size_t TipcDataOffset(const u32* w) {
        if ((w[1] & 0x80000000) == 0) return 2;
        const u32 desc = w[2];
        return 3 + ((desc & 1) ? 2 : 0) + ((desc >> 1) & 0xF) + ((desc >> 5) & 0xF);
    }
    bool EnsureEvents() {
        if (events[0] && events[1]) return true;
        try {
            if (!events[0]) events[0] = context.CreateEvent("nx2:gsv:SyncptRequest");
            if (!events[1]) events[1] = context.CreateEvent("nx2:gsv:SyncptNotify");
        } catch (const std::bad_alloc&) {
            return false;
        }
        return events[0] && events[1];
    }
    void Attach(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const size_t data = TipcDataOffset(w);
        if (data + 1 >= IPC::COMMAND_BUFFER_LENGTH) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidCombination.raw);
            return;
        }
        auto rw = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(0));
        auto ro = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(1));
        if (rw.IsNull() || ro.IsNull() || rw->GetOwner() != process || ro->GetOwner() != process ||
            rw->GetSize() < 0x1000 || ro->GetSize() < 0x2000) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidHandle.raw);
            return;
        }
        if (!EnsureEvents()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultOutOfResource.raw);
            return;
        }
        if (memory != nullptr) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidState.raw);
            return;
        }
        const u32 requested = w[data + 1];
        count = std::min<u32>(requested, (0x1000 - 0x40) / 8);
        memory = &ro->GetOwner()->GetMemory();
        rw_address = GetInteger(rw->GetSourceAddress());
        ro_address = GetInteger(ro->GetSourceAddress());
        memory->Write32(ro_address + 0x00, 1);
        memory->Write32(ro_address + 0x04, 0);
        memory->Write32(ro_address + 0x08, count);
        memory->Write32(ro_address + 0x0c, ValuesOffset);
        memory->Write32(ro_address + 0x10, RwSummaryOffset);
        memory->Write32(ro_address + 0x14, RoSummaryOffset);
        memory->Write32(ro_address + 0x18, RwBitsOffset);
        memory->Write32(ro_address + 0x1c, RoBitsOffset);
        UpdateValues();
        Install();
        IPC::ResponseBuilder rb{ctx, 2, 2, 0};
        rb.Push(ResultSuccess.raw);
        rb.PushCopyObjects(ctx, *events[0]);
        rb.PushCopyObjects(ctx, events[1]->GetReadableEvent());
    }
    bool UpdateValues(std::array<u64, 0x40>* changed=nullptr) {
        if (!memory || stopped.load(std::memory_order_acquire)) return false;
        auto& manager = system.Host1x().GetSyncpointManager();
        bool any = false;
        manager.WithGuestValuesStable([&] {
            const u32 limit = std::min(count, HostSyncpoints);
            for (u32 id=0; id<limit; ++id) {
                const u32 value = manager.GetGuestSyncpointValue(id);
                const u64 address = ro_address + ValuesOffset + u64{id} * 8;
                if (memory->Read64(address) != value) {
                    memory->Write64(address, value);
                    any = true;
                }
                if (changed && polled_values[id] != value)
                    (*changed)[id / 64] |= u64{1} << (id % 64);
                polled_values[id] = value;
            }
        });
        return any;
    }
    void Poll() {
        if (!memory || stopped.load(std::memory_order_acquire) || !events[1]) return;
        const u32 words = (count + 63) / 64;
        std::array<u64, 0x40> armed{};
        for (u32 i=0; i<words && i<armed.size(); ++i)
            armed[i] = memory->Read64(rw_address + RwBitsOffset + u64{i} * 8);
        std::array<u64, 0x40> changed{};
        UpdateValues(&changed);
        bool acked=false;
        for (u32 i=0; i<words && i<armed.size(); ++i) {
            const u64 address = ro_address + RoBitsOffset + u64{i} * 8;
            if (memory->Read64(address) != armed[i]) {
                memory->Write64(address, armed[i]);
                acked=true;
            }
            if ((armed[i] & changed[i]) != 0) acked=true;
        }
        memory->Write64(ro_address + RoSummaryOffset,
                        memory->Read64(rw_address + RwSummaryOffset));
        if (acked) events[1]->Signal(system.Kernel());
    }
    void Install() {
        stopped.store(false, std::memory_order_release);
        std::weak_ptr<NativeSyncptState> weak = shared_from_this();
        system.Host1x().GetSyncpointManager().SetGuestMirror(
            [weak](u32 id,u32 value) {
                if (auto self=weak.lock();
                    self && !self->stopped.load(std::memory_order_acquire) &&
                    self->memory && id<std::min(self->count,HostSyncpoints)) {
                    self->memory->Write64(self->ro_address + ValuesOffset + u64{id}*8, value);
                }
            });
        if (!poll) {
            poll=Core::Timing::CreateEvent("nx2:gsv:SyncptPoll",
                [weak](s64,std::chrono::nanoseconds)->std::optional<std::chrono::nanoseconds>{
                    if (auto self=weak.lock()) self->Poll();
                    return std::nullopt;
                });
            system.CoreTiming().ScheduleLoopingEvent(PollInterval,PollInterval,poll);
        }
    }
    Core::System& system;
    Kernel::KProcess* process;
    KernelHelpers::ServiceContext context;
    std::array<Kernel::KEvent*,2> events{};
    Core::Memory::Memory* memory{};
    u64 rw_address{};
    u64 ro_address{};
    u32 count{};
    std::array<u32,HostSyncpoints> polled_values{};
    std::shared_ptr<Core::Timing::EventType> poll;
    std::atomic<bool> stopped{false};
};

// VI2 uses the actual VI compositor; a frame is acknowledged only when
// Nvnflinger QueueBuffer accepts a real, process-owned NVMap GraphicBuffer.
class IVi2Session final : public ServiceFramework<IVi2Session> {
    static constexpr u64 Default = 0x00746c7561666564ULL;
    struct Client {
        u64 token{};
        u32 signals{};
        u32 release_bit{};
        u32 available_bit{};
        std::array<float,3> output_levels{};
        bool output_levels_set{};
        std::optional<u64> display;
        std::optional<u64> layer;
        u64 available{0x7fff};
        std::array<std::optional<s32>,15> queued{};
        std::array<u32,15> images{};
        std::array<std::shared_ptr<Nvidia::Module>,15> queued_modules{};
        // Retain buffer pins through the real producer's Free->DetachNext
        // transition, and until its returned release fence is satisfied.
        std::array<std::shared_ptr<android::GraphicBuffer>,15> retained{};
        std::array<std::optional<android::Fence>,15> release_fences{};
        std::shared_ptr<android::BufferQueueProducer> producer;
    };
public:
    IVi2Session(Core::System& sys, std::shared_ptr<IGraphicsSupervisor> owner, u64 aruid)
        : ServiceFramework{sys,"IVi2Session"}, m_owner{std::move(owner)},
          m_aruid{aruid}, m_context{sys,"NativeVi2"} {
        static const FunctionInfo f[]{
            {0,&IVi2Session::Handshake,"Handshake"},
            {1,&IVi2Session::Limit,"GetNotificationLimit"},
            {3,&IVi2Session::Limit,"GetSessionLimit"},
            {5,&IVi2Session::Limit,"GetRequestGroupLimit"},
            {11,&IVi2Session::Event,"GetNotificationEvent"},
            {21,&IVi2Session::Notifications,"GetNotifications"},
            // NVN2 waits on command 32 before its first GPU submission.
            // These are independent 60Hz vblank events, not the 11 notification.
            {31,&IVi2Session::VsyncEvent,"GetVsyncEvent"},
            {32,&IVi2Session::VsyncEvent,"GetDisplayVsyncEvent"},
            {261,&IVi2Session::ConfigureNativeDisplayEvent,"BindDisplayVsyncEvent"},
            {262,&IVi2Session::ConfigureNativeDisplayEvent,"UnbindDisplayVsyncEvent"},
            {263,&IVi2Session::ConfigureNativeDisplayEvent,"BindDisplayRefreshRateEvent"},
            {264,&IVi2Session::ConfigureNativeDisplayEvent,"UnbindDisplayRefreshRateEvent"},
            {265,&IVi2Session::ConfigureNativeDisplayEvent,"BindDisplayHdrStatusEvent"},
            {266,&IVi2Session::ConfigureNativeDisplayEvent,"UnbindDisplayHdrStatusEvent"},
            {267,&IVi2Session::ConfigureNativeDisplayEvent,"BindDisplayHdrPaperWhiteEvent"},
            {268,&IVi2Session::ConfigureNativeDisplayEvent,"UnbindDisplayHdrPaperWhiteEvent"},
            {269,&IVi2Session::ConfigureNativeDisplayEvent,"BindDisplayBufferMaxResolutionEvent"},
            {270,&IVi2Session::ConfigureNativeDisplayEvent,"UnbindDisplayBufferMaxResolutionEvent"},
            {271,&IVi2Session::ConfigureNativeDisplayEvent,"BindDisplayModeCapabilityEvent"},
            {272,&IVi2Session::ConfigureNativeDisplayEvent,"UnbindDisplayModeCapabilityEvent"},
            // Observed NVN2 VI2 request-group/display polling controls.
            // The upstream NX2 service acknowledges these without changing
            // display/producer state. Keep other unknown IDs fail-closed.
            {51,&IVi2Session::AcknowledgeDisplayPoll,"ConfigureRequestGroups"},
            {103,&IVi2Session::QueryRefreshRate,"QueryDisplayRefreshRate"},
            {104,&IVi2Session::QueryHdrStatus,"QueryDisplayHdrStatus"},
            {105,&IVi2Session::QueryPaperWhite,"QueryDisplayHdrPaperWhite"},
            {101,&IVi2Session::Open,"OpenDisplay"},
            {102,&IVi2Session::Close,"CloseDisplay"},
            {201,&IVi2Session::Register,"RegisterClientState"},
            {202,&IVi2Session::Unregister,"UnregisterClientState"},
            {212,&IVi2Session::Availability,"GetClientAvailability"},
            {229,&IVi2Session::Present,"PresentClientBuffer"},
            {228,&IVi2Session::Cancel,"CancelClientBuffer"},
            {251,&IVi2Session::Bind,"BindClientDisplay"},
            {252,&IVi2Session::Unbind,"UnbindClientDisplay"},
            {351,&IVi2Session::Encoding,"SetClientOutputEncoding"},
            {352,&IVi2Session::OutputLevels,"SetClientOutputLevels"},
            {111,&IVi2Session::OpenEndpoint,"OpenEndpoint"},
            {112,&IVi2Session::CloseEndpoint,"CloseEndpoint"},
            {106,&IVi2Session::MaxResolution,"GetBufferMaxResolution"},
        };
        RegisterHandlersTipc(f);
    }
    ~IVi2Session() override {
        if (m_vsync_timer) system.CoreTiming().UnscheduleEvent(m_vsync_timer);
        // The CoreTiming callback may have entered on another worker before
        // UnscheduleEvent returned: do not close kernel events it can see.
        {
            const std::scoped_lock lock{m_vsync->mutex};
            m_vsync->events.clear();
        }
        for (auto* event : m_vsync_events) m_context.CloseEvent(event);
        if (m_poll) system.CoreTiming().UnscheduleEvent(m_poll);
        if (m_vi) {
            for (auto& c:m_clients)
                if (c.layer) m_vi->GetContainer()->DestroyManagedLayer(*c.layer);
            if (m_display) m_vi->CloseDisplay(*m_display);
        }
        m_context.CloseEvent(m_event);
    }
    Result HandleSyncRequest(Kernel::KServerSession& session,HLERequestContext& ctx) override {
#ifdef PS5_NATIVE
        if (ctx.IsTipc()) {
            static std::atomic<unsigned> calls{0};
            const auto n = calls.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 48 || (n & (n - 1)) == 0)
                std::fprintf(stderr, "EDEN_NS2_IPC owner=vi2 cmd=%u known=%u count=%u\n",
                             static_cast<unsigned>(ctx.GetCommand()),
                             static_cast<unsigned>(handlers_tipc.contains(ctx.GetCommand())), n);
        }
#endif
        if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand()))
            return ServiceFrameworkBase::HandleSyncRequest(session,ctx);
        const auto guard=LockService();
        InvokeRequestTipc(ctx);
        IPC::ResponseBuilder rb{ctx,2}; rb.Push(Kernel::ResultNotImplemented.raw);
        return system.IsPoweredOn()?ctx.WriteToOutgoingCommandBuffer():ResultSuccess;
    }
private:
    void Handshake(HLERequestContext& ctx) {
        // NS2 SDK VI2 initialization requires the exact incoming u64 token
        // to be echoed. The raw TIPC reply clears the request command buffer,
        // so preserve its words before constructing the reply.
        const auto* packet = ctx.CommandBuffer();
        const u32 token_lo = packet[2];
        const u32 token_hi = packet[3];
        NativeTipcReplyWords(ctx, {token_lo, token_hi});
    }
    static u64 W(const u32* p,size_t i) {return u64{p[i]}|(u64{p[i+1]}<<32);}
    Client* Find(u64 id) {
        if (!id) return nullptr;
        const auto it=std::ranges::find(m_clients,id,&Client::token);
        return it==m_clients.end()?nullptr:std::addressof(*it);
    }
    void Signal(u32 bit) {
        if (bit>=1024)return;
        m_notes[bit/32]|=1U<<(bit%32);
        if(m_event)m_event->Signal(kernel);
    }
    // Validate native TIPC request shape, display identity and live VI
    // session. A bare success would leave stale guest message-buffer words
    // as HDR or output-mode data, producing invalid GPU color constants.
    Result ValidateDisplayStatus(HLERequestContext& ctx, u32 command) const {
        const auto* packet = ctx.CommandBuffer();
        if (!GraphicsSupervisorProtocol::IsDefaultVi2DisplayQuery(
                packet[0], packet[1], W(packet, 2), command))
            return Kernel::ResultInvalidCombination;
        return m_display && m_vi ? ResultSuccess : Kernel::ResultInvalidState;
    }
    void AcknowledgeDisplayPoll(HLERequestContext& ctx) {
        // Only the request-group configure command 51 has no output payload.
        // Unknown commands still fail through the TIPC NotImplemented path.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess.raw);
    }
    void QueryRefreshRate(HLERequestContext& ctx) {
        const Result result = ValidateDisplayStatus(ctx, 103);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(result.raw);
        rb.Push<u32>(result.IsSuccess()
                          ? GraphicsSupervisorProtocol::Vi2NominalRefreshRateHz : 0);
        rb.Push<u32>(0); // SDK's refresh-rate capability boolean.
    }
    void QueryHdrStatus(HLERequestContext& ctx) {
        const Result result = ValidateDisplayStatus(ctx, 104);
        IPC::ResponseBuilder rb{ctx, 5};
        rb.Push(result.raw);
        rb.Push<u64>(result.IsSuccess()
                          ? GraphicsSupervisorProtocol::Vi2SdrOutputFormat : 0);
        rb.Push<u32>(result.IsSuccess()
                          ? GraphicsSupervisorProtocol::Vi2SdrPeakNits : 0);
    }
    void QueryPaperWhite(HLERequestContext& ctx) {
        const Result result = ValidateDisplayStatus(ctx, 105);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(result.raw);
        rb.Push<u32>(result.IsSuccess()
                          ? GraphicsSupervisorProtocol::Vi2SdrPaperWhiteNits : 0);
    }
    void Limit(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        const u32 command = ctx.GetCommand();
        const Result result = p[0] == 16 + command && p[1] == 0
            ? ResultSuccess : Kernel::ResultInvalidCombination;
        // The NS2 SDK probes display/notification capacity before any
        // client buffer can be submitted. Returning zero for commands 1/3
        // advertises a nonexistent display and can abort VI2 initialization.
        // Lichii's reviewed nx2:vi2 replies one for both, and seven for
        // the timestamp/request-group count (TIPC 5). Encore exposes one
        // real default VI display; never report capacity on invalid input.
        const u32 available = command == 5 ? 7u : 1u;
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(result.raw);
        rb.Push<u32>(result.IsSuccess() ? available : 0u);
    }
    void Event(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x1b&&p[1]==1&&p[2]==0){
            if(!m_event)m_event=m_context.CreateEvent("Vi2Notify");
            result=m_event?ResultSuccess:Kernel::ResultOutOfResource;
            if(m_event&&std::ranges::any_of(m_notes,[](u32 note){return note!=0;}))
                m_event->Signal(kernel);
        }
        // VI2 notification cmd 11, like vsync 31/32, returns a COPY
        // event handle. A moved handle changes the guest TIPC handle
        // descriptor and can strand the SDK notification waiter.
        if (result.IsSuccess()) {
            IPC::ResponseBuilder rb{ctx,2,1,0};
            rb.Push(result.raw);
            rb.PushCopyObjects(ctx,m_event->GetReadableEvent());
        } else {
            IPC::ResponseBuilder rb{ctx,2};
            rb.Push(result.raw);
        }
    }
    void VsyncEvent(HLERequestContext& ctx) {
        // 31(u32 slot) creates/returns an indexed native event. 32(u32 slot,
        // COPY handle) duplicates that SAME event, not another vblank source.
        // Guests can wait on its handle before the first GPU submission.
        const auto* packet = ctx.CommandBuffer();
        const u32 command = ctx.GetCommand();
        const auto slot = GraphicsSupervisorProtocol::DecodeVi2NativeEventSlot(
            std::span<const u32>{packet, IPC::COMMAND_BUFFER_LENGTH}, command);
        Result status = Kernel::ResultInvalidCombination;
        if (slot && command == 32 &&
            packet[3] != ctx.GetCopyHandle(0)) {
            status = Kernel::ResultInvalidHandle;
        } else if (slot) {
            status = ResultSuccess;
        }
        if (status.IsError()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(status.raw);
            return;
        }
        auto* event = m_native_events[*slot];
        if (event) {
            IPC::ResponseBuilder rb{ctx, 2, 1, 0};
            rb.Push(ResultSuccess.raw);
            rb.PushCopyObjects(ctx, event->GetReadableEvent());
            return;
        }
        // Reserve the owners and the callback list BEFORE creating a kernel
        // event. Neither registry can allocate after publishing the pointer.
        try {
            m_vsync_events.reserve(GraphicsSupervisorProtocol::RequestGroupCount);
            const std::scoped_lock lock{m_vsync->mutex};
            m_vsync->events.reserve(GraphicsSupervisorProtocol::RequestGroupCount);
        } catch (const std::bad_alloc&) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultOutOfResource.raw);
            return;
        }
        try {
            event = m_context.CreateEvent("NativeVi2Vsync");
        } catch (const std::bad_alloc&) {
            event = nullptr;
        }
        if (!event) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultOutOfResource.raw);
            return;
        }
        m_vsync_events.push_back(event);
        {
            const std::scoped_lock lock{m_vsync->mutex};
            m_vsync->events.push_back(event);
        }
        if (!m_vsync_timer) {
            try {
                auto state = m_vsync;
                auto& event_kernel = system.Kernel();
                auto new_timer = Core::Timing::CreateEvent(
                    "NativeVi2Vsync60Hz",
                    [state, &event_kernel](
                        s64, std::chrono::nanoseconds)
                        -> std::optional<std::chrono::nanoseconds> {
                        const std::scoped_lock lock{state->mutex};
                        for (auto* target : state->events)
                            target->Signal(event_kernel);
                        return std::nullopt;
                    });
                constexpr std::chrono::nanoseconds period{16'666'667};
                system.CoreTiming().ScheduleLoopingEvent(
                    period, period, new_timer);
                m_vsync_timer = std::move(new_timer);
            } catch (const std::bad_alloc&) {
                {
                    const std::scoped_lock lock{m_vsync->mutex};
                    m_vsync->events.pop_back();
                }
                m_vsync_events.pop_back();
                m_context.CloseEvent(event);
                IPC::ResponseBuilder rb{ctx, 2};
                rb.Push(Kernel::ResultOutOfResource.raw);
                return;
            }
        }
        // Assign the stable indexed identity after every fallible operation.
        m_native_events[*slot] = event;
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(ResultSuccess.raw);
        rb.PushCopyObjects(ctx, event->GetReadableEvent());
    }
    void ConfigureNativeDisplayEvent(HLERequestContext& ctx) {
        const u32 command = ctx.GetCommand();
        const auto request = GraphicsSupervisorProtocol::DecodeVi2DisplayEventBinding(
            std::span<const u32>{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH},
            command);
        Result result = Kernel::ResultInvalidCombination;
        if (request && request->display_name == Default &&
            request->value < m_native_events.size()) {
            auto* event = m_native_events[request->value];
            result = Kernel::ResultInvalidState;
            if (event) {
                {
                    // CoreTiming reads only this list; bind/unbind under the
                    // SAME lock as its callback and the VI2 session teardown.
                    const std::scoped_lock lock{m_vsync->mutex};
                    auto& active = m_vsync->events;
                    if (command == 261) {
                        if (std::find(active.begin(), active.end(), event) ==
                            active.end())
                            active.push_back(event); // reserved at event creation
                    } else {
                        std::erase(active, event);
                    }
                }
                // Display capabilities are static in Encore. Signal a
                // change binding exactly once so the SDK can re-query,
                // but do not turn refresh/HDR status into 60Hz events.
                if (command >= 263 && (command & 1))
                    event->Signal(kernel);
                result = ResultSuccess;
            }
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
    }
    void Notifications(HLERequestContext& ctx) {
        Result result=Kernel::ResultInvalidSize;
        if(ctx.GetWriteBufferSize()>=sizeof(m_notes)){
            ctx.WriteBuffer(m_notes);m_notes.fill(0);
            if(m_event)m_event->Clear(kernel);result=ResultSuccess;
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Open(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x75&&p[1]==3&&W(p,2)==Default&&p[4]==1){
            result=Kernel::ResultInvalidState;
            if(!m_display){
                auto root=system.ServiceManager().GetService<VI::IManagerRootService>("vi:m",true);
                if(root)result=root->GetDisplayService(&m_vi,VI::Policy::Compositor);
                if(result.IsSuccess()&&m_vi) {
                    u64 id{};result=m_vi->OpenDefaultDisplay(&id);
                    if(result.IsSuccess())m_display=id;
                }
            }else if(m_refs<1024)result=ResultSuccess;
            if(result.IsSuccess())++m_refs;
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Close(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x76&&p[1]==2&&W(p,2)==Default){
            result=Kernel::ResultNotFound;
            if(m_display&&m_refs){
                result=ResultSuccess;
                if(m_refs==1){
                    result=m_vi->CloseDisplay(*m_display);
                    if(result.IsSuccess())m_display.reset();
                }
                if(result.IsSuccess())--m_refs;
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Register(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        const u64 id=W(p,2);
        if(p[0]==0xd9&&p[1]==4&&id&&p[4]<32&&p[5]<32&&p[4]!=p[5]){
            const u32 slots=(1U<<p[4])|(1U<<p[5]);
            result=Kernel::ResultInvalidState;
            if(!Find(id)&&!(m_slots&slots)){
                result=Kernel::ResultOutOfResource;
                auto it=std::ranges::find(m_clients,u64{0},&Client::token);
                if(it!=m_clients.end()){
                    it->token=id;it->signals=slots;
                    it->release_bit=p[4];it->available_bit=p[5];
                    m_slots|=slots;result=ResultSuccess;
                }
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Unregister(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0xda&&p[1]==2){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c){
                Release();
                result=Kernel::ResultBusy;
                if(std::ranges::none_of(c->queued,[](auto x){return x.has_value();})){
                    if(c->layer&&m_vi)m_vi->GetContainer()->DestroyManagedLayer(*c->layer);
                    Signal(c->release_bit);
                    if(m_endpoint==c->token)m_endpoint.reset();
                    m_slots&=~c->signals;*c={};result=ResultSuccess;
                }
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Bind(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x10b&&p[1]==4&&W(p,4)==Default){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c){result=Kernel::ResultInvalidState;
                if(m_display&&m_refs&&!c->display){c->display=m_display;result=ResultSuccess;}}
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Unbind(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x10c&&p[1]==2){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c&&c->display){c->display.reset();result=ResultSuccess;}
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Availability(HLERequestContext& ctx) {
        Release();
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        u64 mask{};
        if(p[0]==0xe4&&p[1]==2){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c&&c->display&&m_vi){
                result=ResultSuccess;
                if(!c->layer){
                    u64 layer{};
                    result=m_vi->GetContainer()->CreateManagedLayer(&layer,*c->display,m_aruid);
                    if(result.IsSuccess())c->layer=layer;
                }
                if(result.IsSuccess())mask=c->available;
            }
        }
        IPC::ResponseBuilder rb{ctx,4};rb.Push(result.raw);rb.Push(mask);
    }
    void OutputLevels(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x170&&p[1]==5){
            const auto client=Find(W(p,2));
            result=Kernel::ResultNotFound;
            if(client){
                std::array<float,3> values{};
                std::memcpy(values.data(),p+4,sizeof(values));
                result=Kernel::ResultInvalidCombination;
                if(std::ranges::all_of(values,[](float v){return std::isfinite(v);})&&
                    values[0]>=0.0f&&values[0]<=1.0f&&
                    values[1]>=0.0f&&values[1]<=1.0f&&values[2]>=0.0f){
                    client->output_levels=values;
                    client->output_levels_set=true;
                    result=ResultSuccess;
                }
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void OpenEndpoint(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x7f&&p[1]==6&&W(p,2)==Default&&W(p,4)==Default){
            const auto client=Find(W(p,6));
            result=Kernel::ResultNotFound;
            if(client&&client->display==m_display&&m_display){
                result=Kernel::ResultInvalidState;
                if(!m_endpoint){m_endpoint=client->token;result=ResultSuccess;}
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void CloseEndpoint(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x80&&p[1]==4&&W(p,2)==Default&&W(p,4)==Default){
            result=Kernel::ResultNotFound;
            if(m_endpoint){m_endpoint.reset();result=ResultSuccess;}
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void MaxResolution(HLERequestContext& ctx) {
        // The SDK asks for the emulated Switch 2 display's maximum buffer
        // tier, not the PS5 output resolution. A hard-coded 720p mode made
        // 1080p/1440p guests silently select a reduced fallback pipeline.
        const auto* p = ctx.CommandBuffer();
        Result result = ValidateDisplayStatus(ctx, 106);
        u64 mode{};
        if (result.IsSuccess()) {
            s64 width{}, height{};
            result = m_vi->GetDisplayResolution(&width, &height, *m_display);
            if (result.IsSuccess()) {
                if (width > 0 && height > 0)
                    mode = GraphicsSupervisorProtocol::Vi2MaxResolutionMode(
                        Settings::IsDockedMode());
                else
                    result = Kernel::ResultNotImplemented;
            }
        }
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(result.raw);
        rb.Push(mode);
    }
    void Encoding(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x16f&&p[1]==4){
            result=Find(W(p,2))?Kernel::ResultNotImplemented:Kernel::ResultNotFound;
            if(result==Kernel::ResultNotImplemented&&W(p,4)==0x14)result=ResultSuccess;
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Release() {
        for (auto& c : m_clients) {
            if (!c.producer) continue;
            // This Eden Nvnflinger version exposes DetachNextBuffer, not
            // upstream NS2's GetReleasedBufferFence/DetachReleasedBuffer.
            // Only Free buffers can be detached by that API.
            for (u32 tries=0;tries<c.queued.size();++tries) {
                std::shared_ptr<android::GraphicBuffer> buffer;
                android::Fence release_fence{};
                if (c.producer->DetachNextBuffer(&buffer,&release_fence)!=
                    android::Status::NoError) break;
                for (u32 slot=0;slot<c.queued.size();++slot) {
                    if (c.retained[slot] && c.retained[slot]==buffer) {
                        c.release_fences[slot]=release_fence;
                        break;
                    }
                }
            }
            for (u32 slot=0;slot<c.queued.size();++slot) {
                if (!c.queued[slot] || !c.release_fences[slot]) continue;
                auto module=c.queued_modules[slot];
                if (!module) continue;
                const auto& fence=*c.release_fences[slot];
                // A producer fence has four fixed syncpoint entries.
                // Reject an invalid reported count before indexing them;
                // retaining the queued image is safer than releasing its
                // NVMap backing while GPU work might still be outstanding.
                // Service::android::Fence::num_fences is u32, not signed:
                // testing < 0 was always false and emitted a native compiler
                // warning. Keep the original fail-closed release policy:
                // retain the NVMap image until its entire reported fence
                // array is known to fit and every syncpoint has expired.
                const std::size_t fence_count = fence.num_fences;
                if (fence_count > fence.fences.size()) continue;
                bool done=true;
                for (std::size_t i=0;i<fence_count;++i) {
                    const auto& point=fence.fences[i];
                    if (point.id == -1) continue;
                    auto& syncpoints=module->GetContainer().GetSyncpointManager();
                    if (!syncpoints.IsSyncpointAllocated(point.id) ||
                        !syncpoints.HasSyncpointExpired(point.id,point.value)) {
                        done=false;break;
                    }
                }
                if (!done) continue;
                c.queued[slot].reset();
                c.images[slot]=0;
                c.queued_modules[slot].reset();
                c.release_fences[slot].reset();
                c.retained[slot].reset();
                c.available|=u64{1}<<slot;
                Signal(c.available_bit);
            }
        }
    }
    void Present(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        u64 mask{};
        if(p[0]==0xf5&&p[1]==24){
            Release();
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c){
                const u32 slot=GraphicsSupervisorProtocol::DecodeBufferSlot(p[4]);
                result=Kernel::ResultInvalidState;
                if(slot<15&&c->display&&c->layer&&!c->queued[slot]){
                    const auto img=m_owner->GetPresentImage(p[5]);
                    result=Kernel::ResultNotFound;
                    if(img){
                        const auto& pref=img->layout;
                        const u32 width=(pref[2]&0xffffU)+1;
                        const u32 height=(pref[2]>>16)+1;
                        const u32 outw=(p[7]&0xffffU)+1;
                        const u32 outh=(p[7]>>16)+1;
                        result=Kernel::ResultNotImplemented;
                        if(p[6]==0&&p[8]==3&&pref[4]==7&&
                            pref[6]==1&&outw<=width&&outh<=height){
                            const u32 fence=~p[22];
                            auto& nv=img->module->GetContainer();
                            result=Kernel::ResultInvalidCombination;
                            if(p[23]==0&&p[25]==0&&
                                (fence==~u32{0}||
                                 nv.GetSyncpointManager().IsSyncpointAllocated(fence))){
                                if(!c->producer){
                                    result=m_vi->GetContainer()->GetLayerProducerHandle(
                                        &c->producer,*c->layer);
                                    if(result.IsSuccess()){
                                        android::QueueBufferOutput output{};
                                        if(c->producer->Connect({},
                                            android::NativeWindowApi::Egl,false,&output)!=
                                            android::Status::NoError||
                                           c->producer->SetBufferCount(16)!=
                                            android::Status::NoError)
                                            result=Kernel::ResultInvalidState;
                                    }
                                    if(result.IsSuccess()){
                                        s32 binder{};
                                        result=m_vi->GetContainer()->OpenLayer(
                                            &binder,*c->layer,m_aruid);
                                        if(result.IsSuccess())result=m_vi->GetContainer()
                                            ->SetLayerVisibility(*c->layer,true);
                                    }
                                }else result=ResultSuccess;
                                // Release polling must be ready BEFORE
                                // QueueBuffer publishes its producer slot.
                                // If CreateEvent/Schedule throws afterwards,
                                // a frame is live with no retirement path and
                                // the TIPC caller never sees its accepted queue.
                                if(result.IsSuccess() && !m_poll){
                                    try {
                                        auto weak=std::weak_ptr<IVi2Session>(
                                            std::static_pointer_cast<IVi2Session>(
                                                shared_from_this()));
                                        auto timer=Core::Timing::CreateEvent(
                                            "NativeVi2Release",
                                            [weak](s64,std::chrono::nanoseconds)
                                                ->std::optional<std::chrono::nanoseconds>{
                                                if(auto self=weak.lock()){
                                                    const auto guard=self->LockService();
                                                    self->Release();
                                                }
                                                return std::nullopt;
                                            });
                                        system.CoreTiming().ScheduleLoopingEvent(
                                            std::chrono::milliseconds{1},
                                            std::chrono::milliseconds{1},timer);
                                        m_poll=std::move(timer);
                                    } catch (const std::bad_alloc&) {
                                        result=Kernel::ResultOutOfResource;
                                    }
                                }
                                if(result.IsSuccess()){
                                    auto native=std::make_shared<android::NvGraphicBuffer>();
                                    native->width=static_cast<s32>(width);
                                    native->height=static_cast<s32>(height);
                                    native->stride=static_cast<s32>(pref[5]*16);
                                    native->format=android::PixelFormat::Rgba8888;
                                    native->external_format=native->format;
                                    native->buffer_id=img->buffer_id;native->offset=img->offset;
                                    auto buffer=std::make_shared<android::GraphicBuffer>(
                                        nv.GetNvMapFile(),native);
                                    s32 idx{};result=Kernel::ResultOutOfResource;
                                    if(c->producer->AttachBuffer(&idx,buffer)==android::Status::NoError){
                                        android::QueueBufferInput input{};
                                        android::QueueBufferOutput output{};
                                        input.crop={0,0,static_cast<s32>(outw),
                                            static_cast<s32>(outh)};
                                        input.swap_interval=static_cast<s32>(GraphicsSupervisorProtocol::DecodePresentSwapInterval(p[12]));
                                        input.fence=android::Fence::NoFence();
                                        if(fence!=~u32{0}){
                                            input.fence.num_fences=1;
                                            input.fence.fences[0]={static_cast<s32>(fence),p[24]};
                                        }
                                        if(c->producer->QueueBuffer(idx,input,&output)==
                                            android::Status::NoError){
                                            c->queued[slot]=idx;c->images[slot]=p[5];
                                            c->queued_modules[slot]=img->module;
                                            c->retained[slot]=buffer;
                                            c->available&=~(u64{1}<<slot);result=ResultSuccess;
                                        }else{
                                            c->producer->CancelBuffer(idx,input.fence);
                                            result=Kernel::ResultInvalidState;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                mask=c->available;
            }
        }
        IPC::ResponseBuilder rb{ctx,8};rb.Push(result.raw);rb.Push(mask);
        for(u32 i=0;i<4;++i)rb.Push<u32>(0);
    }
    void Cancel(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        u64 mask{};
        if(p[0]==0xf4&&p[1]==19){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c){
                const u32 slot=GraphicsSupervisorProtocol::DecodeBufferSlot(p[4]);
                result=Kernel::ResultInvalidState;
                if(slot<15&&!c->queued[slot]){c->available|=u64{1}<<slot;result=ResultSuccess;}
                mask=c->available;
            }
        }
        IPC::ResponseBuilder rb{ctx,8};rb.Push(result.raw);rb.Push(mask);
        for(u32 i=0;i<4;++i)rb.Push<u32>(0);
    }
    std::shared_ptr<IGraphicsSupervisor> m_owner;
    u64 m_aruid{};
    std::array<Client,16> m_clients{};
    u32 m_slots{};
    std::shared_ptr<VI::IApplicationDisplayService> m_vi;
    std::optional<u64> m_display;
    std::optional<u64> m_endpoint;
    u32 m_refs{};
    std::array<u32,32> m_notes{};
    KernelHelpers::ServiceContext m_context;
    Kernel::KEvent* m_event{};
    struct VsyncState {
        std::mutex mutex;
        std::vector<Kernel::KEvent*> events;
    };
    std::shared_ptr<VsyncState> m_vsync = std::make_shared<VsyncState>();
    std::vector<Kernel::KEvent*> m_vsync_events;
    std::array<Kernel::KEvent*, GraphicsSupervisorProtocol::RequestGroupCount>
        m_native_events{};
    std::shared_ptr<Core::Timing::EventType> m_vsync_timer;
    std::shared_ptr<Core::Timing::EventType> m_poll;
};


class INativeChatGsvSession final : public ServiceFramework<INativeChatGsvSession> {
public:
    explicit INativeChatGsvSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:chat:gsv"},
          m_context{system_, "nx2:chat:gsv"} {
        static const FunctionInfo functions[] = {
            {1, &INativeChatGsvSession::GetEventId, "GetEventId"},
            {11, &INativeChatGsvSession::GetEvent, "GetEvent"},
            {103, &INativeChatGsvSession::OpenVirtualDisplay, "OpenVirtualDisplay"},
        };
        RegisterHandlersTipc(functions);
    }

    ~INativeChatGsvSession() override {
        for (auto& [_, event] : m_events) {
            if (event) m_context.CloseEvent(event);
        }
    }

private:
    void GetEventId(HLERequestContext& ctx) {
        // SDK aborts when this exceeds 0x3ff. One idle event is sufficient
        // while Encore has no active GameChat room.
        NativeTipcReplyWords(ctx, {0});
    }

    void GetEvent(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2];
        if (id > 0x3ff) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidArgument.raw);
            return;
        }
        auto*& event = m_events[id];
        if (!event) {
            char name[48]{};
            std::snprintf(name, sizeof(name), "nx2:chat:gsv:Event%u", id);
            try {
                event = m_context.CreateEvent(name);
            } catch (const std::bad_alloc&) {
                IPC::ResponseBuilder rb{ctx, 2};
                rb.Push(Kernel::ResultOutOfResource.raw);
                return;
            }
        }
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(ResultSuccess.raw);
        rb.PushCopyObjects(ctx, event->GetReadableEvent());
    }

    void OpenVirtualDisplay(HLERequestContext& ctx) {
        NativeTipcReplyWords(ctx, {});
    }

    KernelHelpers::ServiceContext m_context;
    std::map<u32, Kernel::KEvent*> m_events;
};

class INativeCameraSession final : public ServiceFramework<INativeCameraSession> {
public:
    explicit INativeCameraSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:camera"},
          m_context{system_, "nx2:camera"},
          m_event{m_context.CreateEvent("nx2:camera:Event")} {
        static const FunctionInfo functions[] = {
            {3, &INativeCameraSession::OpenCamera, "OpenCamera"},
            {21, &INativeCameraSession::GetCameraEvent, "GetCameraEvent"},
            {29, &INativeCameraSession::CheckImagePreference, "CheckImagePreference"},
        };
        RegisterHandlers(functions);
    }

    ~INativeCameraSession() override {
        if (m_event) m_context.CloseEvent(m_event);
    }

private:
    void OpenCamera(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(0);
    }

    void GetCameraEvent(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, m_event->GetReadableEvent());
    }

    void CheckImagePreference(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    KernelHelpers::ServiceContext m_context;
    Kernel::KEvent* m_event{};
};

IGraphicsSupervisor::IGraphicsSupervisor(Core::System& system_, Kernel::KProcess* process,
                                         u64 aruid)
    : ServiceFramework{system_, "IGraphicsSupervisor"}, m_process{process},
      m_bootstrap{aruid}, m_aruid{aruid},
      m_syncpt{std::make_shared<NativeSyncptState>(system_, process)} {
    m_process->Open(kernel);
    static const FunctionInfo functions[] = {
        {1, &IGraphicsSupervisor::Initialize, "InitializeWithTransferMemory"},
        {4, &IGraphicsSupervisor::Initialize, "Initialize"},
        {11, &IGraphicsSupervisor::BindProcess, "BindProcess"},
        {101, &IGraphicsSupervisor::CompleteBootstrap, "CompleteBootstrap"},
        {201, &IGraphicsSupervisor::CreateSyncptContext, "CreateSyncptContext"},
        {202, &IGraphicsSupervisor::OpenSyncptSession, "OpenSyncptSession"},
        {204, &IGraphicsSupervisor::AttachSyncptMemory, "AttachSyncptMemory"},
        {401, &IGraphicsSupervisor::InitializeGpuContext, "InitializeGpuContext"},
        {402, &IGraphicsSupervisor::InitializeGpuContext, "ActivateGpuContext"},
        {403, &IGraphicsSupervisor::DestroyGpuContext, "DestroyGpuContext"},
        {434, &IGraphicsSupervisor::RegisterGpuAddressSpace, "RegisterGpuAddressSpace"},
        {496, &IGraphicsSupervisor::OpenGpuSession, "OpenGpuSessionWithTransferMemory"},
        {497, &IGraphicsSupervisor::OpenGpuSession, "OpenGpuSession"},
        {498, &IGraphicsSupervisor::QueryDonatedTransferMemoryUsage, "TryQueryDonatedTransferMemoryUsage"},
        {1401, &IGraphicsSupervisor::OpenNvsrvSession, "OpenNvsrvSessionWithTransferMemory"},
        {1402, &IGraphicsSupervisor::OpenNvsrvSession, "OpenNvsrvSession"},
        // Known TIPC contracts without a safe completed implementation:
        // 1411 exposes NVDRV transfer-memory accounting, not GSV boot-buffer
        // accounting (498); do not manufacture three zero usage counters.
        {1411, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetNvDrv2UserTransferMemoryUsage"},
        // 1501/1502 initialize inter-module image sharing. Until image
        // ownership and backing semantics are available, fail closed.
        {1501, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CreateInterModuleImageContext"},
        {1502, &IGraphicsSupervisor::UnavailableDocumentedFeature, "InitializeInterModuleImageContext"},
        {301, &IGraphicsSupervisor::InitializeDisplayContext, "InitializeDisplayContext"},
        {303, &IGraphicsSupervisor::FinalizeDisplayContext, "FinalizeDisplayContext"},
        {305, &IGraphicsSupervisor::OpenDisplaySession, "OpenDisplaySession"},
        {111, &IGraphicsSupervisor::CreateNativeMemory, "CreateNativeMemory"},
        {112, &IGraphicsSupervisor::DestroyNativeMemory, "DestroyNativeMemory"},
        {113, &IGraphicsSupervisor::BindNativeTransferMemory, "CreateNativeMemoryWithTransferMemory"},
        {114, &IGraphicsSupervisor::BindNativeMemory, "BindNativeMemory"},
        {115, &IGraphicsSupervisor::BootNativeMemoryManagement, "BootNativeMemoryManagement"},
        {423, &IGraphicsSupervisor::BindNativeGpuMemory, "BindNativeGpuMemory"},
        {429, &IGraphicsSupervisor::CreateNativeImage, "CreateNativeImage"},
        {430, &IGraphicsSupervisor::DestroyNativeImage, "DestroyNativeImage"},
        {314, &IGraphicsSupervisor::ConfigureNativeWindowImagePool, "DestroyNativeWindowAliasImagePool"},
        {315, &IGraphicsSupervisor::ConfigureNativeWindowImagePool, "CreateNativeWindowAliasImagePool"},
        // SwitchBrew 391 is the 16-word image preference + count size/alignment query.
        // Reuse the validated native layout calculator; unsupported formats fail closed.
        {391, &IGraphicsSupervisor::CalculateNativeImageRequirements, "CalculateRequiredBufferMemory"},
        {901, &IGraphicsSupervisor::InitializeVideoCodecContext, "InitializeVideoDecoderContext"},
        {1001, &IGraphicsSupervisor::UnavailableVideoEncoder, "CreateVideoEncoderContext"},
        {902, &IGraphicsSupervisor::OpenVideoDecoderSession, "OpenVideoDecoderSession"},
        {905, &IGraphicsSupervisor::OpenVideoDecoderSession, "OpenVideoDecoderSessionWithFlag"},
        {906, &IGraphicsSupervisor::OpenVideoDecoderSession, "OpenVideoDecoderSessionWithVersion"},
        {1002, &IGraphicsSupervisor::UnavailableVideoEncoder, "InitializeVideoEncoderContext"},
        {601, &IGraphicsSupervisor::InitializeFeatureContext, "InitializeCameraContext"},
        {602, &IGraphicsSupervisor::OpenCameraSession, "OpenCameraContext"},
        {1201, &IGraphicsSupervisor::InitializeFeatureContext, "InitializeChatContext"},
        {1202, &IGraphicsSupervisor::OpenChatSession, "OpenChatSession"},
        {991, &IGraphicsSupervisor::CalculateDecoderImagePoolBuffer, "CalculateDecoderImagePoolBuffer"},
        {911, &IGraphicsSupervisor::CreateVideoImagePool, "CreateVideoImagePool"},
        {1211, &IGraphicsSupervisor::CreateVideoImagePool, "CreateChatImagePool"},
        {912, &IGraphicsSupervisor::DestroyVideoImagePool, "DestroyVideoImagePool"},
        {1212, &IGraphicsSupervisor::DestroyVideoImagePool, "DestroyChatImagePool"},
        {961, &IGraphicsSupervisor::GetCpuVideoImageDescriptor, "GetCpuUserImageDescriptor"},
        {426, &IGraphicsSupervisor::MapVideoGpuImage, "MapNativeGpuImage"},
        {427, &IGraphicsSupervisor::UnmapVideoGpuImage, "UnmapNativeGpuImage"},
        {428, &IGraphicsSupervisor::QueryVideoGpuImage, "QueryNativeGpuImage"},
        {435, &IGraphicsSupervisor::CalculateNativeImageSize, "CalculateNativeImageSize"},
        {1291, &IGraphicsSupervisor::CalculateChatImagePoolSize, "CalculateChatImagePoolSize"},
        {1101, &IGraphicsSupervisor::ConfigureGraphicsFrameContext, "CreateGraphicsFrameContext"},
        {1102, &IGraphicsSupervisor::ConfigureGraphicsFrameContext, "InitializeGraphicsFrameContext"},
        {1111, &IGraphicsSupervisor::SetGraphicsFrameMode, "SetGraphicsFrameLengthMode"},
        {1112, &IGraphicsSupervisor::SetGraphicsFrameMode, "SetGraphicsFrameRestartMode"},
        {1113, &IGraphicsSupervisor::SetGraphicsFrameMode, "SetGraphicsFrameGpuTimesliceMode"},
        {1510, &IGraphicsSupervisor::CalculateNativeImageRequirements, "CalculateRequirements"},
        // Known optional SwitchBrew commands without a supported Encore backend.
        // Each ID has an explicit fail-closed TIPC handler; never fake Success.
        {187, &IGraphicsSupervisor::UnavailableDocumentedFeature, "QueryNativeMemoryNvsNvRmMemIdForDebug"},
        {211, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CreateDebugSyncDevice"},
        {212, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyDebugSyncDevice"},
        {213, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CreateDebugSyncCounter"},
        {214, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyDebugSyncCounter"},
        {215, &IGraphicsSupervisor::UnavailableDocumentedFeature, "ReportDebugSyncCounterValue"},
        {493, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CreateUserlandDebuggerSession"},
        {603, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyCameraContext"},
        {612, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyOutputBufferPool"},
        {613, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CreateAliasOutputBufferPool"},
        {614, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyAliasOutputBufferPool"},
        {615, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CreateOutputBufferPool"},
        {616, &IGraphicsSupervisor::UnavailableDocumentedFeature, "ReadFrameDataAsync"},
        {620, &IGraphicsSupervisor::UnavailableDocumentedFeature, "InitializeB"},
        {621, &IGraphicsSupervisor::UnavailableDocumentedFeature, "FinalizeB"},
        {622, &IGraphicsSupervisor::UnavailableDocumentedFeature, "ListCameras"},
        {623, &IGraphicsSupervisor::UnavailableDocumentedFeature, "OpenCamera"},
        {624, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CloseCamera"},
        {626, &IGraphicsSupervisor::UnavailableDocumentedFeature, "ListSensorMode"},
        {627, &IGraphicsSupervisor::UnavailableDocumentedFeature, "SetSensorMode"},
        {628, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetAvailableStreams"},
        {629, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetRequiredWorkBufferSize"},
        {630, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CreateStream"},
        {632, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetReadDoneEvent"},
        {633, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetStreamOutputSize"},
        {634, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyStream"},
        {636, &IGraphicsSupervisor::UnavailableDocumentedFeature, "ReadFrameDataAsync"},
        {637, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetAttachedEvent"},
        {638, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetDetachedEvent"},
        {639, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetFrameInfo"},
        {640, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetFrameInfo2"},
        {641, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetConnectionChangedEvent"},
        {650, &IGraphicsSupervisor::UnavailableDocumentedFeature, "SetPictureQualityConfig"},
        {651, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetPictureQualityConfig"},
        {652, &IGraphicsSupervisor::UnavailableDocumentedFeature, "SetDigitalWindow"},
        {653, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetDigitalWindow"},
        {654, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetDigitalWindowCapability"},
        {655, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetSupportedFunctions"},
        {656, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetSupportedDebugFunctions"},
        {657, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetCameraPrivacyCoverStatus"},
        {658, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetCameraDeviceTemperature"},
        {659, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetDeviceUniqueId"},
        {660, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetSensorMode"},
        {661, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetStride"},
        {662, &IGraphicsSupervisor::UnavailableDocumentedFeature, "UnlinkSensor"},
        {663, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetDeviceModel"},
        {664, &IGraphicsSupervisor::UnavailableDocumentedFeature, "SetRawSensorMode"},
        {665, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetAccessAllowed"},
        {666, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetHostDrivenAeRegion"},
        {667, &IGraphicsSupervisor::UnavailableDocumentedFeature, "SetHostDrivenAeRegion"},
        // Camera 691 shares the image-pool requirements query layout; Encore supports only
        // proven chat-camera image formats, and rejects unknown layouts.
        {691, &IGraphicsSupervisor::CalculateChatImagePoolSize, "CalculateImageArraySize"},
        {903, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyVideoDecoderContext"},
        {904, &IGraphicsSupervisor::UnavailableDocumentedFeature, "InitializeVideoDecoderContextForSystem"},
        {921, &IGraphicsSupervisor::UnavailableDocumentedFeature, "InitializeDecoderAliasImagePool"},
        {922, &IGraphicsSupervisor::UnavailableDocumentedFeature, "FinalizeDecoderAliasImagePool"},
        {1003, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyVideoEncoderContext"},
        {1004, &IGraphicsSupervisor::UnavailableDocumentedFeature, "InitializeVideoEncoderContextForSystem"},
        {1011, &IGraphicsSupervisor::UnavailableDocumentedFeature, "InitializeEncoderImagePool"},
        {1012, &IGraphicsSupervisor::UnavailableDocumentedFeature, "FinalizeEncoderImagePool"},
        {1021, &IGraphicsSupervisor::UnavailableDocumentedFeature, "InitializeEncoderAliasImagePool"},
        {1022, &IGraphicsSupervisor::UnavailableDocumentedFeature, "FinalizeEncoderAliasImagePool"},
        {1203, &IGraphicsSupervisor::UnavailableDocumentedFeature, "DestroyChatContext"},
        {1301, &IGraphicsSupervisor::UnavailableDocumentedFeature, "CreateAdrawContext"},
        {1302, &IGraphicsSupervisor::UnavailableDocumentedFeature, "InitializeAdrawContext"},
        {1325, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetAdrawFrameBufferHandle"},
        {1326, &IGraphicsSupervisor::UnavailableDocumentedFeature, "GetAdrawImageHandle"},
    };
    RegisterHandlersTipc(functions);
}
IGraphicsSupervisor::~IGraphicsSupervisor() {
    if (m_syncpt) m_syncpt->Shutdown();
    m_syncpt.reset();
    // Video GMMU mappings and their NVMap pins can outlive the last
    // supervisor reference. Retire prior GPU readers BEFORE the native
    // memory mutex and before any of those pins are dropped. During system
    // shutdown the GPU worker has already been stopped.
    if (m_native_module && system.IsPoweredOn() && !system.IsShuttingDown())
        system.GPU().WaitForIdle();
    // Release the real NVMap backing and page-table locks before the native
    // NVDRV session and the application KProcess lose their final owner.
    const std::scoped_lock lock{m_native_memory_mutex};
    if (m_native_module) {
        auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
        // The GMMU VA owner outlives the mapping until it is retired.
        for (const auto& [key, mapping] : m_video_mappings) {
            if (mapping.address_space)
                mapping.address_space->UnmapNative4K(mapping.address, mapping.size);
        }
        m_video_mappings.clear();
        for (const auto& [first, pool] : m_video_pools.Records()) {
            nvmap.UnpinHandle(pool.backing);
            nvmap.FreeHandle(pool.backing, true);
        }
        // Retire internal image duplicates before their underlying NVMap
        // allocations; otherwise the backing remains busy at shutdown.
        for (const auto& [image_id, image] : m_native_images.Records())
            nvmap.FreeHandle(image.backing, true);
        // Release unborrowed children before dropping their parent's pin.
        // Retry parents after child release: a parent may itself be an owned
        // native-memory allocation that was busy on the first pass.
        std::vector<u32> freed;
        const auto release = [&](u32 id) {
            auto handle = nvmap.GetHandle(id);
            if (!handle) return;
            {
                const std::scoped_lock handle_lock{handle->mutex};
                if (handle->dupes != 1 || handle->internal_dupes != 0 ||
                    handle->pins != 0) return;
            }
            handle.reset();
            const auto info = nvmap.FreeHandle(id, false);
            if (!info || !info->can_unlock) return;
            freed.push_back(id);
            if (const auto transfer = m_native_transfer_backings.find(id);
                transfer != m_native_transfer_backings.end()) {
                // The TransferMemory object, not DeviceShared, owns page
                // stability for TIPC 113. Retire NVMap first, then release
                // the retained kernel object so its own lock can unwind.
                transfer->second->Close(kernel);
                m_native_transfer_backings.erase(transfer);
            } else if (info->address != 0) {
                const auto status = m_process->GetPageTable().UnlockForDeviceAddressSpace(
                    info->address, info->size);
                if (status.IsError())
                    LOG_ERROR(Service_AM, "NS2 native supervisor teardown unlock failed: {:#x}",
                              status.raw);
            }
        };
        const auto ids = m_native_memory.OwnedIds();
        for (const u32 id : ids) release(id);
        for (const auto& [child, parent] : m_native_parents) {
            if (std::find(freed.begin(), freed.end(), child) == freed.end()) {
                // A live external child retains its parent pin. Never revoke
                // another GPU owner just to make shutdown appear successful.
                continue;
            }
            nvmap.UnpinHandle(parent);
            nvmap.FreeHandle(parent, true);
        }
        for (const u32 id : ids) {
            if (std::find(freed.begin(), freed.end(), id) == freed.end())
                release(id);
        }
        if (m_native_memory_session)
            m_native_module->GetContainer().CloseSession(
                Nvidia::NvCore::SessionId{*m_native_memory_session});
    }
    // If an externally referenced NVMap could not be retired above, keep its
    // TransferMemory ref deliberately open. Closing it here could unlock
    // physical pages while that external GPU owner still references them.
    for (const auto& [id, transfer] : m_native_transfer_backings) {
        (void)transfer;
        LOG_ERROR(Service_AM,
                  "NS2 native TransferMemory backing quarantined at shutdown: {}", id);
    }
    m_native_transfer_backings.clear();
    if (m_bootstrap_transfer_memory) {
        m_bootstrap_transfer_memory->Close(kernel);
        m_bootstrap_transfer_memory = nullptr;
        m_bootstrap_transfer_memory_size = 0;
    }
    m_process->Close(kernel);
}
Result IGraphicsSupervisor::HandleSyncRequest(Kernel::KServerSession& session,
                                               HLERequestContext& ctx) {
#ifdef PS5_NATIVE
    if (ctx.IsTipc()) {
        static std::atomic<unsigned> calls{0};
        const auto n = calls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 48 || (n & (n - 1)) == 0)
            std::fprintf(stderr, "EDEN_NS2_IPC owner=graphics_supervisor cmd=%u known=%u count=%u\n",
                         static_cast<unsigned>(ctx.GetCommand()),
                         static_cast<unsigned>(handlers_tipc.contains(ctx.GetCommand())), n);
    }
#endif
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
    bool matches = false;
    Kernel::KTransferMemory* donated_transfer{};
    u64 donated_size{};
    Result result = Kernel::ResultInvalidCombination;
    if (ctx.GetCommand() == 1) {
        const auto request = GraphicsSupervisorProtocol::DecodeBootstrapTransferMemory(packet);
        if (request) {
            auto memory = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(
                ctx.GetCopyHandle(0));
            matches = !memory.IsNull() && memory->GetOwner() == m_process &&
                      request->copied_handle == ctx.GetCopyHandle(0);
            // Never admit a work-buffer allocation exceeding its real
            // KTransferMemory backing; a malformed owner is not recoverable
            // by pretending to have allocated GPU pages.
            if (matches && request->size > memory->GetSize()) {
                IPC::ResponseBuilder rb{ctx, 2};
                rb.Push(Kernel::ResultInvalidSize.raw);
                return;
            }
            if (matches) {
                donated_transfer = memory.GetPointerUnsafe();
                donated_size = request->size;
            }
        }
        result = TranslateBootstrap(m_bootstrap.InitializeTransferMemory(packet, matches));
        if (result.IsSuccess() && donated_transfer) {
            // GSV owns the donated work buffer for the session lifetime.
            // Retain the kernel object even though Encore's HLE backend does
            // not suballocate storage from it.
            donated_transfer->Open(kernel);
            m_bootstrap_transfer_memory = donated_transfer;
            m_bootstrap_transfer_memory_size = donated_size;
        }
    } else {
        const auto request = GraphicsSupervisorProtocol::DecodeBootstrap(packet);
        if (request) {
            const auto process = ctx.GetObjectFromHandle<Kernel::KProcess>(
                request->process_handle);
            matches = !process.IsNull() && process.GetPointerUnsafe() == m_process;
        }
        result = TranslateBootstrap(m_bootstrap.Initialize(packet, matches));
    }
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

void IGraphicsSupervisor::DestroyGpuContext(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    // The kernel session and native GPU memory have independent owners.
    // The context may be retired only once those owners have released
    // their real GMMU mappings, NVMap pins and worker handles.
    const std::scoped_lock lock{m_native_memory_mutex};
    std::erase_if(m_gpu_sessions, [](const auto& weak) { return weak.expired(); });
    const bool resources_released =
        m_gpu_sessions.empty() && m_nvsrv_session.expired() &&
        m_native_memory.Count() == 0 && m_native_images.Count() == 0 &&
        m_video_pools.ImageCount() == 0 && m_video_mappings.empty() &&
        m_native_parents.empty() && !m_native_memory_session.has_value();
    const Result result = TranslateBootstrap(
        m_bootstrap.DestroyGpuContext(packet, resources_released));
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

std::optional<IGraphicsSupervisor::PresentImage>
IGraphicsSupervisor::GetPresentImage(u32 id) {
    const std::scoped_lock lock{m_native_memory_mutex};
    const auto image=m_native_images.Get(id);
    if(!image||!image->window_pool||!m_native_module)return {};
    auto& container=m_native_module->GetContainer();
    const auto handle=container.GetNvMapFile().GetHandle(image->backing);
    if(!handle||!handle->allocated)return {};
    const auto* session=container.GetSession(handle->session_id);
    if(!session||session->process!=m_process)return {};
    u32 backing=image->backing,offset{};
    if(const auto it=m_native_parents.find(backing);it!=m_native_parents.end()){
        const auto parent=container.GetNvMapFile().GetHandle(it->second);
        if(!parent||!parent->allocated||handle->address<parent->address||
            handle->address-parent->address>std::numeric_limits<u32>::max())return {};
        offset=static_cast<u32>(handle->address-parent->address);
        backing=it->second;
    }
    const auto backing_handle=container.GetNvMapFile().GetHandle(backing);
    if(!backing_handle||!backing_handle->allocated ||
        offset>backing_handle->orig_size ||
        image->size>backing_handle->orig_size-offset)
        return {};
    const auto* backing_session=container.GetSession(backing_handle->session_id);
    if (!backing_session || backing_session->process != m_process)
        return {};
    // Allocate the handle token BEFORE acquiring a new NVMap duplicate.
    // Otherwise a bad_alloc here leaks the internal duplicate. The
    // shared_ptr(raw, deleter) constructor invokes the deleter if its
    // control-block allocation fails after the successful duplication.
    std::unique_ptr<u32> lease_token;
    try {
        lease_token = std::make_unique<u32>(backing);
    } catch (const std::bad_alloc&) {
        return {};
    }
    if (backing_handle->Duplicate(true) != Nvidia::NvResult::Success)
        return {};
    try {
        auto lease=std::shared_ptr<u32>(lease_token.release(),
            [module=m_native_module](u32* id){
                module->GetContainer().GetNvMapFile().FreeHandle(*id,true);
                delete id;
            });
        return PresentImage{image->preference,backing,offset,m_native_module,std::move(lease)};
    } catch (const std::bad_alloc&) {
        // shared_ptr invoked our NVMap deleter if its allocation failed.
        // Return an absent present image rather than unwinding through IPC.
        return {};
    }
}
void IGraphicsSupervisor::InitializeDisplayContext(HLERequestContext& ctx) {
    const auto* p=ctx.CommandBuffer();
    Result result=Kernel::ResultInvalidCombination;
    if(p[0]==0x13d&&p[1]==0)
        result=m_bootstrap.CurrentPhase()==GB::Phase::Ready
            ? ResultSuccess:Kernel::ResultInvalidState;
    if(result.IsSuccess())m_display_ready=true;
    IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
}
void IGraphicsSupervisor::FinalizeDisplayContext(HLERequestContext& ctx) {
    const auto* p=ctx.CommandBuffer();
    const Result result=p[0]==0x13f&&p[1]==0
        ? ResultSuccess:Kernel::ResultInvalidCombination;
    if(result.IsSuccess())m_display_ready=false;
    IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
}
void IGraphicsSupervisor::OpenDisplaySession(HLERequestContext& ctx) {
    const auto* p = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    if (p[0] == 0x141 && p[1] == 2 &&
        GraphicsSupervisorProtocol::IsSupportedDisplaySession(
            u64{p[2]} | (u64{p[3]} << 32))) {
        result = Kernel::ResultInvalidState;
        if (m_display_ready) {
            try {
                auto owner = std::static_pointer_cast<IGraphicsSupervisor>(shared_from_this());
                auto handler = std::make_shared<IVi2Session>(system, std::move(owner), m_aruid);
                result = CreateMetadataSession(system, ctx, std::move(handler), &client);
            } catch (const std::bad_alloc&) {
                result = Kernel::ResultOutOfResource;
            }
        }
    }
    // The Switch 2 SDK expects one MOVE handle descriptor for every native
    // GSV child session (305/202/497/1402 and video/feature children).
    // Internal AddMoveObject below is deliberate: the HLE serializer writes
    // its one handle into the same slot, then releases the KSession creator's
    // reference after handle_table.Add(). AddCopyObject would leak that ref.
    // On failure, never publish a descriptor or a null kernel object.
    // AddMoveObject may allocate after CreateMetadataSession published a
    // live KClientSession. On host OOM release its creator reference rather
    // than leaking a service session or throwing through the IPC handler.
    if (result.IsSuccess()) {
        try {
            ctx.AddMoveObject(client);
        } catch (const std::bad_alloc&) {
            client->Close(system.Kernel());
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::InitializeFeatureContext(HLERequestContext& ctx) {
    // SwitchBrew GSV 601/1201: no request parameters or return objects.
    // Do not acknowledge malformed IPC as a successful feature admission.
    const u32 command = ctx.GetCommand();
    const auto* packet = ctx.CommandBuffer();
    const bool valid = (command == 601 || command == 1201) &&
                       packet[0] == 16 + command && packet[1] == 0;
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(valid ? ResultSuccess.raw : Kernel::ResultInvalidCombination.raw);
}

void IGraphicsSupervisor::OpenCameraSession(HLERequestContext& ctx) {
    // Lichii/SDK observation: 602 carries u64 camera-context version 1 and
    // returns one MOVE CameraSession. Never serialize it as COPY or accept
    // a guessed zero-input shape.
    const auto* packet = ctx.CommandBuffer();
    const u64 version = u64{packet[2]} | (u64{packet[3]} << 32);
    if (packet[0] != 16 + 602 || packet[1] != 2 || version != 1) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidCombination.raw);
        return;
    }
    Kernel::KClientSession* client{};
    Result result = Kernel::ResultOutOfResource;
    try {
        auto handler = std::make_shared<INativeCameraSession>(system);
        result = CreateMetadataSession(system, ctx, std::move(handler), &client);
    } catch (const std::bad_alloc&) {
        result = Kernel::ResultOutOfResource;
    }
    if (result.IsSuccess()) {
        try {
            ctx.AddMoveObject(client);
        } catch (const std::bad_alloc&) {
            // Session publication failed: release its untransferred creator ref.
            client->Close(system.Kernel());
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenChatSession(HLERequestContext& ctx) {
    // 1202: GameChat initialization needs one real MOVE session, not a COPY.
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] != 16 + 1202 || packet[1] != 0) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidCombination.raw);
        return;
    }
    Kernel::KClientSession* client{};
    Result result = Kernel::ResultOutOfResource;
    try {
        auto handler = std::make_shared<INativeChatGsvSession>(system);
        result = CreateMetadataSession(system, ctx, std::move(handler), &client);
    } catch (const std::bad_alloc&) {
        result = Kernel::ResultOutOfResource;
    }
    if (result.IsSuccess()) {
        try {
            ctx.AddMoveObject(client);
        } catch (const std::bad_alloc&) {
            client->Close(system.Kernel());
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::CreateSyncptContext(HLERequestContext& ctx) {
    // Horizon S2 GSV 201 has no input/output. The per-process SyncptState
    // was already constructed with this GSV session; 202 serves the child
    // session and 204 attaches the real transfer-memory mirrors.
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 16 + 201 && packet[1] == 0) {
        result = m_syncpt && m_process &&
                         m_bootstrap.CurrentPhase() == GB::Phase::Ready
                     ? ResultSuccess : Kernel::ResultInvalidState;
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenSyncptSession(HLERequestContext& ctx) {
    // SwitchBrew GSV 202 has no input and returns one MOVE SyncptSession.
    // Reject malformed TIPC before allocating a session or publishing a handle.
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] != 16 + 202 || packet[1] != 0) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidCombination.raw);
        return;
    }
    Kernel::KClientSession* client{};
    Result result = Kernel::ResultOutOfResource;
    try {
        auto handler = std::make_shared<INativeSyncptSession>(system);
        result = CreateMetadataSession(system, ctx, std::move(handler), &client);
    } catch (const std::bad_alloc&) {
        result = Kernel::ResultOutOfResource;
    }
    if (result.IsSuccess()) {
        try {
            ctx.AddMoveObject(client);
        } catch (const std::bad_alloc&) {
            // The creator reference has not entered the outgoing queue.
            client->Close(system.Kernel());
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

void IGraphicsSupervisor::AttachSyncptMemory(HLERequestContext& ctx) {
    if (!m_syncpt) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidState.raw);
        return;
    }
    m_syncpt->Attach(ctx);
}

void IGraphicsSupervisor::OpenNvsrvSession(HLERequestContext& ctx) {
    // Switch 2 GSV 1402 returns a real NVDRV INvDrv2User session.
    // The guest uses this handle for NvRm channel setup before GPFIFO.
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    bool valid_request = packet[0] == 16 + 1402 && packet[1] == 0;
    if (packet[0] == 16 + 1401) {
        if (const auto request = GraphicsSupervisorProtocol::DecodeNvDrvTransferSession(
                std::span<const u32>{packet, IPC::COMMAND_BUFFER_LENGTH})) {
            auto memory = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(
                ctx.GetCopyHandle(0));
            result = Kernel::ResultInvalidHandle;
            if (!memory.IsNull() && memory->GetOwner() == m_process &&
                request->copied_handle == ctx.GetCopyHandle(0)) {
                result = Kernel::ResultInvalidSize;
                valid_request = request->size <= memory->GetSize();
            }
        }
    }
    if (valid_request) {
        result = Kernel::ResultInvalidState;
        if (m_process && m_bootstrap.CurrentPhase() == GB::Phase::Ready) {
            // The firmware permits only one live 1402 user session and
            // specifies 0x200f3 when the guest tries to open another.
            // Do not take an extra NVDRV container reference on failure.
            if (!m_nvsrv_session.expired()) {
                result = Result{0x200f3};
            } else {
                try {
                    const auto service =
                        system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
                    const auto module = service ? service->GetModule() : nullptr;
                    if (module) {
                        // 1402/1401 may precede GPU 497 in the native SDK's
                        // initialization sequence. The NVDRV FDs must already
                        // be authenticated as Ampere at their FIRST ioctl,
                        // not wait for IGpuSession / GpuWorkerSharedState.
                        // Hold one process-generation ticket for this exact
                        // child KSession lifetime. Its custom deleter runs
                        // *after* NVDRV::~NVDRV has closed the NvCore session.
                        // Failed session publication and guest CloseHandle
                        // use the very same rollback path; the parent GSV
                        // only retains a weak handle, never a zombie owner.
                        auto pending = std::make_unique<Nvidia::NVDRV>(
                            system, module, "nx2:nvsrv");
                        auto& registry =
                            Nvidia::Devices::NativeGpuProcessRegistry::Instance();
                        if (!registry.Register(m_process)) {
                            result = Kernel::ResultOutOfResource;
                        } else {
                            // The shared_ptr raw-pointer constructor invokes
                            // this deleter even if its control-block
                            // allocation throws. No leaked process ticket.
                            std::shared_ptr<Nvidia::NVDRV> handler{
                                pending.release(),
                                [native_process = m_process](Nvidia::NVDRV* child) noexcept {
                                    delete child;
                                    Nvidia::Devices::NativeGpuProcessRegistry::Instance()
                                        .Unregister(native_process);
                                }};
                            if (handler->InitializeForProcess(m_process)) {
                                result = CreateMetadataSession(system, ctx, handler, &client);
                                if (result.IsSuccess())
                                    m_nvsrv_session = handler;
                            }
                        }
                    }
                } catch (const std::bad_alloc&) {
                    result = Kernel::ResultOutOfResource;
                }
            }
        }
    }
    // Both native 1401 and 1402 return a MOVE descriptor; do not manufacture
    // a session until a process-owned TransferMemory passes 1401 validation.
    // TIPC 1402 returns a MOVE descriptor; fail-closed admission must never
    // publish a null handle. Preserve normal NVDRV/NS1 registration.
    // AddMoveObject may allocate after CreateMetadataSession published a
    // live KClientSession. On host OOM release its creator reference rather
    // than leaking a service session or throwing through the IPC handler.
    if (result.IsSuccess()) {
        try {
            ctx.AddMoveObject(client);
        } catch (const std::bad_alloc&) {
            client->Close(system.Kernel());
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    // Only actual NVDRV service modules can back address-space FD creation.
    const auto source = system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
    const auto module = source ? source->GetModule() : nullptr;
    Result result = TranslateBootstrap(m_bootstrap.CanOpenGpuSession(packet, module != nullptr));
    if (result.IsSuccess() && ctx.GetCommand() == 496) {
        // A TransferMemory work buffer is borrowed for the legacy GPU
        // session. HLE owns the real address spaces through NvCore, not the
        // unused work-buffer storage. Never accept another process's memory.
        const auto request = GraphicsSupervisorProtocol::DecodeGpuTransferSession(packet);
        if (!request) {
            result = Kernel::ResultInvalidCombination;
        } else {
            auto memory = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(
                ctx.GetCopyHandle(0));
            result = Kernel::ResultInvalidHandle;
            if (!memory.IsNull() && memory->GetOwner() == m_process &&
                request->copied_handle == ctx.GetCopyHandle(0)) {
                result = request->size <= memory->GetSize()
                    ? ResultSuccess : Kernel::ResultInvalidSize;
            }
        }
    }
    Kernel::KClientSession* client{};
    if (result.IsSuccess()) {
        try {
            std::erase_if(m_gpu_sessions, [](const auto& weak) { return weak.expired(); });
            // Allocate the weak-owner slot BEFORE publishing a kernel session.
            // A later vector growth failure must not orphan that client handle.
            m_gpu_sessions.reserve(m_gpu_sessions.size() + 1);
            auto handler = std::make_shared<IGpuSession>(system, module, m_process);
            result = CreateMetadataSession(system, ctx, handler, &client);
            if (result.IsSuccess()) {
                m_gpu_sessions.emplace_back(handler);
            }
        } catch (const std::bad_alloc&) {
            result = Kernel::ResultOutOfResource;
        }
    }
    // TIPC 497 must expose the guest's expected MOVE descriptor only on
    // successful bootstrap/NVDRV admission; no null session on failure.
    // AddMoveObject may allocate after CreateMetadataSession published a
    // live KClientSession. On host OOM release its creator reference rather
    // than leaking a service session or throwing through the IPC handler.
    if (result.IsSuccess()) {
        try {
            ctx.AddMoveObject(client);
        } catch (const std::bad_alloc&) {
            client->Close(system.Kernel());
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::QueryDonatedTransferMemoryUsage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] != 16 + 498 || packet[1] != 0) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidCombination.raw);
        return;
    }
    // This query only exists for the TransferMemory boot form. Remote-memory
    // bootstrap did not donate a work buffer and must not receive a synthetic
    // success. Encore's HLE services retain the real buffer but intentionally
    // allocate their runtime objects elsewhere, so used and peak are exactly 0.
    if (!m_bootstrap_transfer_memory || m_bootstrap_transfer_memory_size == 0) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidState.raw);
        return;
    }
    NativeTipcReplyWords(ctx, {
        static_cast<u32>(m_bootstrap_transfer_memory_size),
        static_cast<u32>(m_bootstrap_transfer_memory_size >> 32),
        0, 0, 0, 0,
    });
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
void IGraphicsSupervisor::BootNativeMemoryManagement(HLERequestContext& ctx) {
    // TIPC 115 exchanges an input u64 feature request for an output u64
    // negotiated mask. Older SDKs call it after 204 and before GPU 497.
    const auto* packet = ctx.CommandBuffer();
    if (packet[0] != 16 + 115 || packet[1] != 2) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidCombination.raw);
        return;
    }
    const auto service = system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
    const auto module = service ? service->GetModule() : nullptr;
    if (!m_process || m_bootstrap.CurrentPhase() != GB::Phase::Ready || !module) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidState.raw);
        return;
    }
    const u64 requested = u64{packet[2]} | (u64{packet[3]} << 32);
    const u64 granted = GraphicsSupervisorProtocol::GrantNativeMemoryFeatures(requested, true);
    NativeTipcReplyWords(ctx, {static_cast<u32>(granted), static_cast<u32>(granted >> 32)});
}

void IGraphicsSupervisor::ConfigureGraphicsFrameContext(HLERequestContext& ctx) {
    const u32 command_id = ctx.GetCommand();
    const std::array<u32, 2> command{ctx.CommandBuffer()[0], ctx.CommandBuffer()[1]};
    Result result = Kernel::ResultInvalidCombination;
    if (GraphicsSupervisorProtocol::IsGraphicsFrameContextCommand(command, command_id)) {
        const std::scoped_lock lock{m_graphics_frame_mutex};
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.CurrentPhase() == GB::Phase::Ready) {
            if (command_id == 1101 && !m_graphics_frame_created) {
                m_graphics_frame_created = true;
                result = ResultSuccess;
            } else if (command_id == 1102 && m_graphics_frame_created &&
                       !m_graphics_frame_initialized) {
                m_graphics_frame_initialized = true;
                result = ResultSuccess;
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::SetGraphicsFrameMode(HLERequestContext& ctx) {
    const u32 command_id = ctx.GetCommand();
    const std::array<u32, 3> command{
        ctx.CommandBuffer()[0], ctx.CommandBuffer()[1], ctx.CommandBuffer()[2]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto setting =
            GraphicsSupervisorProtocol::DecodeGraphicsFrameSetting(command, command_id)) {
        const std::scoped_lock lock{m_graphics_frame_mutex};
        result = Kernel::ResultInvalidState;
        if (m_graphics_frame_initialized) {
            switch (setting->command) {
            case 1111:
                m_graphics_frame_length = setting->value;
                break;
            case 1112:
                m_graphics_frame_restart_mode = setting->value;
                break;
            case 1113:
                m_graphics_frame_timeslice_mode = setting->value;
                break;
            default:
                IPC::ResponseBuilder rb{ctx, 2};
                rb.Push(Kernel::ResultInvalidCombination.raw);
                return;
            }
            // These are GSV scheduling policy records. VI2 owns actual
            // presentation cadence on this host; accepting the negotiated
            // values must not invent a GPU drain or alter unrelated queues.
            result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
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
                            if (const auto transfer = m_native_transfer_backings.find(packet[2]);
                                transfer != m_native_transfer_backings.end()) {
                                transfer->second->Close(kernel);
                                m_native_transfer_backings.erase(transfer);
                            } else {
                                result = m_process->GetPageTable().UnlockForDeviceAddressSpace(
                                    freed->address, freed->size);
                            }
                        }
                        // Parent pin/duplicate are retained only while the
                        // child's NVMap and its page-table lock remain alive.
                        if (const auto parent = m_native_parents.find(packet[2]);
                            parent != m_native_parents.end()) {
                            nvmap.UnpinHandle(parent->second);
                            nvmap.FreeHandle(parent->second, true);
                            m_native_parents.erase(parent);
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::BindNativeTransferMemory(HLERequestContext& ctx) {
    const std::array<u32, 9> packet{
        ctx.CommandBuffer()[0], ctx.CommandBuffer()[1], ctx.CommandBuffer()[2],
        ctx.CommandBuffer()[3], ctx.CommandBuffer()[4], ctx.CommandBuffer()[5],
        ctx.CommandBuffer()[6], ctx.CommandBuffer()[7], ctx.CommandBuffer()[8]};
    const auto request = GraphicsSupervisorProtocol::DecodeNativeTransferMemoryBind(packet);
    Result result = Kernel::ResultInvalidCombination;
    if (!request) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }

    const u32 copy_handle = ctx.GetCopyHandle(0);
    auto transfer = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(copy_handle);
    result = Kernel::ResultInvalidHandle;
    if (transfer.IsNull() || request->copied_handle != copy_handle ||
        transfer->GetOwner() != m_process) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    if (GetInteger(transfer->GetSourceAddress()) != request->address ||
        transfer->GetSize() != request->size) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidSize.raw);
        return;
    }

    const std::scoped_lock lock{m_native_memory_mutex};
    result = Kernel::ResultInvalidState;
    if (!m_bootstrap.GpuContextReady() || !m_native_module ||
        m_native_transfer_backings.contains(request->memory)) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    const auto validated =
        m_native_memory.ValidateBind(request->memory, request->address, request->size);
    if (validated != GraphicsSupervisorProtocol::NativeMemoryStatus::Success) {
        result = NativeMemoryError(validated);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }

    auto& container = m_native_module->GetContainer();
    auto& nvmap = container.GetNvMapFile();
    auto handle = nvmap.GetHandle(request->memory);
    if (!handle || handle->allocated) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidState.raw);
        return;
    }
    if (!m_native_memory_session) {
        try {
            m_native_memory_session = container.OpenSession(m_process).id;
        } catch (const std::bad_alloc&) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultOutOfResource.raw);
            return;
        }
    }

    // Reserve the persistent owner node before publishing any NVMap state.
    // Once Handle::Alloc succeeds, NativeMemoryLedger::Bind cannot allocate
    // and cannot fail because the same mutex protects the validated record.
    decltype(m_native_transfer_backings)::iterator retained;
    try {
        const auto [it, inserted] =
            m_native_transfer_backings.try_emplace(request->memory, nullptr);
        if (!inserted) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidState.raw);
            return;
        }
        retained = it;
    } catch (const std::bad_alloc&) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultOutOfResource.raw);
        return;
    }

    transfer->Open(kernel);
    retained->second = transfer.GetPointerUnsafe();
    const auto allocated = handle->Alloc(
        {}, Kernel::PageSize, 0, request->address,
        Nvidia::NvCore::SessionId{*m_native_memory_session});
    if (allocated != Nvidia::NvResult::Success) {
        retained->second->Close(kernel);
        m_native_transfer_backings.erase(retained);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(allocated == Nvidia::NvResult::InsufficientMemory
                    ? Kernel::ResultOutOfResource.raw
                    : Kernel::ResultInvalidState.raw);
        return;
    }

    const auto bound =
        m_native_memory.Bind(request->memory, request->address, request->size);
    if (bound != GraphicsSupervisorProtocol::NativeMemoryStatus::Success) {
        // Unreachable after ValidateBind under m_native_memory_mutex. Do not
        // drop the TransferMemory ref if that invariant is ever violated:
        // Handle::Alloc has already published an address consumed by SMMU.
        LOG_CRITICAL(Service_AM,
                     "NS2 TIPC 113 ledger changed after validated NVMap bind id={}",
                     request->memory);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidState.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess.raw);
}

Result IGraphicsSupervisor::BindNativeMemoryBacking(
    u32 id, u64 address, u64 size, u64 flags, u64 offset) {
    if (!m_bootstrap.GpuContextReady()) return Kernel::ResultInvalidState;
    if ((flags != 0 && flags != 2) || offset != 0)
        return Kernel::ResultNotImplemented;
    const auto record = m_native_memory.Get(id);
    const auto validated = m_native_memory.ValidateBind(id, address, size);
    if (validated != GraphicsSupervisorProtocol::NativeMemoryStatus::Success)
        return NativeMemoryError(validated);
    if (!record || record->size != size || address == 0)
        return Kernel::ResultInvalidSize;
    if (!m_native_module) return Kernel::ResultInvalidState;

    auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
    const auto handle = nvmap.GetHandle(id);
    if (!handle || handle->allocated) return Kernel::ResultInvalidState;
    auto& table = m_process->GetPageTable();
    if (!table.Contains(address, size)) return Kernel::ResultInvalidCurrentMemory;
    const u64 end = address + size; // Bounds checked by the owned ledger.
    for (u64 cursor = address; cursor < end;) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page{};
        const Result query = table.QueryInfo(&info, &page, cursor);
        if (query.IsError() || info.m_size == 0 ||
            info.m_address > cursor ||
            info.m_size > std::numeric_limits<u64>::max() - info.m_address ||
            (info.m_state & Kernel::KMemoryState::FlagMapped) !=
                Kernel::KMemoryState::FlagMapped ||
            (flags == 2 &&
             (info.m_state & Kernel::KMemoryState::FlagCanDeviceMap) !=
                 Kernel::KMemoryState::FlagCanDeviceMap))
            return Kernel::ResultInvalidCurrentMemory;
        const u64 next = info.m_address + info.m_size;
        if (next <= cursor) return Kernel::ResultInvalidCurrentMemory;
        cursor = next;
    }

    // A first NVDRV session can allocate host session/SMMU state. Acquire
    // that owner before locking any guest pages: if it fails there is
    // nothing to unlock, no bound child and no orphaned NVMap backing.
    if (!m_native_memory_session) {
        try {
            m_native_memory_session =
                m_native_module->GetContainer().OpenSession(m_process).id;
        } catch (const std::bad_alloc&) {
            return Kernel::ResultOutOfResource;
        }
    }
    bool is_io{};
    const Result locked = table.LockForMapDeviceAddressSpace(
        &is_io, address, size, Kernel::KMemoryPermission::None, true, true);
    if (locked.IsError()) return locked;
    if (is_io) {
        table.UnlockForDeviceAddressSpace(address, size);
        return Kernel::ResultNotImplemented;
    }
    const auto alloc = handle->Alloc(
        {}, Kernel::PageSize, 0, address,
        Nvidia::NvCore::SessionId{*m_native_memory_session});
    if (alloc != Nvidia::NvResult::Success) {
        table.UnlockForDeviceAddressSpace(address, size);
        return Kernel::ResultOutOfResource;
    }
    return NativeMemoryError(m_native_memory.Bind(id, address, size));
}

void IGraphicsSupervisor::BindNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x82 && packet[1] == 9) {
        const u32 id = packet[2];
        const u64 address = NativeU64(packet, 3);
        const u64 size = NativeU64(packet, 5);
        const u64 flags = NativeU64(packet, 7);
        const u64 offset = NativeU64(packet, 9);
        const std::scoped_lock lock{m_native_memory_mutex};
        result = BindNativeMemoryBacking(id, address, size, flags, offset);
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::BindNativeGpuMemory(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), 14};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request =
            GraphicsSupervisorProtocol::DecodeNativeParentBind(packet)) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        if (m_native_module && m_native_memory.Get(request->child) &&
            !m_native_parents.contains(request->child)) {
            auto& container = m_native_module->GetContainer();
            auto& nvmap = container.GetNvMapFile();
            const auto parent = nvmap.GetHandle(request->parent);
            if (parent && parent->allocated) {
                const auto* session = container.GetSession(parent->session_id);
                const u64 offset = request->parent_offset;
                if (session && session->process == m_process &&
                    offset <= parent->size && request->size <= parent->size - offset &&
                    offset <= std::numeric_limits<u64>::max() - parent->address &&
                    parent->address + offset == request->address) {
                    result = Kernel::ResultOutOfResource;
                    // Reserve the parent/child ledger node BEFORE Duplicate,
                    // PinHandle, guest page-lock and child NVMap Alloc. A
                    // post-bind std::map::emplace OOM previously left a
                    // bound child with no parent pin owner or rollback path.
                    auto staged_parent = m_native_parents.end();
                    try {
                        const auto [it, inserted] = m_native_parents.try_emplace(
                            request->child, request->parent);
                        if (inserted) staged_parent = it;
                    } catch (const std::bad_alloc&) {
                        // No guest or NVDRV state has been changed.
                    }
                    if (staged_parent != m_native_parents.end()) {
                        const bool duplicated =
                            parent->Duplicate(true) == Nvidia::NvResult::Success;
                        if (duplicated) {
                            const auto pinned = nvmap.PinHandle(request->parent, false);
                            if (pinned) {
                                result = BindNativeMemoryBacking(
                                    request->child, request->address,
                                    request->size, 2, 0);
                                if (result.IsError())
                                    nvmap.UnpinHandle(request->parent);
                            }
                        }
                        if (result.IsError()) {
                            if (duplicated)
                                nvmap.FreeHandle(request->parent, true);
                            m_native_parents.erase(staged_parent);
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
IGraphicsSupervisor::FindRegisteredGpuAddressSpace(u32 fd) {
    // The process identity is enforced by IGpuSession's registered FD owner.
    // A numeric FD alone must never grant access to another KProcess GMMU.
    for (const auto& weak : m_gpu_sessions) {
        if (const auto session = weak.lock()) {
            if (auto as = session->GetRegisteredAddressSpace(fd))
                return as;
        }
    }
    return {};
}

// Select only pool IDs whose memory is owned, CPU bound, and still backed by
// the process that created this graphics supervisor. No synthetic image ID.
std::optional<IGraphicsSupervisor::VideoSurface>
IGraphicsSupervisor::LookupVideoSurface(u32 image_id) {
    const std::scoped_lock lock{m_native_memory_mutex};
    const auto view = m_video_pools.Lookup(image_id);
    if (!view || !m_native_module || !m_process) return {};
    const auto record = m_native_memory.Get(view->backing);
    if (!record || !record->bound || !record->guest_address ||
        view->offset > record->size || view->size > record->size - view->offset ||
        record->guest_address > std::numeric_limits<u64>::max() - view->offset)
        return {};
    auto& container = m_native_module->GetContainer();
    const auto handle = container.GetNvMapFile().GetHandle(view->backing);
    if (!handle || !handle->allocated || handle->size < record->size ||
        handle->address != record->guest_address) return {};
    const auto* session = container.GetSession(handle->session_id);
    if (!session || session->process != m_process) return {};
    const u64 address = record->guest_address + view->offset;
    if (!m_process->GetMemory().IsValidVirtualAddressRange(
            Common::ProcessAddress{address}, view->size)) return {};
    for (const auto& [_, pool] : m_video_pools.Records()) {
        if (image_id < pool.first_id || image_id - pool.first_id >= pool.count) continue;
        const u32 width = (pool.packed_dimensions & 0xffff) + 1;
        const u32 height = (pool.packed_dimensions >> 16) + 1;
        if (pool.layout.image_bytes != view->size ||
            pool.layout.row_bytes == 0 || pool.layout.cpu_kind < 8 ||
            pool.layout.cpu_kind > 10) return {};
        const bool gpu_mapped = std::any_of(m_video_mappings.begin(), m_video_mappings.end(),
            [image_id](const auto& entry) {
                return entry.first.second == image_id && entry.second.references != 0;
            });
        return VideoSurface{image_id, view->backing, address, width, height,
                            pool.layout, gpu_mapped};
    }
    return {};
}

// Re-check the exact allocation after rendering; guest 912/112 may retire
// the pool while FFmpeg decodes. The actual guest write and validity checks
// are serialized against pool destruction under m_native_memory_mutex.
bool IGraphicsSupervisor::WriteVideoSurface(
    u32 image_id, const VideoSurface& expected, std::span<const u8> pixels) {
    if (pixels.size() != expected.layout.image_bytes || !pixels.data()) return false;
    const std::scoped_lock lock{m_native_memory_mutex};
    const auto view = m_video_pools.Lookup(image_id);
    if (!view || view->backing != expected.backing || expected.image_id != image_id ||
        view->size != pixels.size() || !m_native_module || !m_process)
        return false;
    // A 426/427 map can change while the codec was rendering. Never write
    // pitch-linear bytes into a newly GPU-mapped block-linear allocation.
    const bool gpu_mapped = std::any_of(m_video_mappings.begin(), m_video_mappings.end(),
        [image_id](const auto& entry) {
            return entry.first.second == image_id && entry.second.references != 0;
        });
    if (gpu_mapped != expected.gpu_mapped) return false;
    const auto record = m_native_memory.Get(view->backing);
    if (!record || !record->bound || !record->guest_address ||
        view->offset > record->size || view->size > record->size - view->offset ||
        record->guest_address > std::numeric_limits<u64>::max() - view->offset ||
        record->guest_address + view->offset != expected.address)
        return false;
    auto& container = m_native_module->GetContainer();
    const auto handle = container.GetNvMapFile().GetHandle(view->backing);
    if (!handle || !handle->allocated || handle->address != record->guest_address ||
        !handle->d_address || view->offset > std::numeric_limits<u64>::max() - handle->d_address)
        return false;
    const auto* session = container.GetSession(handle->session_id);
    if (!session || session->process != m_process ||
        !m_process->GetMemory().IsValidVirtualAddressRange(
            Common::ProcessAddress{expected.address}, pixels.size())) return false;
    if (!m_process->GetMemory().WriteBlock(
            Common::ProcessAddress{expected.address}, pixels.data(), pixels.size())) return false;
    system.GPU().InvalidateRegion(handle->d_address + view->offset, pixels.size());
    return true;
}

// 901 is the SDK video *decoder* context admission call.
// Acknowledging these without a live GPU context and NVDRV module would
// falsely advertise a usable decoder and cause later codec handle failures.
// Existing 902/905/906 still authenticate real decoder child sessions, rather
// than relying on a guessed global initialized flag shared across SDKs.
// SwitchBrew GSV 1001/1002 are VIDEO ENCODER operations, not decoder
// aliases. Eden has no NS2 encoder backend; avoid granting a decoder
// session or advertising encoder support without an implementation.
void IGraphicsSupervisor::UnavailableVideoEncoder(HLERequestContext& ctx) {
    // not implemented yet on Encore
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
}

// Documented but unsupported optional services (debug, camera, encoder, Adraw,
// and system-only decoder pathways). A single fail-closed handler is sufficient:
// distinct function table entries keep every command ID and name explicit.
// Do not read undocumented request payloads, create guest objects or return Success.
void IGraphicsSupervisor::UnavailableDocumentedFeature(HLERequestContext& ctx) {
    // not implemented yet on Encore
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
}

void IGraphicsSupervisor::InitializeVideoCodecContext(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const u32 command = ctx.GetCommand();
    Result result = Kernel::ResultInvalidCombination;
    if (command == 901 && packet[0] == command + 0x10 && packet[1] == 0) {
        result = Kernel::ResultInvalidState;
        if (m_process && m_bootstrap.GpuContextReady()) {
            const auto source = system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
            const auto module = source ? source->GetModule() : nullptr;
            if (module) result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

// 902/905/906 create real decoder child kernel sessions. No codec operation
// reports success until the session has initialized its actual FFmpeg owner.
void IGraphicsSupervisor::OpenVideoDecoderSession(HLERequestContext& ctx) {
    const auto* w = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    const u32 cmd = ctx.GetCommand();
    // 902 has no input words; 905 carries a flag, 906 a flag and
    // library version. Reject malformed TIPC lengths before creating a
    // kernel session or reserving a service handle.
    const bool shape = cmd == 902 ? w[1] == 0 :
                       cmd == 905 ? (w[1] == 1 && w[2] <= 0xffU) :
                       cmd == 906 && w[1] == 3;
    if (w[0] == cmd + 0x10 && shape) {
        result = Kernel::ResultInvalidState;
        if (m_process && m_bootstrap.GpuContextReady()) {
            auto owner = std::static_pointer_cast<IGraphicsSupervisor>(shared_from_this());
            try {
                auto handler = std::make_shared<IGraphicsVideoDecoderSession>(
                    system, std::move(owner), true);
                result = CreateMetadataSession(system, ctx, std::move(handler), &client);
            } catch (const std::bad_alloc&) {
                result = Kernel::ResultOutOfResource;
            }
        }
    }
    // A decoder interface is a TIPC MOVE handle, not a COPY descriptor.
    // Publish the creator reference only after the outgoing queue accepts
    // it. On host OOM, close that reference and report no guest handle.
    if (result.IsSuccess()) {
        try {
            ctx.AddMoveObject(client);
        } catch (const std::bad_alloc&) {
            client->Close(system.Kernel());
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::OpenVideoDecoderChildSession(HLERequestContext& ctx) {
    Kernel::KClientSession* client{};
    auto owner = std::static_pointer_cast<IGraphicsSupervisor>(shared_from_this());
    Result result = Kernel::ResultOutOfResource;
    try {
        auto child = std::make_shared<IGraphicsVideoDecoderSession>(
            system, std::move(owner), false);
        result = CreateMetadataSession(system, ctx, std::move(child), &client);
    } catch (const std::bad_alloc&) {
        result = Kernel::ResultOutOfResource;
    }
    // A decoder interface is a TIPC MOVE handle, not a COPY descriptor.
    // Publish the creator reference only after the outgoing queue accepts
    // it. On host OOM, close that reference and report no guest handle.
    if (result.IsSuccess()) {
        try {
            ctx.AddMoveObject(client);
        } catch (const std::bad_alloc&) {
            client->Close(system.Kernel());
            result = Kernel::ResultOutOfResource;
        }
    }
    if (result.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result.raw);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
}

// GSV 991 is the decoder-pool analogue of 1510/1291: reply with the
// required backing size and alignment BEFORE the guest allocates memory.
// Use the exact same validated pitch-linear layout that GSV 911 later
// registers; otherwise a successful size query could lead to a short
// allocation and out-of-range video writes on the GPU/decoder worker.
void IGraphicsSupervisor::CalculateDecoderImagePoolBuffer(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const auto layout = (packet[0] == 16 + 991 && packet[1] == 17)
        ? CalculateNativeVideoImageLayout(std::span<const u32>{packet + 2, 16}, packet[18])
        : std::nullopt;
    const Result result = layout ? ResultSuccess : Kernel::ResultInvalidCombination;
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result.raw);
    rb.Push(layout ? static_cast<u32>(layout->total_bytes) : 0u);
    rb.Push(layout ? static_cast<u32>(layout->total_bytes >> 32) : 0u);
    // nn::codec::video::detail::ipc GSV 991: decoder backing alignment
    // is 64 * 128. This is a requirement, not a fabricated GPU VA.
    constexpr u64 DecoderPoolAlignment = 64ull * 128;
    rb.Push(layout ? static_cast<u32>(DecoderPoolAlignment) : 0u);
    rb.Push(0u);
}

void IGraphicsSupervisor::CreateVideoImagePool(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 first{};
    const bool chat_pool = ctx.GetCommand() == 1211;
    const u32 expected_header = chat_pool ? 0x4cbU : 0x39fU;
    if (packet[0] == expected_header && packet[1] == 18) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            const std::span<const u32> preference{packet + 2, 16};
            const u32 count = packet[18];
            const auto layout = chat_pool
                ? CalculateChatCameraImageLayout(preference, count)
                : CalculateNativeVideoImageLayout(preference, count);
            result = Kernel::ResultNotImplemented;
            if (layout && (layout->image_bytes & 0xfffU) == 0 &&
                (layout->image_stride & 0xfffU) == 0) {
                const std::scoped_lock lock{m_native_memory_mutex};
                result = Kernel::ResultNotFound;
                const u32 backing = packet[19];
                const auto owned = m_native_memory.Get(backing);
                if (owned && owned->bound && m_native_module) {
                    auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                    const auto handle = nvmap.GetHandle(backing);
                    result = Kernel::ResultInvalidSize;
                    if (handle && handle->allocated && handle->size >= layout->total_bytes) {
                        const auto* session =
                            m_native_module->GetContainer().GetSession(handle->session_id);
                        result = Kernel::ResultInvalidHandle;
                        if (session && session->process == m_process) {
                            result = Kernel::ResultOutOfResource;
                            if (m_next_image_id &&
                                m_next_image_id + count - 1 <=
                                    std::numeric_limits<u32>::max() &&
                                m_video_pools.ImageCount() <=
                                    NativeVideoPoolLedger::MaxImages - count &&
                                handle->Duplicate(true) == Nvidia::NvResult::Success) {
                                const DAddr pinned = nvmap.PinHandle(backing, false);
                                if (pinned) {
                                    if (m_video_pools.Add(static_cast<u32>(m_next_image_id),
                                                          count, backing, handle->size,
                                                          preference[2], *layout)) {
                                        first = static_cast<u32>(m_next_image_id);
                                        m_next_image_id += count;
                                        result = ResultSuccess;
                                    } else {
                                        nvmap.UnpinHandle(backing);
                                    }
                                }
                                if (result.IsError()) nvmap.FreeHandle(backing, true);
                            }
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(first);
}

void IGraphicsSupervisor::DestroyVideoImagePool(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    const bool chat_pool = ctx.GetCommand() == 1212;
    const u32 expected_header = chat_pool ? 0x4ccU : 0x3a0U;
    if (packet[0] == expected_header && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        const auto pool = m_video_pools.Get(packet[2]);
        if (pool && m_native_module) {
            result = Kernel::ResultBusy;
            bool mapped = false;
            for (const auto& [key, mapping] : m_video_mappings) {
                const u32 image = key.second;
                if (image >= pool->first_id &&
                    image - pool->first_id < pool->count) {
                    mapped = true;
                    break;
                }
            }
            if (!mapped) {
                auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                nvmap.UnpinHandle(pool->backing);
                const auto freed = nvmap.FreeHandle(pool->backing, true);
                result = freed && m_video_pools.Remove(pool->first_id)
                    ? ResultSuccess : Kernel::ResultInvalidState;
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

// 961 returns a 128-byte CPU image descriptor for a currently owned
// decoder-pool image. Never invent offsets or acknowledge a destroyed ID.
void IGraphicsSupervisor::GetCpuVideoImageDescriptor(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::array<u32, 32> descriptor{};
    if (packet[0] == 0x3c1 && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        if (const auto owned = m_video_pools.CpuImageDescriptor(packet[2])) {
            descriptor = *owned;
            result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 34};
    rb.Push(result.raw);
    for (const u32 word : descriptor) rb.Push(word);
}

void IGraphicsSupervisor::QueryVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::array<u64, 16> descriptor{};
    u64 image_offset{};
    if (packet[0] == 0x1bc && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        if (const auto image = m_video_pools.Lookup(packet[2])) {
            descriptor[0] = 1;
            descriptor[1] = image->packed_dimensions;
            descriptor[4] = 0x101000001ULL;
            descriptor[5] = image->offset | (image->size << 32);
            if (image->native_gpu_layout) {
                // nn::chat::gsv native images expose layout code, block
                // height and pitch, and return the per-image backing offset.
                descriptor[0] |= u64{image->gpu_layout_code} << 32;
                descriptor[2] = u64{image->block_height_log2} |
                                (u64{image->pitch_gobs} << 32);
                image_offset = image->offset;
            }
            result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 39};
    rb.Push(result.raw);
    rb.Push<u64>(0);
    rb.Push(image_offset);
    for (const u64 field : descriptor) rb.Push(field);
    rb.Push<u32>(0);
}

void IGraphicsSupervisor::MapVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::array<u64, 16> descriptor{};
    u64 address{}, size{}, second_reply{};
    if (packet[0] == 0x1ba && packet[1] == 4 && packet[3] == 1 &&
        ((packet[5] & 0xff) == 1 || (packet[5] & 0xff) == 3)) {
        result = Kernel::ResultNotFound;
        if (const auto as = FindRegisteredGpuAddressSpace(packet[4])) {
            const std::scoped_lock lock{m_native_memory_mutex};
            if (const auto image = m_video_pools.Lookup(packet[2])) {
                descriptor[0] = 1;
                descriptor[1] = image->packed_dimensions;
                descriptor[4] = 0x101000001ULL;
                descriptor[5] = image->offset | (image->size << 32);
                if (image->native_gpu_layout) {
                    descriptor[0] |= u64{image->gpu_layout_code} << 32;
                    descriptor[2] = u64{image->block_height_log2} |
                                    (u64{image->pitch_gobs} << 32);
                }
                size = image->size;
                const auto key = std::make_pair(packet[4], packet[2]);
                if (auto it = m_video_mappings.find(key); it != m_video_mappings.end()) {
                    // A recycled numeric FD is not proof of the same AS/GMMU.
                    result = Kernel::ResultInvalidState;
                    if (it->second.address_space == as) {
                        result = Kernel::ResultOutOfResource;
                        if (it->second.references != std::numeric_limits<u32>::max()) {
                            ++it->second.references;
                            address = it->second.address;
                            result = ResultSuccess;
                        }
                    }
                } else {
                    // Reserve the CPU ownership node BEFORE MapNativeImage
                    // commits a real GMMU mapping and NVMap pin. Emplacing
                    // afterwards can throw bad_alloc, orphaning a live VA
                    // that no supervisor entry knows how to retire.
                    result = Kernel::ResultOutOfResource;
                    auto staged = m_video_mappings.end();
                    try {
                        staged = m_video_mappings.try_emplace(
                            key, VideoImageMapping{as, 0, image->size, 1}).first;
                    } catch (const std::bad_alloc&) {
                        // No GMMU operation or NVMap pin has occurred.
                    }
                    if (staged != m_video_mappings.end()) {
                        const auto mapped = as->MapNativeImage(
                            image->backing, image->offset, image->size, address);
                        if (mapped == Nvidia::NvResult::Success) {
                            staged->second.address = address;
                            result = ResultSuccess;
                        } else {
                            // The driver owns its internal failure rollback.
                            // Never retain an unbound entry with address 0.
                            m_video_mappings.erase(staged);
                            address = 0;
                        }
                    }
                }
                if (result.IsSuccess()) {
                    if (image->native_gpu_layout) {
                        // GameChat 426 returns backing offset; keep the same
                        // offset in the GpuOunceUserImageDescriptor.
                        second_reply = image->offset;
                    } else {
                        // Decoder images keep Eden's historical mapped ABI:
                        // second u64 is the last mapped byte and descriptor
                        // offset becomes zero once directly GPU-addressed.
                        descriptor[5] &= 0xffffffff00000000ULL;
                        second_reply = address + size - 1;
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 39};
    rb.Push(result.raw);
    rb.Push(address);
    rb.Push(second_reply);
    for (const u64 field : descriptor) rb.Push(field);
    rb.Push<u32>(0);
}

void IGraphicsSupervisor::UnmapVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1bb && packet[1] == 6) {
        const auto as = FindRegisteredGpuAddressSpace(packet[3]);
        const auto key = std::make_pair(packet[3], packet[2]);
        const u64 address = NativeU64(packet, 4);
        const u64 end = NativeU64(packet, 6);
        // Only a final image reference can unmap the GMMU and release its
        // physical NVMap pin. The prior GPU queue MUST retire first; waiting
        // with m_native_memory_mutex held can deadlock presentation callbacks.
        // Capture the ownership shape under the lock, then revalidate after
        // the drain so a second unmap cannot drop an unretired pin.
        bool may_retire = false;
        if (as) {
            const std::scoped_lock preflight{m_native_memory_mutex};
            const auto it = m_video_mappings.find(key);
            may_retire = it != m_video_mappings.end() &&
                it->second.address_space == as && it->second.address == address &&
                it->second.size > 0 && end >= address &&
                end - address == it->second.size - 1 &&
                it->second.references == 1;
        }
        if (may_retire && system.IsPoweredOn() && !system.IsShuttingDown())
            system.GPU().WaitForIdle();

        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        const auto it = m_video_mappings.find(key);
        if (as && it != m_video_mappings.end() &&
            it->second.address_space == as && it->second.address == address &&
            it->second.size > 0 && end >= address &&
            end - address == it->second.size - 1) {
            result = ResultSuccess;
            if (it->second.references == 1) {
                if (!may_retire && system.IsPoweredOn() && !system.IsShuttingDown()) {
                    // Another request dropped the count to one during
                    // preflight. Retry rather than release without a drain.
                    result = Kernel::ResultInvalidState;
                } else {
                    result = it->second.address_space->UnmapNative4K(
                        address, it->second.size) == Nvidia::NvResult::Success
                        ? ResultSuccess : Kernel::ResultInvalidState;
                    if (result.IsSuccess()) m_video_mappings.erase(it);
                }
            } else {
                --it->second.references;
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
            const auto requirements = ::Service::AM::CalculateNativeImageRequirements(preferences, packet[18]);
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
                            m_next_image_id <= std::numeric_limits<u32>::max() &&
                            handle->Duplicate(true) == Nvidia::NvResult::Success) {
                            // The map insertion in NativeImageLedger::Create
                            // can allocate after the NVMap duplicate succeeds.
                            // An allocation failure must not leak that live
                            // handle or escape as an uncaught HLE exception.
                            try {
                                if (const auto created =
                                        m_native_images.Create(
                                            backing, handle->size, *requirements, preferences,
                                            static_cast<u32>(m_next_image_id))) {
                                    id = *created;
                                    ++m_next_image_id;
                                    result = ResultSuccess;
                                } else {
                                    nvmap.FreeHandle(backing, true);
                                }
                            } catch (const std::bad_alloc&) {
                                nvmap.FreeHandle(backing, true);
                                result = Kernel::ResultOutOfResource;
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

void IGraphicsSupervisor::ConfigureNativeWindowImagePool(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const bool create = ctx.GetCommand() == 315;
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == (create ? 0x14bU : 0x14aU) &&
        packet[1] == (create ? 2U : 1U)) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        if (m_native_images.Get(packet[2])) {
            const u32 count = create ? packet[3] : 0;
            result = m_native_images.ConfigureWindowPool(packet[2], count, create)
                ? ResultSuccess : Kernel::ResultInvalidState;
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::DestroyNativeImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1be && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        const auto image = m_native_images.Get(packet[2]);
        if (image && m_native_module && !image->window_pool) {
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


void IGraphicsSupervisor::CalculateChatImagePoolSize(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::span<const u32> preference{packet + 2, 16};
    const u32 count = packet[18];
    const auto layout =
        packet[1] == 17 ? CalculateChatCameraImageLayout(preference, count) : std::nullopt;
    const Result result = layout ? ResultSuccess : Kernel::ResultInvalidCombination;
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result.raw);
    rb.Push(layout ? static_cast<u32>(layout->total_bytes) : 0u);
    rb.Push(layout ? static_cast<u32>(layout->total_bytes >> 32) : 0u);
    // Keep malformed guest block-height values from reaching a host shift.
    const u32 block_log2 = preference[4] <= 8 ? preference[4] : 7;
    const u64 alignment = layout ? std::max<u64>(4096, 64ULL << block_log2) : 0;
    rb.Push(static_cast<u32>(alignment));
    rb.Push(static_cast<u32>(alignment >> 32));
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
