// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Ampere 0xC797 host SEM_EXECUTE 0x1b: isolated from Maxwell semaphore
// state and methods. All comparisons operate on the guest u32 payload.
#include <cstdint>
#include <optional>

namespace Tegra::Engines::NativeSemaphore {
[[nodiscard]] constexpr std::uint64_t Address(std::uint32_t high,
                                               std::uint32_t low) noexcept {
    return (std::uint64_t{high & 0x01ffffffU} << 32) |
           std::uint64_t{low & ~3U};
}
[[nodiscard]] constexpr bool IsAddressAdmissible(std::uint64_t address) noexcept {
    return address < (std::uint64_t{1} << 49) && !(address & 3U) &&
           address <= (std::uint64_t{1} << 49) - sizeof(std::uint32_t);
}
[[nodiscard]] constexpr std::optional<bool> AcquireReady(
    std::uint32_t operation, std::uint32_t observed,
    std::uint32_t expected) noexcept {
    switch (operation) {
    case 0: return observed == expected;
    case 2: return observed >= expected;
    case 3: return static_cast<std::int32_t>(observed - expected) >= 0;
    case 4: return (observed & expected) != 0;
    case 5: return (observed & expected) == 0;
    default: return std::nullopt; // release 1 and invalid 6/7 are not waits
    }
}
static_assert(Address(0x10001, 0x1043) == 0x1000100001040ULL);
static_assert(IsAddressAdmissible(0x1000100001040ULL));
static_assert(!IsAddressAdmissible(std::uint64_t{1} << 49));
static_assert(AcquireReady(0, 3, 3) == true);
static_assert(AcquireReady(2, 4, 3) == true);
static_assert(AcquireReady(3, 0, 0xffffffffU) == true);
static_assert(AcquireReady(4, 4, 4) == true);
static_assert(AcquireReady(5, 0, 1) == true);
static_assert(!AcquireReady(6, 0, 0));
} // namespace Tegra::Engines::NativeSemaphore
