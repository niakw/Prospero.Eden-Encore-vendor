#include <algorithm>
#include <chrono>
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef PS5_NATIVE
#include "gpu_failure.h"
#include <stdexcept>
#endif
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/dma_pusher.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/kepler_memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/guest_memory.h"
#ifdef PS5_NATIVE
#include "core/hle/service/nvdrv/devices/native_gpu_channel_registry.h"
#include "core/hle/service/nvdrv/devices/native_gpu_fetch_admission.h"
#include "video_core/host1x/host1x.h"
#endif
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace Tegra {

constexpr u32 MacroRegistersStart = 0xE00;
constexpr u32 ComputeInline = 0x6D;

// Kepler registers are smaller than the GPU method field. Reject malformed
// writes BEFORE enqueueing them into EngineInterface::method_sink, whose
// ConsumeSinkImpl would otherwise index regs.reg_array out of bounds.
[[nodiscard]] constexpr bool IsKeplerRegisterMethodValid(Engines::EngineTypes engine, u32 method) {
    switch (engine) {
    case Engines::EngineTypes::KeplerCompute:
        return method < Engines::KeplerCompute::Regs::NUM_REGS;
    case Engines::EngineTypes::KeplerMemory:
        return method < Engines::KeplerMemory::Regs::NUM_REGS;
    default:
        return true;
    }
}

DmaPusher::DmaPusher(Core::System& system_, MemoryManager& memory_manager_, Control::ChannelState& channel_state_)
    : system{system_}
    , memory_manager{memory_manager_}
    , channel_state{channel_state_}
    , signal_sync{false}
    , synced{false}
{}

DmaPusher::~DmaPusher() = default;

#ifdef PS5_NATIVE
bool DmaPusher::ResumeNativeAcquire() {
    if (!puller.ResumeNativeAcquire(*this)) return false;
    if (dma_pushbuffer.empty()) return true;
    auto& pending = dma_pushbuffer.front().native_syncpoint_wait;
    if (!pending) return true;

    // Only the authenticated channel's admitted wait can park this list.
    // Recheck the source reservation around the live counter read: an ID
    // recycled by another channel must not satisfy an old dependency.
    if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
            &channel_state) || !pending->lease ||
        !pending->lease->load(std::memory_order_acquire)) {
        throw std::runtime_error("Native GPFIFO syncpoint wait owner retired");
    }

    // The guest counter can advance when commands are merely enqueued.
    // Only the Host1x HOST counter is incremented after real fence
    // retirement. Poll nonblocking, so other channels can submit work.
    rasterizer->ReleaseFences(false);
    const auto completed = system.Host1x().GetSyncpointManager().GetHostSyncpointValue(
        pending->id);
    if (!pending->lease->load(std::memory_order_acquire)) {
        throw std::runtime_error("Native GPFIFO syncpoint wait source recycled");
    }
    if (((completed - pending->threshold) & 0x80000000U) != 0)
        return false;
    pending.reset();
    return true;
}
#endif

void DmaPusher::DispatchCalls() {
#ifdef PS5_NATIVE
    if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
            &channel_state)) {
        dma_pushbuffer_subindex = 0; // Preserve exact NS1 submission behavior.
    }
#else
    dma_pushbuffer_subindex = 0;
#endif
    dma_state.is_last_call = true;
    while (system.IsPoweredOn()) {
        if (!Step()) {
            break;
        }
    }
    system.GPU().FlushCommands();
    system.GPU().OnCommandListEnd();
}

#ifdef PS5_NATIVE
[[noreturn]] void DmaPusher::FailNativeSubmission(const char* message) {
    const auto error = std::make_exception_ptr(std::runtime_error(message));
    ::Eden::RecordGpuFailure(error);
    Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().ReportFault(&channel_state);
    std::rethrow_exception(error);
}
#endif

bool DmaPusher::Step() {
    if (!ib_enable || dma_pushbuffer.empty()) {
        return false;
    }
#ifdef PS5_NATIVE
    const auto require_native_submission_lease = [this] {
        const auto& lease = dma_pushbuffer.front().native_submission_lease;
        // A revoked GSV epoch makes the registry ticket intentionally stale;
        // the per-packet lease is therefore the authoritative native marker.
        if (!lease) return;
        if (!lease->load(std::memory_order_acquire))
            FailNativeSubmission("Native GPFIFO address-space lease retired");
    };
    require_native_submission_lease();
    // The unresolved comparison stops this GPU channel without blocking the
    // worker, which can process releases from other channels in the meantime.
    if (!ResumeNativeAcquire()) {
        return false;
    }
#endif

    CommandList& command_list = dma_pushbuffer.front();

    const size_t prefetch_size = command_list.prefetch_command_list.size();
    const size_t command_list_size = command_list.command_lists.size();

    if (prefetch_size == 0 && command_list_size == 0) {
        dma_pushbuffer.pop();
        dma_pushbuffer_subindex = 0;
#ifdef PS5_NATIVE
        command_resume_offset = 0;
#endif
        return true;
    }

    if (prefetch_size > 0) {
#ifdef PS5_NATIVE
        if (command_resume_offset > prefetch_size) {
            throw std::runtime_error("Native GPFIFO prefetch resume cursor invalid");
        }
        const auto commands = std::span<const CommandHeader>{
            command_list.prefetch_command_list.data(), prefetch_size};
        require_native_submission_lease();
        ProcessCommands(commands.subspan(command_resume_offset));
        if (HasPendingNativeAcquire()) {
            return false;
        }
        command_resume_offset = 0;
#else
        ProcessCommands(command_list.prefetch_command_list);
#endif
        dma_pushbuffer.pop();
        return true;
    }

    auto& current_command = command_list.command_lists[dma_pushbuffer_subindex];
    const CommandListHeader& header = current_command;
    dma_state.dma_get = header.addr;

    if (signal_sync && !synced) {
        std::unique_lock lk(sync_mutex);
        while (!synced && !system.IsShuttingDown()) {
#ifdef PS5_NATIVE
            // A failed GPU worker cannot deliver this pending fence.
            if (auto failure = ::Eden::PeekGpuFailure())
                std::rethrow_exception(failure);
#endif
            sync_cv.wait_for(lk, std::chrono::milliseconds(10));
        }
#ifdef PS5_NATIVE
        // A failure published together with the final notification must not
        // be mistaken for a successful DMA fence acknowledgement.
        if (auto failure = ::Eden::PeekGpuFailure())
            std::rethrow_exception(failure);
#endif
        if (system.IsShuttingDown()) return false;
        signal_sync = false;
        synced = false;
#ifdef PS5_NATIVE
        require_native_submission_lease();
#endif
    }

    if (header.size > 0) {
#ifdef PS5_NATIVE
        // Command headers are validated by NVDRV at submission, but the
        // address space can change before the GPU worker consumes the live
        // command words. Revalidate after the inter-list fence wait and
        // before dirty-memory probes, SafeRead, or ProcessCommands. Never
        // acknowledge a revoked native GPFIFO as an all-zero pushbuffer.
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &channel_state) &&
            !Service::Nvidia::Devices::NativeGpfifoFetchMapped(
                dma_state.dma_get, static_cast<u64>(header.size),
                [this](u64 addr, u64 bytes) {
                    return memory_manager.IsFullyMappedRange(addr, bytes);
                })) {
            LOG_ERROR(HW_GPU, "Native GPFIFO command unmapped at fetch: address={:#x} words={}",
                      dma_state.dma_get, static_cast<u64>(header.size));
#ifdef PS5_NATIVE
            // Returning false silently finishes DispatchCalls; its caller
            // would then publish a completed GPU queue fence and process
            // later syncpoint increments for work that was never executed.
            // The maintained PS5 GPU pthread entry catches this exception,
            // records its fatal cause and requests worker stop before any
            // queue fence is signaled. No exception leaves the OS thread.
            throw std::runtime_error("Native GPFIFO command unmapped at GPU fetch");
#else
            return false;
#endif
        }

        const bool native_channel =
            Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &channel_state);
        if (native_channel && command_resume_offset == 0) {
            // A regular method payload may continue at split_method_end while
            // NVN2 calls a different, non-contiguous command buffer first.
            // Only park when that expected continuation is present later in
            // this same submission; macro payloads retain their existing path.
            const bool park_call =
                dma_state.method_count != 0 &&
                dma_state.method < MacroRegistersStart &&
                header.addr != split_method_end &&
                header.size != dma_state.method_count &&
                SplitResumesLater(command_list, dma_pushbuffer_subindex + 1);
            if (park_call || !parked_methods.empty()) {
                ResolveSplitMethod(header.addr, park_call);
            }
        }
#endif
        if (subchannels[dma_state.subchannel] && dma_state.method_count) {
            const auto engine = subchannel_type[dma_state.subchannel];
            const bool kepler_payload = engine == Engines::EngineTypes::KeplerCompute && dma_state.method == ComputeInline && dma_state.non_incrementing;
            const bool macro_payload = engine == Engines::EngineTypes::Maxwell3D && dma_state.method >= MacroRegistersStart;
            if (kepler_payload || macro_payload) {
                const size_t words = std::min<size_t>(dma_state.method_count, header.size);
                subchannels[dma_state.subchannel]->current_dirty = memory_manager.IsMemoryDirty(dma_state.dma_get, words * sizeof(u32));
            }
        }
        bool use_safe = Settings::IsDMALevelDefault() ? Settings::IsGPULevelHigh() : Settings::IsDMALevelSafe();
#ifdef PS5_NATIVE
        // P2.7: C7x0 native GPFIFO command words can have been produced by
        // an earlier GPU dispatch. A guest setting for UNSAFE DMA must never
        // bypass GPU cache coherency for an authenticated NS2 channel.
        // SafeRead flushes the actual guest GPU region before consuming the
        // live words, including the non-contiguous backing fallback.
        use_safe |= Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(&channel_state);
#endif
        if (use_safe) {
            Tegra::Memory::GpuGuestMemory<Tegra::CommandHeader, Tegra::Memory::GuestMemoryFlags::SafeRead>headers(memory_manager, dma_state.dma_get, header.size, &command_headers);
#ifdef PS5_NATIVE
            if (command_resume_offset > headers.size()) {
                throw std::runtime_error("Native GPFIFO entry shortened during acquire");
            }
            require_native_submission_lease();
            ProcessCommands(std::span<const CommandHeader>{
                headers.data(), headers.size()}.subspan(command_resume_offset));
#else
            ProcessCommands(headers);
#endif
        } else {
            Tegra::Memory::GpuGuestMemory<Tegra::CommandHeader, Tegra::Memory::GuestMemoryFlags::UnsafeRead>headers(memory_manager, dma_state.dma_get, header.size, &command_headers);
#ifdef PS5_NATIVE
            if (command_resume_offset > headers.size()) {
                throw std::runtime_error("Native GPFIFO entry shortened during acquire");
            }
            require_native_submission_lease();
            ProcessCommands(std::span<const CommandHeader>{
                headers.data(), headers.size()}.subspan(command_resume_offset));
#else
            ProcessCommands(headers);
#endif
        }
    }

#ifdef PS5_NATIVE
    if (HasPendingNativeAcquire()) {
        // Keep both the queue entry and its method state; completing the
        // entry now would wrongly acknowledge unexecuted guest commands.
        return false;
    }
    if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
            &channel_state) &&
        dma_state.method_count != 0) {
        split_method_end = header.addr + static_cast<GPUVAddr>(header.size) * sizeof(u32);
    }
    command_resume_offset = 0;
#endif
    if (++dma_pushbuffer_subindex >= command_list_size) {
#ifdef PS5_NATIVE
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &channel_state)) {
            // A parked method is valid only inside the submission that split
            // it. Ring addresses repeat; retaining it could consume a later
            // frame's command header as stale register data.
            parked_methods.clear();
        }
#endif
        dma_pushbuffer.pop();
        dma_pushbuffer_subindex = 0;
    } else {
        const bool explicit_sync = command_list.command_lists[dma_pushbuffer_subindex].sync;
        const bool user_sync = Settings::values.sync_memory_operations.GetValue();
#ifdef PS5_NATIVE
        // A native NS2 GPFIFO fence is part of the guest-visible ordering
        // contract, not an optional host quality preset. Its previous
        // Compute/3D writes must retire BEFORE the next live command list
        // is read, even if the user's NS1 sync optimization is disabled.
        signal_sync = explicit_sync &&
            (user_sync || Service::Nvidia::Devices::NativeGpuChannelRegistry::
                              Instance().IsNative(&channel_state));
#else
        signal_sync = explicit_sync && user_sync;
#endif
    }

    if (signal_sync) {
        rasterizer->SignalFence([this]() {
            std::scoped_lock lk(sync_mutex);
            synced = true;
            sync_cv.notify_all();
        });
    }

    return true;
}

void DmaPusher::ProcessCommands(std::span<const CommandHeader> commands) {
#ifdef PS5_NATIVE
    const size_t offset_before = command_resume_offset;
#endif
    for (size_t index = 0; index < commands.size();) {
#ifdef PS5_NATIVE
        // A fault can race a long pushbuffer after Step's boundary check.
        // One acquire load per decoded native word/batch prevents execution
        // from continuing after the AS health token is revoked.
        if (const auto& lease = dma_pushbuffer.front().native_submission_lease;
            lease && !lease->load(std::memory_order_acquire))
            FailNativeSubmission("Native GPFIFO address-space lease retired mid-dispatch");
#endif
        // Data word of methods command
        if (dma_state.method_count && dma_state.non_incrementing) {
            auto const& command_header = commands[index]; //must ref (MUltiMethod re)
            dma_state.dma_word_offset = u32(
#ifdef PS5_NATIVE
                offset_before +
#endif
                index) * sizeof(u32);
            u32 max_write = u32(std::min<std::size_t>(
                index + dma_state.method_count, commands.size()) - index);
#ifdef PS5_NATIVE
            // No puller batching across a native SEM_EXECUTE: it may park
            // the channel after its first word.
            if (dma_state.method < non_puller_methods &&
                Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                    &channel_state)) {
                max_write = 1;
            }
#endif
            CallMultiMethod(&command_header.argument, max_write);
            dma_state.method_count -= max_write;
            dma_state.is_last_call = true;
            index += max_write;
        } else if (dma_state.method_count) {
            auto const command_header = commands[index]; //can copy
            dma_state.dma_word_offset = u32(
#ifdef PS5_NATIVE
                offset_before +
#endif
                index) * sizeof(u32);
            dma_state.is_last_call = dma_state.method_count <= 1;
            CallMethod(command_header.argument);
            dma_state.method += !dma_state.non_incrementing ? 1 : 0;
            dma_state.non_incrementing |= dma_increment_once;
            dma_state.method_count--;
            index++;
        } else {
            auto const command_header = commands[index]; //can copy
            // No command active - this is the first word of a new one
            switch (command_header.mode) {
            case SubmissionMode::Increasing:
                SetState(command_header);
                dma_state.non_incrementing = false;
                dma_increment_once = false;
                break;
            case SubmissionMode::NonIncreasing:
                SetState(command_header);
                dma_state.non_incrementing = true;
                dma_increment_once = false;
                break;
            case SubmissionMode::Inline:
                dma_state.method = command_header.method;
                dma_state.subchannel = command_header.subchannel;
                dma_state.dma_word_offset = u64(-s64(dma_state.dma_get)); // negate to set address as 0
                CallMethod(command_header.arg_count);
                dma_state.non_incrementing = true;
                dma_increment_once = false;
                break;
            case SubmissionMode::IncreaseOnce:
                SetState(command_header);
                dma_state.non_incrementing = false;
                dma_increment_once = true;
                break;
            default:
                break;
            }
            index++;
        }
#ifdef PS5_NATIVE
        if (HasPendingNativeAcquire()) {
            // All words through the semaphore were consumed, but nothing
            // following it; begin exactly at this word after release.
            command_resume_offset = offset_before + index;
            return;
        }
#endif
    }
}

// NVN2 may split one register-write payload across GPFIFO entries while a
// different command buffer runs in between. Park only that authenticated
// native call boundary, then restore the exact unfinished method at the
// contiguous resume address. NS1 never calls this path.
void DmaPusher::ResolveSplitMethod(GPUVAddr entry_address, bool park) {
    const auto continued = std::find_if(
        parked_methods.begin(), parked_methods.end(),
        [entry_address](const ParkedMethod& candidate) {
            return candidate.resume_address == entry_address;
        });
    const bool can_park =
        park || (continued != parked_methods.end() && dma_state.method < MacroRegistersStart);

    ParkedMethod resume{};
    const bool has_resume = continued != parked_methods.end();
    if (has_resume) {
        resume = *continued;
        parked_methods.erase(continued);
    }

    if (can_park && dma_state.method_count != 0 && entry_address != split_method_end) {
        if (parked_methods.size() == MaxParkedMethods) {
            parked_methods.erase(parked_methods.begin());
        }
        parked_methods.push_back(ParkedMethod{
            .method = dma_state.method,
            .subchannel = dma_state.subchannel,
            .method_count = dma_state.method_count,
            .non_incrementing = dma_state.non_incrementing,
            .increment_once = dma_increment_once,
            .resume_address = split_method_end,
        });
        dma_state.method_count = 0;
        dma_increment_once = false;
    }

    if (has_resume && dma_state.method_count == 0) {
        dma_state.method = resume.method;
        dma_state.subchannel = resume.subchannel;
        dma_state.method_count = resume.method_count;
        dma_state.non_incrementing = resume.non_incrementing;
        dma_increment_once = resume.increment_once;
        split_method_end = entry_address;
    }
}

bool DmaPusher::SplitResumesLater(const CommandList& list, std::size_t from) const {
    for (std::size_t i = from; i < list.command_lists.size(); ++i) {
        if (list.command_lists[i].size > 0 &&
            list.command_lists[i].addr == split_method_end) {
            return true;
        }
    }
    return false;
}

void DmaPusher::SetState(const CommandHeader& command_header) {
    dma_state.method = command_header.method;
    dma_state.subchannel = command_header.subchannel;
    dma_state.method_count = command_header.method_count;
}

void DmaPusher::CallMethod(u32 argument) {
    if (dma_state.method < non_puller_methods) {
        puller.CallPullerMethod(*this, Engines::Puller::MethodCall{
            dma_state.method,
            argument,
            dma_state.subchannel,
            dma_state.method_count,
        });
    } else {
        auto subchannel = subchannels[dma_state.subchannel];
        if (!subchannel) {
            // A guest may submit methods before binding an engine. Never
            // dereference a missing subchannel or replay it as a valid method.
            LOG_ERROR(HW_GPU, "GPFIFO method {:#x} on unbound subchannel {}",
                      dma_state.method, dma_state.subchannel);
#ifdef PS5_NATIVE
            // Ignoring an unbound engine on an authenticated native queue
            // would let GPFIFO publish completion for commands never run.
            // An invalid SET_OBJECT already unbinds this channel: retire its
            // queue instead of manufacturing a successful guest fence.
            if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                    &channel_state)) {
                throw std::runtime_error("Native GPFIFO method on unbound subchannel");
            }
#endif
            return;
        }
        if (!IsKeplerRegisterMethodValid(subchannel_type[dma_state.subchannel], dma_state.method)) {
            LOG_ERROR(HW_GPU, "Invalid Kepler GPU method {:#x} on subchannel {}",
                      dma_state.method, dma_state.subchannel);
            return;
        }
        if (!subchannel->execution_mask[dma_state.method]) {
            subchannel->method_sink.emplace_back(dma_state.method, argument);
        } else {
            subchannel->ConsumeSink(system);
            subchannel->current_dma_segment = dma_state.dma_get + dma_state.dma_word_offset;
            subchannel->CallMethod(system, dma_state.method, argument, dma_state.is_last_call);
        }
    }
}

void DmaPusher::CallMultiMethod(const u32* base_start, u32 num_methods) {
    if (dma_state.method < non_puller_methods) {
        puller.CallMultiMethod(*this, dma_state.method, dma_state.subchannel, base_start, num_methods, dma_state.method_count);
    } else {
        auto subchannel = subchannels[dma_state.subchannel];
        if (!subchannel) {
            // A guest may submit methods before binding an engine. Never
            // dereference a missing subchannel or replay it as a valid method.
            LOG_ERROR(HW_GPU, "GPFIFO method {:#x} on unbound subchannel {}",
                      dma_state.method, dma_state.subchannel);
#ifdef PS5_NATIVE
            // Ignoring an unbound engine on an authenticated native queue
            // would let GPFIFO publish completion for commands never run.
            // An invalid SET_OBJECT already unbinds this channel: retire its
            // queue instead of manufacturing a successful guest fence.
            if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                    &channel_state)) {
                throw std::runtime_error("Native GPFIFO method on unbound subchannel");
            }
#endif
            return;
        }
        if (!IsKeplerRegisterMethodValid(subchannel_type[dma_state.subchannel], dma_state.method)) {
            LOG_ERROR(HW_GPU, "Invalid Kepler multi-method {:#x} on subchannel {}",
                      dma_state.method, dma_state.subchannel);
            return;
        }
        subchannel->ConsumeSink(system);
        subchannel->current_dma_segment = dma_state.dma_get + dma_state.dma_word_offset;
        subchannel->CallMultiMethod(system, dma_state.method, base_start, num_methods, dma_state.method_count);
    }
}

void DmaPusher::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
}

} // namespace Tegra
