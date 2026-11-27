// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace Eden::Vulkan::MemoryPolicy {

struct SparseBackingDecision {
    bool measured = false;
    std::uint64_t headroom_bytes = 0;
    std::uint64_t max_new_bytes = 0;
};

// Bounds sparse backing in the shared PS5 GPU pool for both NS1 and NS2.
// Unknown or non-pool-backed usage cannot be treated as measured VRAM.
constexpr SparseBackingDecision SparseBackingBudget(
    std::uint64_t shared_pool_capacity, std::uint64_t shared_pool_used,
    bool reliable_shared_pool_usage, std::uint64_t per_image_limit) noexcept {
    if (!reliable_shared_pool_usage)
        return {false, 0, per_image_limit};
    constexpr std::uint64_t GiB = 1ULL << 30;
    const std::uint64_t reserve = shared_pool_capacity >= 8 * GiB
                                      ? GiB : shared_pool_capacity / 8;
    if (shared_pool_capacity == 0 || shared_pool_used > shared_pool_capacity)
        return {true, reserve, 0};
    const std::uint64_t free = shared_pool_capacity - shared_pool_used;
    const std::uint64_t available = free > reserve ? free - reserve : 0;
    return {true, reserve, std::min(available, per_image_limit)};
}

} // namespace Eden::Vulkan::MemoryPolicy
