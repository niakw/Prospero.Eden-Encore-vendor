// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Native Ampere graphics CBUF reads: 64-KiB hardware window, 49-bit GPU VA.
// Does not change the common Maxwell/NS1 CBUF reader.
#include <cstdint>

namespace VideoCommon {
[[nodiscard]] constexpr bool NativeGraphicsCbufWordAdmissible(
    bool enabled, std::uint64_t base, std::uint64_t declared_bytes,
    std::uint32_t byte_offset) noexcept {
    constexpr std::uint64_t va_limit = std::uint64_t{1} << 49;
    constexpr std::uint32_t window_bytes = 0x10000U;
    constexpr std::uint32_t word_bytes = sizeof(std::uint32_t);
    return enabled && base != 0 && base < va_limit &&
           (byte_offset & (word_bytes - 1)) == 0 &&
           byte_offset <= window_bytes - word_bytes &&
           declared_bytes >= word_bytes &&
           byte_offset <= declared_bytes - word_bytes &&
           byte_offset <= va_limit - base &&
           word_bytes <= va_limit - base - byte_offset;
}
} // namespace VideoCommon
