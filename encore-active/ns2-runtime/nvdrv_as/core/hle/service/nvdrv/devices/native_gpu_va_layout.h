// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Validate an NS2 guest GPU VA layout before narrowing its page indices to
// FlatAllocator<u32>. Both allocator limits are ABSOLUTE GPU page addresses.
#include <cstdint>
#include <limits>
#include <optional>

namespace Service::Nvidia::Devices {
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
} // namespace Service::Nvidia::Devices
