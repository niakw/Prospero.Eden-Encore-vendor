// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <optional>

#include "common/common_types.h"

namespace Vulkan {

/// Frame Pacing Mode targets guest/game frames. Generated presents share the
/// same game-frame slot instead of each consuming a full 1/target_fps slot.
class FramePacer {
public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] std::optional<Clock::time_point> Next(Clock::time_point now, double target_fps,
                                                        u32 game_frame_presents) {
        if (last_target_fps != target_fps) {
            frame_interval = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1.0 / target_fps));
            max_frames = static_cast<u64>(0.1 * target_fps);
            last_target_fps = target_fps;
            frames = 0.0;
            start_time = now;
        }
        const u32 presents = game_frame_presents == 0 ? 1 : game_frame_presents;
        frames += 1.0 / static_cast<double>(presents);
        const auto target_time =
            start_time + std::chrono::duration_cast<Clock::duration>(frame_interval * frames);
        if (target_time >= now) {
            return target_time;
        }
        if (frames > static_cast<double>(max_frames)) {
            frames = 0.0;
            start_time = now;
        }
        return std::nullopt;
    }

private:
    Clock::duration frame_interval{};
    Clock::time_point start_time{};
    double last_target_fps{};
    u64 max_frames{};
    double frames{};
};

} // namespace Vulkan
