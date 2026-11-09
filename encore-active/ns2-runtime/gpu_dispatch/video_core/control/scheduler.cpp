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
    std::shared_ptr<ChannelState> channel_state;
    {
        std::unique_lock lk(scheduling_guard);
        auto it = channels.find(channel);
        // A recycled or unknown channel ID must never be dereferenced or
        // enqueue GPU work on a former process epoch. Reject before binding
        // or acknowledging any submitted guest commands.
        if (it == channels.end() || !it->second || !it->second->payload)
            throw std::runtime_error("Native GPFIFO: missing channel owner");
        channel_state = it->second;
        gpu.BindChannel(channel_state->bind_id);
    }
    // Process commands outside the lock to reduce contention.
    // Multiple channels can prepare their commands in parallel.
    auto& pusher = channel_state->payload->dma_pusher;
    pusher.Push(std::move(entries));
    pusher.DispatchCalls();
    if (pusher.HasPendingNativeAcquire() &&
        std::find(pending_native_channels.begin(), pending_native_channels.end(),
                  channel_state) == pending_native_channels.end()) {
        pending_native_channels.push_back(channel_state);
    }
    if (!pending_native_channels.empty()) {
        ResumePendingNative(gpu);
    }
}

void Scheduler::ResumePendingNative(GPU& gpu) {
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
    std::unique_lock lk(scheduling_guard);
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
