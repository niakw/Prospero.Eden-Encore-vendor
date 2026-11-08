// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/hle/service/am/service/graphics_video_decoder_session.h"
#include "core/hle/service/am/service/native_video_eos.h"
#include "core/hle/service/am/service/native_video_work_buffer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_server_session.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/graphics_supervisor.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/syncpoint_manager.h"
#include "video_core/textures/decoders.h"

namespace Service::AM {
namespace {
constexpr u32 NoData = 0x1c286f; // nn::codec Acquire: output queue is empty
constexpr u32 MaxSyncpoints = 192;
constexpr std::size_t MaxInputBytes = 32U * 1024 * 1024;
constexpr u32 MaxDimension = 8192;
}

IGraphicsVideoDecoderSession::IGraphicsVideoDecoderSession(
    Core::System& system_, std::shared_ptr<IGraphicsSupervisor> owner, bool creator)
    : ServiceFramework{system_, creator ? "nx2:vdec" : "nx2:vdecsvc"},
      m_owner{std::move(owner)}, m_context{system_, "nx2:vdec"},
      m_work_buffer{system_.Kernel()}, m_creator{creator} {
    static const FunctionInfo functions[] = {
        {1, &IGraphicsVideoDecoderSession::CreateDecoderService, "CreateVideoDecoderService"},
        {3, &IGraphicsVideoDecoderSession::GetEvent, "GetInputReadyEvent"},
        {6, &IGraphicsVideoDecoderSession::Start, "Start"},
        {8, &IGraphicsVideoDecoderSession::Stop, "Flush"},
        {101, &IGraphicsVideoDecoderSession::Configure, "Configure"},
        {109, &IGraphicsVideoDecoderSession::SubmitInput, "SubmitInput"},
        {115, &IGraphicsVideoDecoderSession::Finalize, "Finalize"},
        {200, &IGraphicsVideoDecoderSession::Initialize, "Initialize"},
        {201, &IGraphicsVideoDecoderSession::Initialize, "InitializeWithFlag"},
        {202, &IGraphicsVideoDecoderSession::Initialize, "InitializeWithVersion"},
        {207, &IGraphicsVideoDecoderSession::Stop, "Stop"},
        {212, &IGraphicsVideoDecoderSession::UnregisterBuffer, "UnregisterOutputImage"},
        {213, &IGraphicsVideoDecoderSession::GetOutputInfo, "GetOutputInfo"},
        {311, &IGraphicsVideoDecoderSession::RegisterBuffer, "RegisterOutputImage"},
        {314, &IGraphicsVideoDecoderSession::ReleaseBuffer, "ReleaseOutputImage"},
        {400, &IGraphicsVideoDecoderSession::SubmitInput, "SubmitInputWithInfo"},
    };
    RegisterHandlersTipc(functions);
}

IGraphicsVideoDecoderSession::~IGraphicsVideoDecoderSession() {
    // CoreTiming may execute off-thread. Detach before unscheduling, then
    // close the event: the callback owns only shared ready-state, never this.
    m_ready->Detach();
    if (m_ready_timer) system.CoreTiming().UnscheduleEvent(m_ready_timer);
    std::shared_ptr<Nvidia::Module> module;
    {
        const std::scoped_lock lock{m_owner->m_native_memory_mutex};
        module = m_owner->m_native_module;
    }
    if (m_syncpoint != NoSyncpoint && module)
        module->GetContainer().GetSyncpointManager().FreeSyncpoint(m_syncpoint);
    if (m_event) m_context.CloseEvent(m_event);
}

Result IGraphicsVideoDecoderSession::HandleSyncRequest(
    Kernel::KServerSession& session, HLERequestContext& ctx) {
    if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand()))
        return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
    const auto guard = LockService();
    InvokeRequestTipc(ctx);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
    return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
}

void IGraphicsVideoDecoderSession::Acknowledge(HLERequestContext& ctx, Result result) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsVideoDecoderSession::CreateDecoderService(HLERequestContext& ctx) {
    if (!m_creator || m_finalized) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidState.raw);
        return;
    }
    m_owner->OpenVideoDecoderChildSession(ctx);
}

void IGraphicsVideoDecoderSession::Initialize(HLERequestContext& ctx) {
    if (m_creator || m_finalized || m_initialized) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    // These SDK methods supply a real work-buffer transfer memory handle.
    // An arbitrary or expired handle must never initialize a decoder.
    auto memory = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(0));
    if (!VideoDecoderProtocol::IsWorkBufferOwned(memory.GetPointerUnsafe(),
                                                  m_owner->m_process)) {
        Acknowledge(ctx, Kernel::ResultInvalidHandle);
        return;
    }
    if (!VideoDecoderProtocol::IsValidWorkBufferSize(memory->GetSize())) {
        Acknowledge(ctx, Kernel::ResultInvalidSize);
        return;
    }
    // Retain an actual kernel KAutoObject reference, not just the raw handle.
    m_work_buffer = std::move(memory);
    m_initialized = true;
    Acknowledge(ctx, ResultSuccess);
}

void IGraphicsVideoDecoderSession::Configure(HLERequestContext& ctx) {
    if (m_creator || !m_initialized || m_finalized || m_started) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    const auto* w = ctx.CommandBuffer();
    if (w[5] == 0 || w[6] == 0 || w[5] > MaxDimension || w[6] > MaxDimension ||
        ctx.BufferDescriptorA().empty()) {
        Acknowledge(ctx, Kernel::ResultInvalidSize);
        return;
    }
    const auto bytes = ctx.ReadBufferA(0);
    if (bytes.empty() || bytes.size() > 128) {
        Acknowledge(ctx, Kernel::ResultInvalidSize);
        return;
    }
    const std::string_view name{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    const auto end = name.find('\0');
    const std::string_view mime = name.substr(0, end);
    if (mime != "video/vp9" && mime != "video/vp8" && mime != "video/avc" &&
        mime != "video/h264" && mime != "video/av01" && mime != "video/av1" &&
        mime != "video/hevc" && mime != "video/h265" &&
        mime != "video/hev1" && mime != "video/hvc1") {
        Acknowledge(ctx, Kernel::ResultNotImplemented);
        return;
    }
    auto decoder = std::make_unique<Tegra::Host1x::Nx2VideoDecoder>(mime);
    if (!decoder->IsOpen()) {
        Acknowledge(ctx, Kernel::ResultNotImplemented);
        return;
    }
    m_decoder = std::move(decoder);
    Acknowledge(ctx, ResultSuccess);
}

void IGraphicsVideoDecoderSession::Start(HLERequestContext& ctx) {
    if (m_creator || m_finalized || !m_initialized || !m_decoder ||
        !m_decoder->IsOpen()) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    m_started = true;
    m_ready->Set(true, m_pending.size() < MaxFrames);
    m_ready->Notify();
    Acknowledge(ctx, ResultSuccess);
}

void IGraphicsVideoDecoderSession::Stop(HLERequestContext& ctx) {
    if (m_creator || m_finalized || !m_initialized) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    m_pending.clear();
    if (m_decoder) m_decoder->Reset();
    if (ctx.GetCommand() == 207) m_started = false;
    m_ready->Set(m_started, m_pending.size() < MaxFrames);
    if (!m_started && m_event) m_event->Clear(system.Kernel());
    // No fake fence completion: buffered output without a decoded image
    // cannot become a successful GPU presentation on Flush or Stop.
    Acknowledge(ctx, ResultSuccess);
}

void IGraphicsVideoDecoderSession::Finalize(HLERequestContext& ctx) {
    if (m_creator || m_finalized) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    m_started = false;
    m_ready->Set(false, false);
    // Finalize is terminal even if the guest keeps this IPC session handle
    // open. Stop the 4ms ready-event pulse now, rather than leaving it
    // scheduled until destruction. The callback owns only shared ready-state.
    m_ready->Detach();
    if (m_ready_timer) {
        system.CoreTiming().UnscheduleEvent(m_ready_timer);
        m_ready_timer.reset();
    }
    if (m_event) m_event->Clear(system.Kernel());
    m_pending.clear();
    m_decoder.reset();
    m_work_buffer.Reset(nullptr);
    m_finalized = true;
    Acknowledge(ctx, ResultSuccess);
}

void IGraphicsVideoDecoderSession::GetEvent(HLERequestContext& ctx) {
    if (m_creator || m_finalized) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidState.raw);
        return;
    }
    if (!m_event) {
        m_event = m_context.CreateEvent("NS2VideoInputReady");
        if (m_event) {
            auto* event = m_event;
            m_ready->Attach([event, &kernel = system.Kernel()] {
                event->Signal(kernel);
            });
            // The guest uses an auto-clearing wait handle: a second wait
            // must be satisfied while there is still input ring capacity.
            m_ready_timer = Core::Timing::CreateEvent(
                "NS2VideoInputReadyPulse",
                [ready = m_ready](s64, std::chrono::nanoseconds)
                    -> std::optional<std::chrono::nanoseconds> {
                    ready->Notify();
                    return std::nullopt;
                });
            constexpr auto Interval = std::chrono::milliseconds{4};
            system.CoreTiming().ScheduleLoopingEvent(Interval, Interval, m_ready_timer);
        }
    }
    if (!m_event) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultOutOfResource.raw);
        return;
    }
    m_ready->Set(m_started, m_pending.size() < MaxFrames);
    m_ready->Notify();
    IPC::ResponseBuilder rb{ctx, 2, 1, 0};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, m_event->GetReadableEvent());
}

bool IGraphicsVideoDecoderSession::EnsureSyncpoint() {
    if (m_syncpoint != NoSyncpoint) return true;
    std::shared_ptr<Nvidia::Module> module;
    {
        const std::scoped_lock lock{m_owner->m_native_memory_mutex};
        module = m_owner->m_native_module;
    }
    if (!module) return false;
    const u32 id = module->GetContainer().GetSyncpointManager().AllocateSyncpoint(false);
    if (id >= MaxSyncpoints) return false;
    m_syncpoint = id;
    m_reserved_value = system.Host1x().GetSyncpointManager().GetGuestSyncpointValue(id);
    return true;
}

std::optional<std::array<u32, 4>> IGraphicsVideoDecoderSession::ReserveFence() {
    if (!EnsureSyncpoint() || m_reserved_value == std::numeric_limits<u32>::max()) return {};
    return std::array<u32, 4>{~m_syncpoint, 0U, ++m_reserved_value, 0U};
}

void IGraphicsVideoDecoderSession::RegisterBuffer(HLERequestContext& ctx) {
    const auto image = ctx.CommandBuffer()[2];
    if (m_creator || !m_initialized || m_finalized || !m_owner->LookupVideoSurface(image) ||
        m_registered.contains(image)) {
        Acknowledge(ctx, Kernel::ResultInvalidHandle);
        return;
    }
    const auto fence = ReserveFence();
    if (!fence) {
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    // Both owning containers can allocate. Do not publish a reserved
    // completion fence for a surface that failed to enter the queue.
    try {
        m_registered.insert(image);
        m_available.push_back({image, std::nullopt});
    } catch (const std::bad_alloc&) {
        m_registered.erase(image);
        --m_reserved_value;
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    for (const u32 word : *fence) rb.Push(word);
    Pump();
}

void IGraphicsVideoDecoderSession::ReleaseBuffer(HLERequestContext& ctx) {
    const auto* w = ctx.CommandBuffer();
    const u32 image = w[2];
    if (m_creator || m_finalized || !m_registered.contains(image) ||
        !m_guest_owned.contains(image) || !m_owner->LookupVideoSurface(image)) {
        Acknowledge(ctx, Kernel::ResultInvalidHandle);
        return;
    }
    const u32 release_id = ~w[4];
    const u32 release_value = w[6];
    if ((release_id >= MaxSyncpoints &&
         !(release_id == NoSyncpoint && release_value == 0)) ||
        (release_id == m_syncpoint &&
         !system.Host1x().GetSyncpointManager().IsReadyGuest(release_id, release_value))) {
        Acknowledge(ctx, Kernel::ResultInvalidHandle);
        return;
    }
    const auto fence = ReserveFence();
    if (!fence) {
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    // Allocate the return-to-decoder queue entry BEFORE surrendering the
    // guest's ownership. On OOM it keeps its image and release fence, and
    // the just-reserved completion value is rolled back.
    try {
        m_available.push_back({image, release_id == NoSyncpoint
            ? std::nullopt : std::optional<ReleaseFence>{{release_id, release_value}}});
    } catch (const std::bad_alloc&) {
        --m_reserved_value;
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    m_guest_owned.erase(image);
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    for (const u32 word : *fence) rb.Push(word);
    Pump();
}

void IGraphicsVideoDecoderSession::UnregisterBuffer(HLERequestContext& ctx) {
    const u32 image = ctx.CommandBuffer()[2];
    if (!m_registered.contains(image) || m_creator || m_finalized) {
        Acknowledge(ctx, Kernel::ResultInvalidHandle);
        return;
    }
    // An image with an undelivered output still belongs to the guest's
    // decoder queue, even after Stop; never silently discard that output.
    if (std::any_of(m_outputs.begin(), m_outputs.end(),
                    [=](const Output& a) { return a.image == image; })) {
        Acknowledge(ctx, Kernel::ResultBusy);
        return;
    }
    const auto available = std::find_if(
        m_available.begin(), m_available.end(),
        [=](const AvailableImage& a) { return a.id == image; });
    if (available != m_available.end()) {
        // Stop clears pending decode work and relinquishes decoder ownership.
        // Without this case, an image registered before Start or retained
        // after Stop could NEVER be unregistered. Do not bypass a guest
        // release fence or remove an image from a running decoder.
        if (m_started ||
            (available->release &&
             !system.Host1x().GetSyncpointManager().IsReadyGuest(
                 available->release->id, available->release->value))) {
            Acknowledge(ctx, Kernel::ResultBusy);
            return;
        }
        m_available.erase(available);
    }
    m_registered.erase(image);
    m_guest_owned.erase(image);
    Acknowledge(ctx, ResultSuccess);
}

void IGraphicsVideoDecoderSession::Pump() {
    if (!m_started || m_syncpoint == NoSyncpoint || !m_decoder) return;
    auto& syncpoints = system.Host1x().GetSyncpointManager();
    while (!m_available.empty() && !m_pending.empty() &&
           m_outputs.size() < MaxOutputs) {
        // A blocked release fence protects its own image, not every other
        // registered surface. Select the oldest READY image to avoid
        // head-of-line starvation while retaining each guest-owned fence.
        const auto ready = std::find_if(m_available.begin(), m_available.end(),
            [&](const AvailableImage& image) {
                return !image.release ||
                    syncpoints.IsReadyGuest(image.release->id, image.release->value);
            });
        if (ready == m_available.end())
            return; // No image may be overwritten before its release fence.
        const u32 image_id = ready->id;
        const auto target = m_owner->LookupVideoSurface(image_id);
        if (!target) {
            // Pool 912 can retire an image while the decoder still has its
            // registered ID in the ready queue. Unregister that dead image
            // instead of permanently blocking every other ready surface.
            // No frame was written, so neither advance a completion fence
            // nor consume the queued decoded picture.
            m_available.erase(ready);
            m_registered.erase(image_id);
            continue;
        }
        const auto& next = *ready;
        const auto& frame = m_pending.front();
        const u32 width = std::min(frame.picture.width, target->width);
        const u32 height = std::min(frame.picture.height, target->height);
        if (!width || !height || !frame.picture.frame) return;
        // Reject pathological surfaces and allocation failures without
        // signalling completion for a frame that was never written.
        constexpr std::size_t MaxImageBytes = 256U * 1024 * 1024;
        if (target->layout.image_bytes > MaxImageBytes) return;
        std::vector<u8> pixels;
        try {
            pixels.resize(target->layout.image_bytes);
        } catch (const std::bad_alloc&) {
            return;
        }
        const auto format = target->layout.cpu_kind == 10
            ? Tegra::Host1x::Nx2PictureFormat::Nv12
            : target->layout.cpu_kind == 9 ? Tegra::Host1x::Nx2PictureFormat::Bgra8
                                           : Tegra::Host1x::Nx2PictureFormat::Rgba8;
        if (!m_decoder->Render(frame.picture, format, width, height,
                               target->layout.row_bytes, target->layout.chroma_offset, pixels))
            return;
        if (target->gpu_mapped && !target->layout.nv12) {
            // A GPU 426 image is consumed in Maxwell block-linear RGB layout,
            // unlike the pitch-linear 961 CPU image. Convert ALL allocated
            // rows, not just the cropped frame, before invalidating textures.
            constexpr u32 BytesPerPixel = 4;
            constexpr u32 BlockHeightLog2 = 4; // 16 GOB rows (128 pixels)
            const u32 pitch = target->layout.row_bytes;
            if (!pitch || (pitch % BytesPerPixel) != 0 ||
                Tegra::Texture::CalculateSize(true, BytesPerPixel, pitch / BytesPerPixel,
                                             target->height, 1, BlockHeightLog2, 0) > pixels.size())
                return;
            std::vector<u8> tiled;
            try {
                tiled.resize(pixels.size());
            } catch (const std::bad_alloc&) {
                return;
            }
            Tegra::Texture::SwizzleTexture(tiled, pixels, BytesPerPixel,
                                           pitch / BytesPerPixel, target->height, 1,
                                           BlockHeightLog2, 0);
            pixels.swap(tiled);
        }
        // Stage fallible host bookkeeping BEFORE writing to guest memory.
        // If deque/set allocation fails after the write, the old code could
        // publish pixels without an output record or its completion fence.
        try {
            m_outputs.push_back({next.id, width, height, frame.picture.pts, frame.eos});
            try {
                m_guest_owned.insert(next.id);
            } catch (const std::bad_alloc&) {
                m_outputs.pop_back();
                return;
            }
        } catch (const std::bad_alloc&) {
            return;
        }
        if (!m_owner->WriteVideoSurface(next.id, *target, pixels)) {
            m_guest_owned.erase(next.id);
            m_outputs.pop_back();
            return;
        }
        m_available.erase(ready);
        m_pending.pop_front();
        syncpoints.IncrementGuest(m_syncpoint); // Only after real guest buffer was written.
    }
    m_ready->SetRoom(m_pending.size() < MaxFrames);
    m_ready->Notify();
}

void IGraphicsVideoDecoderSession::SubmitInput(HLERequestContext& ctx) {
    if (m_creator || !m_started || m_finalized || !m_decoder ||
        !m_decoder->IsOpen()) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    const auto* w = ctx.CommandBuffer();
    // EOS-only packets may contain no compressed A buffer. The SDK still
    // carries PTS and the end-of-stream byte in three normal data words.
    const std::size_t words = 2 + 3 * ctx.BufferDescriptorA().size();
    if (w[0] != ctx.GetCommand() + 0x10 || w[1] < 3 ||
        words + 2 >= IPC::COMMAND_BUFFER_LENGTH) {
        Acknowledge(ctx, Kernel::ResultInvalidCombination);
        return;
    }
    if (m_submitted == std::numeric_limits<u64>::max()) {
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    const u64 pts = u64{w[words]} | (u64{w[words + 1]} << 32);
    const bool eos = (w[words + 2] & 0xff) != 0;
    if (pts > static_cast<u64>(std::numeric_limits<s64>::max())) {
        Acknowledge(ctx, Kernel::ResultInvalidArgument);
        return;
    }
    const std::span<const u8> compressed = ctx.BufferDescriptorA().empty()
        ? std::span<const u8>{} : ctx.ReadBufferA(0);
    if (compressed.size() > MaxInputBytes || (compressed.empty() && !eos)) {
        Acknowledge(ctx, Kernel::ResultInvalidSize);
        return;
    }
    // A full queue can still accept an empty EOS packet to mark its final
    // pending frame; it cannot accept more compressed access units.
    if (m_pending.size() >= MaxFrames && !compressed.empty()) {
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    std::vector<Tegra::Host1x::Nx2Picture> decoded;
    if ((!compressed.empty() && !m_decoder->Decode(compressed, pts, decoded)) ||
        (eos && !m_decoder->Drain(decoded))) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    if (decoded.size() > MaxFrames - m_pending.size()) {
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    // An EOS packet can drain a delayed frame, or it can only mark the last
    // genuine image already queued. With no image, report an explicit error:
    // fabricating a completed GPU fence here would hang or corrupt clients.
    if (eos && decoded.empty() &&
        !VideoDecoderProtocol::MarkLastEos(m_pending, m_outputs)) {
        Acknowledge(ctx, Kernel::ResultNotFound);
        return;
    }
    // Publish the decoded packet atomically into the bounded output queue.
    // An allocation failure after the first frame must not leave half of the
    // packet visible to GetOutputInfo while SubmitInput reports no success.
    const std::size_t previous_pending = m_pending.size();
    try {
        for (std::size_t i = 0; i < decoded.size(); ++i)
            m_pending.push_back({std::move(decoded[i]), eos && i + 1 == decoded.size()});
    } catch (const std::bad_alloc&) {
        while (m_pending.size() > previous_pending)
            m_pending.pop_back();
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    Pump();
    m_ready->SetRoom(m_pending.size() < MaxFrames);
    m_ready->Notify();
    ++m_submitted;
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push(m_submitted);
}

void IGraphicsVideoDecoderSession::GetOutputInfo(HLERequestContext& ctx) {
    // Stop(207) preserves genuine, already-rendered outputs: a caller must
    // be able to drain them after stopping before UnregisterBuffer can
    // release the pool. Refusing every query while stopped deadlocked the
    // image lifecycle (output stuck + unregister returns Busy). No new
    // decoding is permitted after Stop, and an empty stopped queue retains
    // the original InvalidState contract.
    if (m_creator || m_finalized || (!m_started && m_outputs.empty())) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    Pump();
    if (m_outputs.empty()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.PushRaw<u32>(NoData);
        return;
    }
    const auto output = m_outputs.front();
    m_outputs.pop_front();
    Pump(); // A full output queue can now accept another genuine frame.
    std::array<u32, 16> words{};
    const u64 pts_ns = output.pts > std::numeric_limits<u64>::max() / 1000
        ? std::numeric_limits<u64>::max() : output.pts * 1000;
    words[0] = output.image;
    words[1] = output.width;
    words[2] = output.height;
    words[3] = output.width;
    words[4] = output.height;
    words[5] = 0;
    words[6] = static_cast<u32>(pts_ns);
    words[7] = static_cast<u32>(pts_ns >> 32);
    words[8] = output.eos ? 1U : 0U;
    IPC::ResponseBuilder rb{ctx, 18};
    rb.Push(ResultSuccess);
    for (const auto value : words) rb.Push(value);
}
} // namespace Service::AM
