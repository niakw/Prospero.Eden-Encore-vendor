// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "common/common_types.h"

namespace VideoCommon {

// PS5-selected, tracked source replacement for the immutable Eden UsageTracker.
// Adapted from Eglantine #462's UsageTracker partial-byte and page tests.
// A 64-byte tracking bit covers any overlapped byte, even a 1-byte access.
// That is conservative for Vulkan upload-vs-draw dependencies.
class UsageTracker {
    // PAGE_SHIFT is a FreeBSD system macro.
    static constexpr std::size_t BUFFER_BYTES_PER_BITSHIFT = 6;
    static constexpr std::size_t BUFFER_PAGE_SHIFT = 6 + BUFFER_BYTES_PER_BITSHIFT;
    static constexpr u64 BUFFER_PAGE_BYTES = u64{1} << BUFFER_PAGE_SHIFT;

public:
    explicit UsageTracker(std::size_t size)
        : pages((size >> BUFFER_PAGE_SHIFT) + 1, 0ULL) {}

    void Reset() noexcept {
        std::ranges::fill(pages, 0ULL);
    }

    void Track(u64 offset, u64 size) noexcept {
        if (!ValidRange(offset, size)) return;
        const u64 last = offset + size - 1;
        const std::size_t first_page = static_cast<std::size_t>(offset >> BUFFER_PAGE_SHIFT);
        const std::size_t last_page = static_cast<std::size_t>(last >> BUFFER_PAGE_SHIFT);
        if (last_page >= pages.size()) return;

        const u64 first_bytes =
            std::min<u64>(size, BUFFER_PAGE_BYTES - (offset % BUFFER_PAGE_BYTES));
        TrackPage(first_page, offset, first_bytes);
        for (std::size_t page = first_page + 1; page < last_page; ++page)
            pages[page] = ~u64{0};
        if (first_page != last_page) {
            const u64 last_base = last & ~(BUFFER_PAGE_BYTES - 1);
            TrackPage(last_page, last_base, (last % BUFFER_PAGE_BYTES) + 1);
        }
    }

    [[nodiscard]] bool IsUsed(u64 offset, u64 size) const noexcept {
        if (!ValidRange(offset, size)) return false;
        const u64 last = offset + size - 1;
        const std::size_t first_page = static_cast<std::size_t>(offset >> BUFFER_PAGE_SHIFT);
        const std::size_t last_page = static_cast<std::size_t>(last >> BUFFER_PAGE_SHIFT);
        if (last_page >= pages.size()) return false;

        const u64 first_bytes =
            std::min<u64>(size, BUFFER_PAGE_BYTES - (offset % BUFFER_PAGE_BYTES));
        if (IsPageUsed(first_page, offset, first_bytes)) return true;
        if (first_page == last_page) return false;

        for (std::size_t page = first_page + 1; page < last_page; ++page) {
            if (pages[page] != 0) return true;
        }
        const u64 last_base = last & ~(BUFFER_PAGE_BYTES - 1);
        return IsPageUsed(last_page, last_base, (last % BUFFER_PAGE_BYTES) + 1);
    }

private:
    [[nodiscard]] static bool ValidRange(u64 offset, u64 size) noexcept {
        // The source span is guest-controlled; refuse wrap and empty ranges.
        return size != 0 && size <= std::numeric_limits<u64>::max() - offset;
    }

    [[nodiscard]] static u64 PageMask(u64 offset, u64 size) noexcept {
        if (size == 0) return 0;
        const u64 offset_in_page = offset % BUFFER_PAGE_BYTES;
        const u64 bytes = std::min<u64>(size, BUFFER_PAGE_BYTES - offset_in_page);
        const std::size_t first_bit =
            static_cast<std::size_t>(offset_in_page >> BUFFER_BYTES_PER_BITSHIFT);
        const std::size_t last_bit = static_cast<std::size_t>(
            (offset_in_page + bytes - 1) >> BUFFER_BYTES_PER_BITSHIFT);
        const std::size_t count = last_bit - first_bit + 1;
        // count belongs to [1,64]. No shift by the operand width (UB).
        return ((~u64{0} >> (64 - count)) << first_bit);
    }

    void TrackPage(std::size_t page, u64 offset, u64 size) noexcept {
        pages[page] |= PageMask(offset, size);
    }

    [[nodiscard]] bool IsPageUsed(std::size_t page, u64 offset, u64 size) const noexcept {
        return (pages[page] & PageMask(offset, size)) != 0;
    }

    std::vector<u64> pages;
};

} // namespace VideoCommon
