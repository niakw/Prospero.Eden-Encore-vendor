// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace Eden::GpuMemory {
// The same GPU VA can remain in the Mapped state while its physical backing
// changes. Rasterizer texture and buffer caches must invalidate the OLD
// mapping before the PTE is replaced. Preserve the existing NS1 transitions.
template <class Entry>
[[nodiscard]] constexpr bool NeedsRasterizerUnmap(
    Entry previous, Entry next, Entry mapped, std::uint32_t previous_page,
    std::uint32_t replacement_page) noexcept {
    return previous != next ||
           (previous == mapped && next == mapped &&
            previous_page != replacement_page);
}
} // namespace Eden::GpuMemory
