// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <string_view>
#include <vector>

#include <fmt/ranges.h>

#include "common/string_util.h"
#include "audio_core/audio_core.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/memory.h"
#include "core/hle/service/audio/errors.h"
#include "core/hle/service/audio/nx2_sysmix.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

struct Nx2SysmixStreams {
    explicit Nx2SysmixStreams(Core::System& system_) : system{system_} {
        timer = Core::Timing::CreateEvent("sysmix output", [this](s64, std::chrono::nanoseconds late) {
            std::unique_lock lock{mutex, std::try_to_lock};
            if (lock) {
                for (auto& [id, stream] : streams) {
                    stream->Pump();
                }
            }
            const auto interval = std::chrono::nanoseconds{std::chrono::milliseconds{5}};
            const auto missed = std::max(late, std::chrono::nanoseconds::zero()) / interval;
            return std::optional<std::chrono::nanoseconds>{interval * (missed + 1)};
        });
        system.CoreTiming().ScheduleLoopingEvent(std::chrono::milliseconds{5},
                                                 std::chrono::milliseconds{5}, timer);
    }

    ~Nx2SysmixStreams() {
        system.CoreTiming().UnscheduleEvent(timer);
    }

    // A request may name several stream IDs. Authenticate every ID and
    // process BEFORE changing any host stream, so a later invalid item
    // cannot leave an earlier stream spuriously playing. Retain the
    // registry mutex through the transition: service-domain CloseStream
    // cannot remove an owner between admission and the operation.
    Result SetEngagedBatch(std::span<const u32> ids, bool engaged,
                           const Kernel::KProcess* process) {
        R_UNLESS(process, ResultInvalidHandle);
        R_UNLESS(ids.size() <= 32, ResultInvalidArgument);
        std::array<std::shared_ptr<SysmixOutput>, 32> selected{};
        std::array<bool, 32> original{};
        std::scoped_lock lock{mutex};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (std::find(ids.begin(), ids.begin() + i, ids[i]) != ids.begin() + i)
                return ResultInvalidArgument;
            const auto it = streams.find(ids[i]);
            R_UNLESS(it != streams.end(), ResultNotFound);
            R_UNLESS(it->second && it->second->IsOwnedBy(process), ResultInvalidHandle);
            const u32 state = it->second->GetState();
            R_UNLESS(state != 0, ResultOperationFailed);
            original[i] = state == 2;
            selected[i] = it->second;
        }
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const Result result = selected[i]->SetEngaged(engaged);
            if (result.IsFailure()) {
                // The sink may reject Start or Stop. Best-effort rollback
                // of earlier accepted operations; never advertise success
                // for a partially failed batch.
                for (std::size_t j = i; j > 0; --j)
                    (void)selected[j - 1]->SetEngaged(original[j - 1]);
                return result;
            }
        }
        return ResultSuccess;
    }

    Result SetEngaged(u32 id, bool engaged, const Kernel::KProcess* process) {
        return SetEngagedBatch(std::span<const u32>{&id, 1}, engaged, process);
    }

    // Reserve and publish an ID under ONE critical section. The previous
    // check-unlock-allocate-lock-insert sequence allowed two concurrent
    // sessions to pass the 32-stream cap; the timer could observe the
    // intermediate state. A failed factory/insert never consumes an ID.
    template <typename Factory>
    Result CreateBounded(Factory&& factory, u32& out_id,
                         std::shared_ptr<SysmixOutput>& out_stream) {
        std::scoped_lock lock{mutex};
        R_UNLESS(streams.size() < 32 && next_id != 0, ResultOutOfSessions);
        const u32 candidate = next_id;
        auto stream = factory(candidate);
        R_UNLESS(stream, ResultOperationFailed);
        R_UNLESS(streams.emplace(candidate, stream).second, ResultOutOfSessions);
        ++next_id;
        out_id = candidate;
        out_stream = std::move(stream);
        return ResultSuccess;
    }

    std::mutex mutex;
    std::map<u32, std::shared_ptr<SysmixOutput>> streams;
    u32 next_id{1};
    Core::System& system;
    std::shared_ptr<Core::Timing::EventType> timer;
};

class INx2SysmixStreamSession final : public ServiceFramework<INx2SysmixStreamSession> {
public:
    INx2SysmixStreamSession(Core::System& system_, std::shared_ptr<Nx2SysmixStreams> streams_,
                           std::shared_ptr<SysmixOutput> stream_, u32 id_)
        : ServiceFramework{system_, "nx2:audsmx:u:stream"}, streams{std::move(streams_)},
          stream{std::move(stream_)}, id{id_} {
        static const FunctionInfo functions[] = {
            {0, &INx2SysmixStreamSession::OpenStream, "OpenStream"},
            {1, &INx2SysmixStreamSession::CloseStream, "CloseStream"},
            {2, &INx2SysmixStreamSession::GetStreamState, "GetStreamState"},
            {3, &INx2SysmixStreamSession::GetStreamGain, "GetStreamGain"},
            {4, &INx2SysmixStreamSession::SetStreamGain, "SetStreamGain"},
            {5, &INx2SysmixStreamSession::AcquireStateChangeEvent, "AcquireStreamStateChangeEvent"},
            {6, &INx2SysmixStreamSession::Succeed, "ReleaseStreamStateChangeEvent"},
            {7, &INx2SysmixStreamSession::AcquireDataEvent, "AcquireStreamDataEvent"},
            {8, &INx2SysmixStreamSession::ReleaseDataEvent, "ReleaseStreamDataEvent"},
            {9, &INx2SysmixStreamSession::Unsupported, "ClockStream"},
            {10, &INx2SysmixStreamSession::Unsupported, "PrimeStream"},
            {11, &INx2SysmixStreamSession::Unsupported, "PurgeStream"},
            {12, &INx2SysmixStreamSession::Unsupported, "RenameStream (not implemented)"},
        };
        RegisterHandlers(functions);
    }

    ~INx2SysmixStreamSession() override {
        {
            std::scoped_lock lock{streams->mutex};
            streams->streams.erase(id);
        }
        stream->Close();
    }

private:
    void OpenStream(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto sample_type = rp.Pop<u32>();
        const auto channels = rp.Pop<u32>();
        const auto sample_rate = rp.Pop<u32>();
        const auto frames = rp.Pop<u32>();
        const auto prefill = rp.Pop<u32>();
        const auto flags = rp.Pop<u32>();
        const auto result = stream->Open(sample_type, channels, sample_rate, frames, prefill, flags);
        LOG_INFO(Service_Audio,
                 "stream {} open: sample type {}, {} channels, {} Hz, {} frames, prefill {}, flags {:#x}, result {:#x}",
                 id, sample_type, channels, sample_rate, frames, prefill, flags, result.raw);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result);
    }

    void CloseStream(HLERequestContext& ctx) {
        stream->Close();
        Succeed(ctx);
    }

    void GetStreamState(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(stream->GetState());
    }

    void GetStreamGain(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(stream->GetGain());
    }

    void SetStreamGain(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto gain = rp.Pop<f32>();
        rp.Pop<u32>();
        const auto ramp = rp.Pop<s64>();
        const auto result = stream->SetGain(gain, ramp);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result);
    }

    void AcquireStateChangeEvent(HLERequestContext& ctx) {
        auto* event = stream->GetStateEvent();
        IPC::ResponseBuilder rb{ctx, 2, event ? 1u : 0u};
        rb.Push(event ? ResultSuccess : ResultOutOfSessions);
        if (event) {
            rb.PushCopyObjects(ctx, event);
        }
    }

    void AcquireDataEvent(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto mode = rp.Pop<u32>();
        const auto threshold = rp.Pop<s32>();
        const auto result = stream->SetDataNotification(mode, threshold);
        IPC::ResponseBuilder rb{ctx, 2, result.IsSuccess() ? 1u : 0u};
        rb.Push(result);
        if (result.IsSuccess()) {
            rb.PushCopyObjects(ctx, stream->GetDataEvent());
        }
    }

    void ReleaseDataEvent(HLERequestContext& ctx) {
        const auto result = stream->SetDataNotification(0, 0);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result);
    }

    void Unsupported(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNotSupported);
    }

    void Succeed(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    std::shared_ptr<Nx2SysmixStreams> streams;
    std::shared_ptr<SysmixOutput> stream;
    u32 id;
};

class INx2SysmixUserSession final : public ServiceFramework<INx2SysmixUserSession> {
public:
    INx2SysmixUserSession(Core::System& system_, std::shared_ptr<Nx2SysmixStreams> streams_,
                         Kernel::KProcess* process_)
        : ServiceFramework{system_, "nx2:audsmx:u:session"}, streams{std::move(streams_)},
          process{system.Kernel(), process_} {
        static const FunctionInfo functions[] = {
            {1, &INx2SysmixUserSession::CreateStream, "CreateStream"},
            {2, &INx2SysmixUserSession::EngageStreams, "EngageStreams"},
            {3, &INx2SysmixUserSession::DisengageStreams, "DisengageStreams"},
        };
        RegisterHandlers(functions);
    }

private:
    void CreateStream(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto device = rp.Pop<u32>();
        const auto flags = rp.Pop<u32>();
        [[maybe_unused]] const auto applet_resource_user_id = rp.Pop<u64>();
        const auto buffer = rp.Pop<u64>();
        const auto size = rp.Pop<u64>();
        Result result = ResultInvalidArgument;
        if (device == 1 && ctx.GetThread().GetOwnerProcess() == process.GetPointerUnsafe()) {
            result = SysmixOutput::ValidateRegion(*process, buffer, size, flags);
        }
        if (result.IsFailure()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(result);
            return;
        }
        u32 id{};
        std::shared_ptr<SysmixOutput> stream;
        Result admitted = ResultOutOfSessions;
        try {
            admitted = streams->CreateBounded(
                [&](u32 candidate) {
                    return std::make_shared<SysmixOutput>(
                        system, *process, system.AudioCore().GetOutputSink(),
                        candidate, buffer, size, flags);
                }, id, stream);
        } catch (const std::bad_alloc&) {
            // The map and next_id stay unchanged when allocation fails.
            admitted = ResultOutOfSessions;
        }
        if (admitted.IsFailure()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(admitted);
            return;
        }
        LOG_INFO(Service_Audio, "stream {} '{}': flags {:#x}, buffer {:#x} size {:#x}", id,
                 Common::StringFromBuffer(ctx.ReadBuffer(0)), flags, buffer, size);
        IPC::ResponseBuilder rb{ctx, 3, 0, 1};
        rb.Push(ResultSuccess);
        rb.Push(id);
        rb.PushIpcInterface<INx2SysmixStreamSession>(ctx, system, streams, stream, id);
    }

    void SetEngaged(HLERequestContext& ctx, bool engaged) {
        const auto buffer = ctx.ReadBuffer(0);
        Result result = ResultInvalidArgument;
        if (buffer.size() % sizeof(u32) == 0 && buffer.size() <= 32 * sizeof(u32)) {
            std::array<u32, 32> ids{};
            const auto count = buffer.size() / sizeof(u32);
            for (std::size_t i = 0; i < count; ++i)
                std::memcpy(&ids[i], buffer.data() + i * sizeof(u32), sizeof(u32));
            result = streams->SetEngagedBatch(
                std::span<const u32>{ids.data(), count}, engaged,
                ctx.GetThread().GetOwnerProcess());
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result);
    }

    void EngageStreams(HLERequestContext& ctx) {
        SetEngaged(ctx, true);
    }

    void DisengageStreams(HLERequestContext& ctx) {
        SetEngaged(ctx, false);
    }

    std::shared_ptr<Nx2SysmixStreams> streams;
    Kernel::KScopedAutoObject<Kernel::KProcess> process;
};

class INx2AudioSoundMixerService final : public ServiceFramework<INx2AudioSoundMixerService> {
public:
    INx2AudioSoundMixerService(Core::System& system_, std::shared_ptr<Nx2SysmixStreams> streams_)
        : ServiceFramework{system_, "nx2:audsmx:u"}, streams{std::move(streams_)} {
        static const FunctionInfo functions[] = {
            {0, &INx2AudioSoundMixerService::OpenUserSession, "OpenUserSession"},
        };
        RegisterHandlers(functions);
    }

private:
    void OpenUserSession(HLERequestContext& ctx) {
        auto process = ctx.GetObjectFromHandle<Kernel::KProcess>(ctx.GetCopyHandle(0));
        if (process.IsNull() || process.GetPointerUnsafe() != ctx.GetThread().GetOwnerProcess()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultInvalidHandle);
            return;
        }
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<INx2SysmixUserSession>(ctx, system, streams, process.GetPointerUnsafe());
    }

    std::shared_ptr<Nx2SysmixStreams> streams;
};

} // namespace Service::Audio
