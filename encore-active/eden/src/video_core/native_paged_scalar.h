// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One 4 KiB physical-backend page is the maximum safe contiguous view of an
// arbitrary guest GPU scalar. A 64-bit access at page offset 4095 may map its
// final seven bytes to a DIFFERENT physical allocation. Never memcpy through
// one translated pointer across that boundary.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace Eden::GpuScalar {

inline constexpr std::uint64_t kBackingPageBytes = 4096;

template <typename T, typename Translate>
[[nodiscard]] bool Read(std::uint64_t address, T& result, Translate&& translate) {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= 8);
    result = {};
    if (address > (std::numeric_limits<std::uint64_t>::max)() - (sizeof(T) - 1))
        return false;
    const std::size_t first_bytes = (std::min)(
        sizeof(T), static_cast<std::size_t>(
            kBackingPageBytes - (address & (kBackingPageBytes - 1))));
    const std::size_t tail_bytes = sizeof(T) - first_bytes;
    const auto* first = translate(address);
    // Resolve both mappings BEFORE reading a partial value; failed second
    // translations must not be masked by a contiguous-looking first page.
    const auto* second = tail_bytes ? translate(address + first_bytes) : first;
    if (!first || !second)
        return false;
    T staged{};
    auto* out = reinterpret_cast<std::uint8_t*>(&staged);
    std::memcpy(out, first, first_bytes);
    // A 1-byte scalar cannot cross a 4 KiB page. Compile that impossible
    // tail out entirely so fortified GCC never treats it as an out-of-bounds
    // write on a one-byte object. Wider reads still verify both mappings.
    if constexpr (sizeof(T) > 1) {
        if (tail_bytes)
            std::memcpy(out + first_bytes, second, tail_bytes);
    }
    result = staged;
    return true;
}

template <typename T, typename Translate>
[[nodiscard]] bool Write(std::uint64_t address, T value, Translate&& translate) {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= 8);
    if (address > (std::numeric_limits<std::uint64_t>::max)() - (sizeof(T) - 1))
        return false;
    const std::size_t first_bytes = (std::min)(
        sizeof(T), static_cast<std::size_t>(
            kBackingPageBytes - (address & (kBackingPageBytes - 1))));
    const std::size_t tail_bytes = sizeof(T) - first_bytes;
    auto* first = translate(address);
    auto* second = tail_bytes ? translate(address + first_bytes) : first;
    // No partially published scalar when its second page is unmapped.
    if (!first || !second)
        return false;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    std::memcpy(first, bytes, first_bytes);
    // For u8, a second-page copy is impossible. Do not instantiate a
    // pointer past the 1-byte source object: GCC 13 -O2 correctly rejects
    // the potentially overreading memcpy even though tail_bytes is zero.
    if constexpr (sizeof(T) > 1) {
        if (tail_bytes)
            std::memcpy(second, bytes + first_bytes, tail_bytes);
    }
    return true;
}

} // namespace Eden::GpuScalar
