// SPDX-License-Identifier: GPL-3.0-or-later
// R2 research-only typed staging-copy bounds; no GPU lifetime lease implied.
#pragma once
#include <cstdint>
#include <limits>
namespace Vulkan::NS2Research {
struct StagingUploadBounds {
    std::uint64_t staging_offset{}, mapped_bytes{}, staging_capacity{};
    std::uint64_t staging_binding_offset{}, destination_capacity{}, destination_binding_offset{};
    bool same_memory{};
};
struct StagingUploadCopy {
    std::uint64_t source_offset{}, destination_offset{}, bytes{};
};
[[nodiscard]] constexpr bool UploadRangeFits(std::uint64_t start, std::uint64_t bytes,
                                              std::uint64_t capacity) noexcept {
    return start <= capacity && bytes <= capacity - start;
}
[[nodiscard]] constexpr bool VerifyStagingUploadCopy(const StagingUploadBounds& b,
                                                     const StagingUploadCopy& c) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (b.staging_binding_offset > max - b.staging_capacity ||
        b.destination_binding_offset > max - b.destination_capacity ||
        c.source_offset < b.staging_offset ||
        !UploadRangeFits(c.source_offset - b.staging_offset, c.bytes, b.mapped_bytes) ||
        !UploadRangeFits(c.source_offset, c.bytes, b.staging_capacity) ||
        !UploadRangeFits(c.destination_offset, c.bytes, b.destination_capacity))
        return false;
    if (!c.bytes || !b.same_memory) return true;
    const auto src = b.staging_binding_offset + c.source_offset;
    const auto dst = b.destination_binding_offset + c.destination_offset;
    return src <= dst ? dst - src >= c.bytes : src - dst >= c.bytes;
}
static_assert(VerifyStagingUploadCopy({32, 64, 256, 0, 128, 512, false}, {32, 0, 64}));
static_assert(!VerifyStagingUploadCopy({32, 64, 256, 0, 128, 512, false}, {31, 0, 1}));
static_assert(!VerifyStagingUploadCopy({32, 64, 256, 0, 128, 512, false}, {32, 0, 65}));
static_assert(!VerifyStagingUploadCopy({0, 128, 256, 0, 128, 0, true}, {0, 32, 64}));
} // namespace Vulkan::NS2Research
