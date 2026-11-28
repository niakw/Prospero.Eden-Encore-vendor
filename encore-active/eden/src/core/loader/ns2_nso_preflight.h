// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N3q: opt-in NS2 ExeFS admission guard for the existing Eden NSO0 loader.
// Checks the exact on-disk 0x100-byte NSO0 header BEFORE the core loader
// allocates its code set, compressed staging buffers or enters guest memory.
// Not a Nintendo Switch 2 NSO variant decoder: the legacy NSO0 layout only.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Eden::NS2::ExeFS {
constexpr std::size_t NsoHeaderBytes = 0x100;
constexpr std::uint64_t MaxImageBytes = 512ull * 1024 * 1024;
constexpr std::uint64_t MaxSegmentBytes = 256ull * 1024 * 1024;

enum class NsoPreflightStatus {
    Ready, TruncatedHeader, InvalidMagic, UnsupportedVersion,
    InvalidFlags, InvalidSegment, InvalidFileRange, InvalidImageLayout,
};

struct NsoPreflightResult {
    NsoPreflightStatus status{NsoPreflightStatus::TruncatedHeader};
    std::uint64_t image_bytes{};
    [[nodiscard]] constexpr bool Ready() const {
        return status == NsoPreflightStatus::Ready;
    }
};

[[nodiscard]] constexpr std::uint32_t NsoWord(std::span<const std::uint8_t> data,
                                               std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

[[nodiscard]] constexpr NsoPreflightResult PreflightNso(
    std::span<const std::uint8_t> bytes, std::uint64_t file_bytes) {
    if (bytes.size() < NsoHeaderBytes || file_bytes < NsoHeaderBytes)
        return {NsoPreflightStatus::TruncatedHeader, 0};
    if (NsoWord(bytes, 0) != 0x304f534eU) // 'NSO0'
        return {NsoPreflightStatus::InvalidMagic, 0};
    if (NsoWord(bytes, 4) != 0)
        return {NsoPreflightStatus::UnsupportedVersion, 0};
    const auto flags = NsoWord(bytes, 12);
    // ZBIC uses the high bit in Encore's native loader (Switch 22.0+).
    if ((flags & ~0xbfU) != 0 || ((flags & 0x80U) && !(flags & 0x7U)))
        return {NsoPreflightStatus::InvalidFlags, 0};

    std::uint64_t end_image = 0;
    std::uint64_t previous_virtual_end = 0;
    for (std::size_t index = 0; index < 3; ++index) {
        const std::size_t offset = 0x10 + index * 0x10;
        const std::uint64_t file_offset = NsoWord(bytes, offset);
        const std::uint64_t virtual_offset = NsoWord(bytes, offset + 4);
        const std::uint64_t uncompressed = NsoWord(bytes, offset + 8);
        const std::uint64_t stored = NsoWord(bytes, 0x60 + index * 4);
        const bool compressed = ((flags >> index) & 1) != 0;
        if (uncompressed > MaxSegmentBytes || stored > MaxSegmentBytes ||
            ((uncompressed == 0) != (stored == 0)) ||
            (uncompressed && !compressed && stored < uncompressed))
            return {NsoPreflightStatus::InvalidSegment, 0};
        if (stored && (file_offset < NsoHeaderBytes || file_offset > file_bytes ||
                       stored > file_bytes - file_offset))
            return {NsoPreflightStatus::InvalidFileRange, 0};
        if (virtual_offset < previous_virtual_end ||
            virtual_offset > MaxImageBytes ||
            uncompressed > MaxImageBytes - virtual_offset)
            return {NsoPreflightStatus::InvalidImageLayout, 0};
        previous_virtual_end = virtual_offset + uncompressed;
        end_image = previous_virtual_end;
    }
    const std::uint64_t bss_bytes = NsoWord(bytes, 0x3c);
    if (bss_bytes > MaxImageBytes - end_image)
        return {NsoPreflightStatus::InvalidImageLayout, 0};
    end_image += bss_bytes;
    // The real loader rounds allocations to a page; reserve room for it.
    if (end_image > MaxImageBytes - 0x4000)
        return {NsoPreflightStatus::InvalidImageLayout, 0};
    return {NsoPreflightStatus::Ready, end_image};
}
} // namespace Eden::NS2::ExeFS
