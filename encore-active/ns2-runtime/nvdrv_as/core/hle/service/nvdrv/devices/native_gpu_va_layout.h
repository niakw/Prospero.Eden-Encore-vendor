// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Validate an NS2 guest GPU VA layout before narrowing its page indices to
// FlatAllocator<u32>. Both allocator limits are ABSOLUTE GPU page addresses.
#include <cstdint>
#include <limits>
#include <optional>

namespace Service::Nvidia::Devices {
// Switch 2's GPU-only render targets can be allocated at 0x81_0000_0000,
// above the 37-bit Maxwell default. Keep these constants exclusive to the
// explicitly created NS2 GPU session; legacy NVDRV retains its original VA.
inline constexpr std::uint64_t NativeNs2GpuVaStart = 0x08000000ULL;
inline constexpr std::uint64_t NativeNs2GpuVaSmallSplit = 3ULL << 38; // 768 GiB
inline constexpr std::uint64_t NativeNs2GpuVaEnd = 1ULL << 40; // 1 TiB

struct NativeGpuVaPageLayout {
    std::uint32_t small_begin;
    std::uint32_t small_end;
    std::uint32_t big_begin;
    std::uint32_t big_end;
};

[[nodiscard]] constexpr std::optional<NativeGpuVaPageLayout> PlanNativeGpuVaLayout(
    std::uint64_t start, std::uint64_t split, std::uint64_t end,
    std::uint32_t big_page_size) {
    constexpr std::uint64_t small_page_size = 0x1000;
    if (big_page_size != 0x10000 && big_page_size != 0x20000) return std::nullopt;
    if (start == 0 || start >= split || split >= end ||
        (start & (small_page_size - 1)) != 0 ||
        (split & (big_page_size - 1)) != 0 ||
        (end & (big_page_size - 1)) != 0)
        return std::nullopt;

    const auto small_begin = start / small_page_size;
    const auto small_end = split / small_page_size;
    const auto big_begin = split / big_page_size;
    const auto big_end = end / big_page_size;
    constexpr auto max_index = std::numeric_limits<std::uint32_t>::max();
    if (small_begin > max_index || small_end > max_index ||
        big_begin > max_index || big_end > max_index)
        return std::nullopt;
    return NativeGpuVaPageLayout{
        static_cast<std::uint32_t>(small_begin),
        static_cast<std::uint32_t>(small_end),
        static_cast<std::uint32_t>(big_begin),
        static_cast<std::uint32_t>(big_end),
    };
}
static_assert(PlanNativeGpuVaLayout(NativeNs2GpuVaStart, NativeNs2GpuVaSmallSplit,
                                    NativeNs2GpuVaEnd, 0x20000).has_value());
static_assert(NativeNs2GpuVaStart < 0x8100000000ULL &&
              0x8100000000ULL < NativeNs2GpuVaSmallSplit);
} // namespace Service::Nvidia::Devices
