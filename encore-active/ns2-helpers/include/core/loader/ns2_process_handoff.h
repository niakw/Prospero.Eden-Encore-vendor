// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N3u: bounded, read-only NSO -> KProcess text handoff probe.
// The production ExeFS owner supplies the *actual* process memory reader.
// Compressed or patched code must not be compared to original file bytes.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include "core/loader/ns2_nso_preflight.h"

namespace Eden::NS2::ExeFS {
enum class TextHandoffStatus {
    Verified,
    SkippedCompressed,
    SkippedEmptyText,
    InvalidHeader,
    InvalidGuestAddress,
    FileReadFailed,
    GuestReadFailed,
    Mismatch,
};

template <typename FileReader, typename GuestReader>
[[nodiscard]] TextHandoffStatus VerifyUncompressedTextHandoff(
    std::span<const std::uint8_t> header, std::uint64_t file_size,
    std::uint64_t module_base, FileReader&& read_file, GuestReader&& read_guest) {
    if (!PreflightNso(header, file_size).Ready())
        return TextHandoffStatus::InvalidHeader;
    if ((NsoWord(header, 0x0c) & 1U) != 0)
        return TextHandoffStatus::SkippedCompressed;
    const auto file_offset = static_cast<std::uint64_t>(NsoWord(header, 0x10));
    const auto text_offset = static_cast<std::uint64_t>(NsoWord(header, 0x14));
    const auto text_size = static_cast<std::uint64_t>(NsoWord(header, 0x18));
    if (!text_size)
        return TextHandoffStatus::SkippedEmptyText;

    // Sample exactly the first 16 original AArch64 text bytes, never a whole
    // guest image. NSO0 preflight already checked the file range.
    const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(text_size, 16));
    if (text_offset > std::numeric_limits<std::uint64_t>::max() - module_base)
        return TextHandoffStatus::InvalidGuestAddress;
    const std::uint64_t guest_addr = module_base + text_offset;
    if (count > std::numeric_limits<std::uint64_t>::max() - guest_addr)
        return TextHandoffStatus::InvalidGuestAddress;
    std::array<std::uint8_t, 16> expected{};
    std::array<std::uint8_t, 16> actual{};
    if (read_file(file_offset, expected.data(), count) != count)
        return TextHandoffStatus::FileReadFailed;
    if (!read_guest(guest_addr, actual.data(), count))
        return TextHandoffStatus::GuestReadFailed;
    return std::memcmp(expected.data(), actual.data(), count) == 0
               ? TextHandoffStatus::Verified : TextHandoffStatus::Mismatch;
}
} // namespace Eden::NS2::ExeFS
