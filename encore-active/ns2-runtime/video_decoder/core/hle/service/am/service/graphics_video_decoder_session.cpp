// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/hle/service/am/service/graphics_video_decoder_session.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

#include "core/core.h"
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
      m_owner{std::move(owner)}, m_context{system_, "nx2:vdec"}, m_creator{creator} {
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
    const auto memory = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(0));
    if (memory.IsNull()) {
        Acknowledge(ctx, Kernel::ResultInvalidHandle);
        return;
    }
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
        mime != "video/h264") {
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
    if (m_event) m_event->Signal(system.Kernel());
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
    m_pending.clear();
    m_decoder.reset();
    m_finalized = true;
    Acknowledge(ctx, ResultSuccess);
}

void IGraphicsVideoDecoderSession::GetEvent(HLERequestContext& ctx) {
    if (m_creator || m_finalized) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultInvalidState.raw);
        return;
    }
    if (!m_event) m_event = m_context.CreateEvent("NS2VideoInputReady");
    if (!m_event) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultOutOfResource.raw);
        return;
    }
    if (m_started && m_pending.size() < MaxFrames) m_event->Signal(system.Kernel());
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
    m_registered.insert(image);
    m_available.push_back({image, std::nullopt});
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
    m_guest_owned.erase(image);
    m_available.push_back({image, release_id == NoSyncpoint
        ? std::nullopt : std::optional<ReleaseFence>{{release_id, release_value}}});
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
    if (std::any_of(m_available.begin(), m_available.end(),
                    [=](const AvailableImage& a) { return a.id == image; }) ||
        std::any_of(m_outputs.begin(), m_outputs.end(),
                    [=](const Output& a) { return a.image == image; })) {
        Acknowledge(ctx, Kernel::ResultBusy);
        return;
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
        const auto& next = m_available.front();
        if (next.release && !syncpoints.IsReadyGuest(next.release->id, next.release->value))
            return; // Never overwrite an output image still read by its consumer.
        const auto target = m_owner->LookupVideoSurface(next.id);
        if (!target) return;
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
        if (!m_owner->WriteVideoSurface(next.id, *target, pixels)) return;
        m_outputs.push_back({next.id, width, height, frame.picture.pts, frame.eos});
        m_guest_owned.insert(next.id);
        m_available.pop_front();
        m_pending.pop_front();
        syncpoints.IncrementGuest(m_syncpoint); // Only after real guest buffer was written.
    }
    if (m_event && m_pending.size() < MaxFrames) m_event->Signal(system.Kernel());
}

void IGraphicsVideoDecoderSession::SubmitInput(HLERequestContext& ctx) {
    if (m_creator || !m_started || m_finalized || !m_decoder ||
        !m_decoder->IsOpen() || ctx.BufferDescriptorA().empty()) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    const auto* w = ctx.CommandBuffer();
    const std::size_t words = 2 + 3 * ctx.BufferDescriptorA().size();
    if (words + 2 >= IPC::COMMAND_BUFFER_LENGTH || m_pending.size() >= MaxFrames ||
        m_submitted == std::numeric_limits<u64>::max()) {
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    const auto compressed = ctx.ReadBufferA(0);
    if (compressed.empty() || compressed.size() > MaxInputBytes) {
        Acknowledge(ctx, Kernel::ResultInvalidSize);
        return;
    }
    const u64 pts = u64{w[words]} | (u64{w[words + 1]} << 32);
    const bool eos = (w[words + 2] & 0xff) != 0;
    std::vector<Tegra::Host1x::Nx2Picture> decoded;
    if (!m_decoder->Decode(compressed, pts, decoded) ||
        (eos && !m_decoder->Drain(decoded))) {
        Acknowledge(ctx, Kernel::ResultInvalidState);
        return;
    }
    if (decoded.size() > MaxFrames - m_pending.size()) {
        Acknowledge(ctx, Kernel::ResultOutOfResource);
        return;
    }
    for (std::size_t i = 0; i < decoded.size(); ++i)
        m_pending.push_back({std::move(decoded[i]), eos && i + 1 == decoded.size()});
    Pump();
    if (m_event && m_pending.size() < MaxFrames)
        m_event->Signal(system.Kernel());
    ++m_submitted;
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push(m_submitted);
}

void IGraphicsVideoDecoderSession::GetOutputInfo(HLERequestContext& ctx) {
    if (m_creator || !m_started || m_finalized) {
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
