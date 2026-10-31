// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Shared, generation-agnostic native GPU memory admission policy.
// PS5 native reports the shared CPU/GPU direct-memory pool's largest free
// block via the common Device::GetDeviceMemoryUsage adapter. Do not treat
// reported VRAM and visible VRAM usage as additive allocations.
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Eden::Vulkan::MemoryPolicy {

struct BudgetDecision {
    std::uint64_t max_new_bytes{};
    std::uint64_t headroom_bytes{};
    bool measured{};
};

// No game-title, architecture or Switch-generation condition belongs here.
// Unknown hardware telemetry gets only the conservative existing per-image
// cap. A measured pool retains at least 1 GiB (or 1/8 for smaller pools)
// for the guest CPU, Vulkan staging, the driver and follow-up allocations.
// "used" must measure the common memory pool, not the driver's double-counted
// VRAM/visible-VRAM totals.
[[nodiscard]] constexpr BudgetDecision SparseBackingBudget(
    std::uint64_t device_capacity, std::uint64_t shared_pool_used,
    bool reliable_shared_pool_usage, std::uint64_t original_image_cap) noexcept {
    if (!device_capacity || !original_image_cap) return {};
    if (!reliable_shared_pool_usage) {
        return {original_image_cap, 0, false};
    }
    if (shared_pool_used > device_capacity) return {0, 0, true};
    constexpr std::uint64_t GiB = std::uint64_t{1} << 30;
    const std::uint64_t reserve = std::min(GiB, device_capacity / 8);
    const std::uint64_t free = device_capacity - shared_pool_used;
    if (free <= reserve) return {0, reserve, true};
    return {std::min(original_image_cap, free - reserve), reserve, true};
}
} // namespace Eden::Vulkan::MemoryPolicy
