// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace Service::Nvidia::Devices::NativeGpuAllocation {

// All ioctl subranges are checked with subtraction, never offset + bytes.
[[nodiscard]] constexpr bool ContainsSubrange(std::uint64_t size,
                                              std::uint64_t offset,
                                              std::uint64_t bytes) noexcept {
    return bytes != 0 && offset <= size && bytes <= size - offset;
}

// Return only an allocation that fully owns [address, address + bytes).
// Unlike a post-decrement of upper_bound(), this never decrements begin()
// for an empty table or an address before the first allocation. Subtraction
// avoids unsigned overflow in address - base + bytes.
template <typename Map>
[[nodiscard]] auto FindContaining(Map& allocations, std::uint64_t address,
                                  std::uint64_t bytes) -> typename Map::iterator {
    // A native Ampere process exposes at most 49 GPU VA bits. Refuse
    // wrapped/zero-length spans before any allocation lookup.
    constexpr std::uint64_t gpu_limit = std::uint64_t{1} << 49;
    if (!bytes || address >= gpu_limit || bytes > gpu_limit - address)
        return allocations.end();
    auto it = allocations.upper_bound(address);
    if (it == allocations.begin()) return allocations.end();
    --it;
    if (address < it->first || it->first >= gpu_limit ||
        it->second.size > gpu_limit - it->first) return allocations.end();
    const std::uint64_t offset = address - it->first;
    if (offset >= it->second.size || bytes > it->second.size - offset)
        return allocations.end();
    return it;
}

} // namespace Service::Nvidia::Devices::NativeGpuAllocation
