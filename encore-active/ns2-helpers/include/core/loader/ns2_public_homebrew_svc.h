// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Public, unsigned HOS hello test ONLY. Do not restrict other games.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace Eden::NS2::ExeFS {
inline constexpr std::array<std::uint32_t, 5> PublicHelloSvcIds{
    0x07, 0x0B, 0x1E, 0x26, 0x27
};
inline constexpr std::uint64_t PublicHelloProgramId = 0x0100000000000420ULL;
[[nodiscard]] constexpr bool IsPublicHelloFixture(
    std::uint64_t title_id, std::span<const std::uint8_t> process_name) noexcept {
    constexpr std::array<std::uint8_t, 11> expected{
        'N','S','2','-','H','O','S','T','-','H','B'};
    if (title_id != PublicHelloProgramId || process_name.size() != 16)
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (process_name[i] != expected[i]) return false;
    for (std::size_t i = expected.size(); i < process_name.size(); ++i)
        if (process_name[i] != 0) return false;
    return true;
}
template<class IsAllowed>
[[nodiscard]] constexpr std::uint32_t MissingPublicHelloSvcs(IsAllowed&& permitted) {
    std::uint32_t missing = 0;
    for (std::size_t i = 0; i < PublicHelloSvcIds.size(); ++i)
        if (!permitted(PublicHelloSvcIds[i])) missing |= 1U << i;
    return missing;
}
} // namespace Eden::NS2::ExeFS
