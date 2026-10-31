// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Pure native GP FIFO envelope guard. Actual GPU work is scheduled by
// nvhost_gpu only after this validation succeeds.
#include <cstdint>
#include <optional>

namespace Service::Nvidia::Devices {
struct NativeSubmissionPlan {
    std::uint32_t completion_count;
    std::uint32_t increment_total;
};
[[nodiscard]] constexpr std::optional<NativeSubmissionPlan> PlanNativeSubmission(
    std::uint32_t flags, std::uint32_t requested_fence_increment,
    bool channel_ready) {
    // Known bits: wait, increment fence, hw-format, suppress WFI, native
    // completion count in original increment_value bit.
    constexpr std::uint32_t AllowedFlags = 0x117U;
    if (!channel_ready || (flags & ~AllowedFlags) ||
        ((flags & 1U) && (flags & 0x100U)))
        return {};
    const std::uint32_t native_count =
        (flags & 0x100U) && !(flags & 2U) ? requested_fence_increment : 0;
    if (native_count > 64) return {};
    return NativeSubmissionPlan{
        native_count,
        (flags & 2U ? 2U : 0U) + (flags & 0x100U ? requested_fence_increment : 0U)};
}
} // namespace Service::Nvidia::Devices
