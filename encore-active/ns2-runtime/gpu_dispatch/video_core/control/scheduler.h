// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include "common/container/unordered_map.h"

#include "video_core/dma_pusher.h"

namespace Tegra {

class GPU;

namespace Control {

struct ChannelState;

class Scheduler {
public:
    void Push(GPU& gpu, s32 channel, CommandList&& entries);

    void DeclareChannel(std::shared_ptr<ChannelState> new_channel);

    [[nodiscard]] bool HasPendingNativeChannels() const noexcept {
        const std::scoped_lock lock(pending_guard);
        return !pending_native_channels.empty();
    }
    void ResumePendingNative(GPU& gpu);

private:
    ::Common::unordered_map<s32, std::shared_ptr<ChannelState>> channels;
    std::vector<std::shared_ptr<ChannelState>> pending_native_channels;
    std::shared_mutex scheduling_guard;
    mutable std::mutex pending_guard;
};

} // namespace Control

} // namespace Tegra
