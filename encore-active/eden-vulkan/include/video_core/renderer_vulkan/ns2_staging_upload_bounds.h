// SPDX-License-Identifier: GPL-3.0-or-later
// R2 research-only typed staging-copy bounds; no GPU lifetime lease implied.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
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
// Bounded fusion of adjacent regions INSIDE ONE ALREADY-ORDERED typed
// staging upload. No cross-command batching, barrier removal, queue reorder,
// or GPU ownership extension is implied. On capacity exhaustion the caller
// must keep the exact original VkBufferCopy list.
template <std::size_t MaxRegions = 64>
class StagingRegionCoalescer final {
    static_assert(MaxRegions > 0);
public:
    [[nodiscard]] constexpr std::size_t Size() const noexcept { return count_; }
    [[nodiscard]] constexpr std::span<const StagingUploadCopy> Copies() const noexcept {
        return {regions_.data(), count_};
    }
    [[nodiscard]] constexpr bool Append(const StagingUploadBounds& bounds,
                                        const StagingUploadCopy& next) noexcept {
        if (!VerifyStagingUploadCopy(bounds, next)) return false;
        // Zero-sized copies are already dropped by the original CopyBuffer.
        if (next.bytes == 0) return true;
        constexpr auto max = std::numeric_limits<std::uint64_t>::max();
        if (count_ != 0) {
            StagingUploadCopy& last = regions_[count_ - 1];
            if (last.source_offset <= max - last.bytes &&
                last.destination_offset <= max - last.bytes &&
                next.bytes <= max - last.bytes &&
                next.source_offset == last.source_offset + last.bytes &&
                next.destination_offset == last.destination_offset + last.bytes) {
                const StagingUploadCopy combined{last.source_offset,
                                                 last.destination_offset,
                                                 last.bytes + next.bytes};
                // Same VkDeviceMemory can alias across individually safe
                // regions. Prove the full combined span before replacing them.
                if (VerifyStagingUploadCopy(bounds, combined)) {
                    last.bytes = combined.bytes;
                    return true;
                }
            }
        }
        if (count_ == MaxRegions) return false;
        regions_[count_++] = next;
        return true;
    }
private:
    std::array<StagingUploadCopy, MaxRegions> regions_{};
    std::size_t count_{};
};

[[nodiscard]] constexpr bool StagingRegionFusionCompileContract() {
    StagingRegionCoalescer<2> fused;
    constexpr StagingUploadBounds b{0, 128, 128, 0, 128, 256, false};
    return fused.Append(b, {0, 0, 16}) && fused.Append(b, {16, 16, 16}) &&
           fused.Size() == 1 && fused.Copies()[0].bytes == 32 &&
           fused.Append(b, {80, 80, 8}) && fused.Size() == 2 &&
           !fused.Append(b, {100, 100, 4});
}
static_assert(StagingRegionFusionCompileContract());

// A reservation belongs to one scheduler submission tick. Equality is
// necessary for prospective batching, but never sufficient for GPU lifetime.
[[nodiscard]] constexpr bool IsCurrentStagingRequest(std::uint64_t request_generation,
                                                      std::uint64_t request_tick,
                                                      std::uint64_t live_tick) noexcept {
    return request_generation != 0 && request_tick != 0 &&
           request_tick == live_tick;
}
static_assert(IsCurrentStagingRequest(1, 4, 4));
static_assert(!IsCurrentStagingRequest(0, 4, 4));
static_assert(!IsCurrentStagingRequest(1, 4, 5));

static_assert(VerifyStagingUploadCopy({32, 64, 256, 0, 128, 512, false}, {32, 0, 64}));
static_assert(!VerifyStagingUploadCopy({32, 64, 256, 0, 128, 512, false}, {31, 0, 1}));
static_assert(!VerifyStagingUploadCopy({32, 64, 256, 0, 128, 512, false}, {32, 0, 65}));
static_assert(!VerifyStagingUploadCopy({0, 128, 256, 0, 128, 0, true}, {0, 32, 64}));
} // namespace Vulkan::NS2Research
