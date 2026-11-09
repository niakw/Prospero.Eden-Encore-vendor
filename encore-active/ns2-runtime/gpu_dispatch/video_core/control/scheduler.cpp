// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <memory>
#include <stdexcept>

#include "common/assert.h"
#include "core/core.h"
#include "video_core/control/channel_state.h"
#include "video_core/control/scheduler.h"
#include "video_core/gpu.h"

namespace Tegra::Control {

void Scheduler::Push(GPU& gpu, s32 channel, CommandList&& entries) {
    {
        // Hold a SHARED channel epoch lease through submission. Channel
        // re-registration must not retire the old owner while a GPU worker
        // is still dispatching its commands. Independent readers retain
        // concurrency; only replacement needs the exclusive lease.
        std::shared_lock epoch_lock(scheduling_guard);
        const auto it = channels.find(channel);
        if (it == channels.end() || !it->second || !it->second->payload)
            throw std::runtime_error("Native GPFIFO: missing channel owner");
        const auto channel_state = it->second;
        auto& pusher = channel_state->payload->dma_pusher;
        // GPU::BindChannel mutates a single shared GPU execution context.
        // Concurrent native channel submissions and semaphore replay must
        // not interleave a bind with another owner's DispatchCalls.
        {
            const std::scoped_lock gpu_lock(gpu_submit_guard);
            gpu.BindChannel(channel_state->bind_id);
            pusher.Push(std::move(entries));
            pusher.DispatchCalls();
        }
        // A second GPU-worker path may retry parked semaphores while this
        // submission publishes its pending channel. Guard the actual list.
        const std::scoped_lock pending_lock(pending_guard);
        if (pusher.HasPendingNativeAcquire() &&
            std::find(pending_native_channels.begin(), pending_native_channels.end(),
                      channel_state) == pending_native_channels.end()) {
            pending_native_channels.push_back(channel_state);
        }
    }
    // Do not hold the scheduling lease while replaying other channels.
    if (HasPendingNativeChannels())
        ResumePendingNative(gpu);
}

void Scheduler::ResumePendingNative(GPU& gpu) {
    // Lock GPU execution BEFORE the pending list, matching Push's order:
    // dispatch from a parked channel cannot race another channel's bind.
    const std::scoped_lock gpu_lock(gpu_submit_guard);
    // No stale-generation retire may mutate the pending vector while a
    // worker is retrying or dispatching a parked native GPFIFO channel.
    const std::scoped_lock pending_lock(pending_guard);
    // Another channel may release an earlier channel in this same pass.
    // Never wait inside a DMA submission: the PS5 GPU worker remains free
    // to process other guest queues, and the idle worker also retries.
    for (bool progress = true; progress;) {
        progress = false;
        for (const auto& state : pending_native_channels) {
            auto& pusher = state->payload->dma_pusher;
            if (!pusher.system.IsPoweredOn()) {
                return;
            }
            if (!pusher.HasPendingNativeAcquire() ||
                !pusher.ResumeNativeAcquire()) {
                continue;
            }
            gpu.BindChannel(state->bind_id);
            pusher.DispatchCalls();
            progress = true;
        }
    }
    std::erase_if(pending_native_channels, [](const auto& state) {
        return !state->payload->dma_pusher.HasPendingNativeAcquire();
    });
}

void Scheduler::DeclareChannel(std::shared_ptr<ChannelState> new_channel) {
    if (!new_channel || !new_channel->payload)
        throw std::runtime_error("Native GPFIFO: invalid channel owner");
    const s32 channel = new_channel->bind_id;
    std::unique_lock epoch_lock(scheduling_guard);
    const std::scoped_lock pending_lock(pending_guard);
    const auto existing = channels.find(channel);
    if (existing != channels.end()) {
        if (existing->second == new_channel)
            return; // Same live owner: never discard its pending wait.
        // A driver can reuse the bind ID after process/channel teardown.
        // The old generation must not resume a parked semaphore against
        // the new owner's GPU binding, or publish an obsolete guest fence.
        const auto retired = existing->second;
        std::erase(pending_native_channels, retired);
        existing->second = std::move(new_channel);
    } else {
        channels.emplace(channel, std::move(new_channel));
    }
}

} // namespace Tegra::Control
