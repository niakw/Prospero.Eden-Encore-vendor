// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental Switch 2 NPDM layout probe. No game data, keys or firmware.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>

namespace Eden::NS2 {

struct AcidLocation {
    std::uint64_t absolute_offset{};
    bool extended{};
};

// Subtraction-based bounds checks avoid overflow from untrusted metadata.
constexpr bool ContainsRange(std::uint64_t offset, std::uint64_t size,
                             std::uint64_t capacity) noexcept {
    return offset <= capacity && size <= capacity - offset;
}

// ACID signature/key prefix is 0x200 bytes on Switch 1 and 0x300 on Switch 2.
// Prefer the legacy layout when both validate. Callback verifies complete body.
template <typename ValidAcidBody>
std::optional<AcidLocation> FindAcidBody(std::uint64_t acid_offset,
                                         std::uint64_t acid_size,
                                         std::uint64_t file_size,
                                         std::uint64_t header_size,
                                         ValidAcidBody&& valid_body) {
    if (acid_offset < 0x80 || !ContainsRange(acid_offset, acid_size, file_size)) {
        return std::nullopt;
    }
    for (const std::uint64_t prefix : {std::uint64_t{0x200}, std::uint64_t{0x300}}) {
        if (!ContainsRange(prefix, header_size, acid_size)) {
            continue;
        }
        const std::uint64_t body = acid_offset + prefix; // safe after full ACID range check
        if (valid_body(body)) {
            return AcidLocation{body, prefix == 0x300};
        }
    }
    return std::nullopt;
}

} // namespace Eden::NS2
