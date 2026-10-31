// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Eglantine PR #48 (Splatoon Raiders): a previously tracked contiguous
// physical run ends before pages that are mapped again or unmapped.
// Invoke while holding the native mapping mutex, use the native atomics.
#include <cstddef>
#include <cstdint>
namespace Eden::NS2::Eglantine {
template<class Read, class Write>
constexpr void CutContinuityAt(std::size_t first_changed_page,
                                Read&& read_count, Write&& write_count) {
    for (std::size_t index = first_changed_page; index-- > 0;) {
        const std::size_t remaining = first_changed_page - 1 - index;
        // No prior run crosses the boundary; earlier runs end sooner.
        if (read_count(index) <= remaining) break;
        write_count(index, static_cast<std::uint32_t>(remaining));
    }
}
} // namespace Eden::NS2::Eglantine
