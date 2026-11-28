// SPDX-FileCopyrightText: Copyright 2026 Eden Encore Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <utility>

namespace Service::Nvidia::Devices {
// P2.7: submission-time command mapping may be revoked before the GPU
// worker fetches live words. Recheck the actual interval immediately before
// DmaPusher::ProcessCommands; never interpret zero-filled unmapped data as
// a valid native GPU packet or acknowledge its following fence.
template <typename IsMapped>
[[nodiscard]] bool NativeGpfifoFetchMapped(std::uint64_t address,
                                            std::uint64_t words,
                                            IsMapped&& is_mapped) {
    constexpr std::uint64_t native_limit = std::uint64_t{1} << 49;
    constexpr std::uint64_t bytes_per_word = sizeof(std::uint32_t);
    // Empty GPU spans may be legal fence-only entries and read no memory.
    if (words == 0) return true;
    if (address == 0 || address >= native_limit ||
        words > (native_limit - address) / bytes_per_word)
        return false;
    return std::forward<IsMapped>(is_mapped)(address, words * bytes_per_word);
}
} // namespace Service::Nvidia::Devices
