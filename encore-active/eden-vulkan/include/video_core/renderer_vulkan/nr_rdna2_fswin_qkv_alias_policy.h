// SPDX-License-Identifier: MIT
// C32 full-resolution FSwin needs the Q/K/V scratch AFTER the dense
// contraction has consumed the expanded activation. Reuse disjoint
// segments of that no-longer-live allocation; do not alias simultaneous
// Q/K/V readers or the model/input/contracted/context/output buffers.
#pragma once
#include <cstdint>
namespace Eden::Graphics::NeuralRDNA2 {
struct FSwinQkvExpandedAlias {
    std::uint64_t query_bytes{};
    std::uint64_t key_bytes{};
    std::uint64_t value_bytes{};
    std::uint64_t expanded_bytes{};
};
[[nodiscard]] constexpr bool CanFSwinQkvReuseExpanded(
    const FSwinQkvExpandedAlias& p) noexcept {
    // Vulkan guarantees minStorageBufferOffsetAlignment <= 256 bytes.
    // Q is at 0; K and V start at aligned cumulative offsets.
    constexpr std::uint64_t alignment=256u;
    if (!p.query_bytes || !p.key_bytes || !p.value_bytes ||
        !p.expanded_bytes ||
        (p.query_bytes % alignment) ||
        (p.key_bytes % alignment) ||
        (p.value_bytes % 4u) ||
        p.query_bytes > p.expanded_bytes)
        return false;
    const auto remaining_after_q=p.expanded_bytes-p.query_bytes;
    if (p.key_bytes > remaining_after_q) return false;
    return p.value_bytes <= remaining_after_q-p.key_bytes;
}
} // namespace Eden::Graphics::NeuralRDNA2
