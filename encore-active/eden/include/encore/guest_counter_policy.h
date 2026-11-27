// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Encore shared guest AArch64 counter contract. It is selected from verified
// program metadata, not from file names or a per-game compatibility switch.
// Source: Eglantine nx2 0218eac (31.25 MHz guest CNTFRQ, 19.2 MHz on NS1).
#include <atomic>
#include <cstdint>

namespace Eden::GuestCounter {
inline constexpr std::uint64_t Switch1Hz = 19'200'000;
inline constexpr std::uint64_t Switch2Hz = 31'250'000;
inline constexpr std::uint64_t NanosecondsPerSecond = 1'000'000'000;
inline constexpr std::uint64_t GuestCpuHz = 1'020'000'000;

inline std::atomic<std::uint64_t> active_hz{Switch1Hz};
inline std::atomic<std::uint64_t> native_tsc_factor{0};

// A single native 64-bit multiply-high converts x86 TSC to guest CNTPCT.
// The caller must select the title before starting the guest CPU/JIT.
// PS5 uses x86-64; the factor is calculated once when validating NPDM.
inline void SelectProgram(bool extended_acid, std::uint64_t host_tsc_hz) noexcept {
    const std::uint64_t guest_hz = extended_acid ? Switch2Hz : Switch1Hz;
    const std::uint64_t factor = host_tsc_hz == 0 ? 0 :
        static_cast<std::uint64_t>(
            (static_cast<unsigned __int128>(guest_hz) << 64) / host_tsc_hz);
    native_tsc_factor.store(factor, std::memory_order_relaxed);
    active_hz.store(guest_hz, std::memory_order_release);
}
[[nodiscard]] inline std::uint64_t Rate() noexcept {
    return active_hz.load(std::memory_order_acquire);
}
[[nodiscard]] inline std::uint64_t NativeFactor() noexcept {
    return native_tsc_factor.load(std::memory_order_relaxed);
}
[[nodiscard]] inline std::uint64_t CpuTicksToGuest(std::uint64_t ticks) noexcept {
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(ticks) * Rate()) / GuestCpuHz);
}
[[nodiscard]] inline std::uint64_t NanosecondsToGuest(std::uint64_t ns) noexcept {
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(ns) * Rate()) / NanosecondsPerSecond);
}
} // namespace Eden::GuestCounter
