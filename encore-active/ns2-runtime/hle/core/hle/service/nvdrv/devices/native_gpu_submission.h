// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Pure native GP FIFO envelope guard. Actual GPU work is scheduled by
// nvhost_gpu only after this validation succeeds.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace Service::Nvidia::Devices {
// Guest CPU pointers are NOT the GPU virtual addresses used by the native
// GMMU. Check all kickoff headers in their owning process's CPU map before
// reading them. A zero-entry fence submission legitimately has no pointer.
template <typename Readable>
[[nodiscard]] constexpr bool NativeGpfifoGuestHeadersReadable(
    std::uint64_t address, std::uint32_t count, std::size_t entry_size,
    Readable&& readable) {
    if (count == 0) return true;
    if (address == 0 || entry_size == 0 ||
        count > std::numeric_limits<std::size_t>::max() / entry_size)
        return false;
    const std::size_t bytes = static_cast<std::size_t>(count) * entry_size;
    if (address > std::numeric_limits<std::uint64_t>::max() - bytes)
        return false;
    return readable(address, bytes);
}
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
    // When both native count and the ordinary two-increment fence are set,
    // enqueue BOTH counts. The old conditional reserved the combined value
    // in IncrementSyncpointMaxExt but silently dropped the native increments,
    // leaving the guest waiting on a fence that could never be signalled.
    const std::uint32_t native_count =
        (flags & 0x100U) ? requested_fence_increment : 0;
    if (native_count > 64) return {};
    return NativeSubmissionPlan{
        native_count,
        (flags & 2U ? 2U : 0U) + (flags & 0x100U ? requested_fence_increment : 0U)};
}
// nvgpu channel SETUP_BIND (Switch 2 H:0x21) is not an unconditional
// allocation acknowledgement. This host executes kernel-mode GPFIFO lists
// through its real ChannelState/GMMU. User-mode USERD/doorbell submissions
// need additional guest-mapped buffers and must fail closed until implemented.
struct NativeKernelChannelSetupRequest {
    std::uint32_t num_entries{};
    std::uint32_t num_inflight_jobs{};
    std::uint32_t flags{};
    std::int32_t userd_fd{};
    std::int32_t gpfifo_fd{};
    std::uint64_t userd_offset{};
    std::uint64_t gpfifo_offset{};
};
enum class NativeKernelChannelSetupStatus : std::uint8_t {
    Valid, Invalid, Unsupported,
};
[[nodiscard]] constexpr NativeKernelChannelSetupStatus ValidateNativeKernelChannelSetup(
    const NativeKernelChannelSetupRequest& request) {
    // NVIDIA requires a power-of-two ring with at least four entries for
    // kernel-mode submits; reject absurd/overflowed guest counts.
    if (request.num_entries < 4 || request.num_entries > (1U << 28) ||
        (request.num_entries & (request.num_entries - 1)) != 0)
        return NativeKernelChannelSetupStatus::Invalid;
    // The emulator's existing virtual-channel submit API has no user-mode
    // doorbell nor kernel-side inflight-job budget; do not advertise either.
    if (request.flags != 0 || request.num_inflight_jobs != 0 ||
        (request.userd_fd != 0 && request.userd_fd != -1) ||
        (request.gpfifo_fd != 0 && request.gpfifo_fd != -1) ||
        request.userd_offset != 0 || request.gpfifo_offset != 0)
        return NativeKernelChannelSetupStatus::Unsupported;
    return NativeKernelChannelSetupStatus::Valid;
}

} // namespace Service::Nvidia::Devices
