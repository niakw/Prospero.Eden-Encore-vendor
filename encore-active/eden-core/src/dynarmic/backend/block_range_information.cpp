// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/block_range_information.h"

#include <algorithm>
#include <boost/icl/interval_set.hpp>
#include "common/common_types.h"

namespace Dynarmic::Backend {

template<typename P>
void BlockRangeInformation<P>::AddRange(boost::icl::discrete_interval<P> range, IR::LocationDescriptor location) {
    if (boost::icl::is_empty(range))
        return;
    const P first = boost::icl::first(range);
    const P last = boost::icl::last(range);
    for (P page = first >> page_bits;; ++page) {
        pages[page].push_back({first, last, location});
        if (page == (last >> page_bits))
            break;
    }
}

template<typename P>
void BlockRangeInformation<P>::ClearCache() {
    pages.clear();
}

template<typename P>
::Common::unordered_set<IR::LocationDescriptor> BlockRangeInformation<P>::InvalidateRanges(const boost::icl::interval_set<P>& ranges) {
    ::Common::unordered_set<IR::LocationDescriptor> erase_locations;
    // Invalidated blocks can span multiple guest pages: track all owner
    // pages so stale entries cannot later evict unrelated recompiled code.
    std::vector<P> purge_pages;
    for (const auto& interval : ranges) {
        if (boost::icl::is_empty(interval))
            continue;
        const P first = boost::icl::first(interval);
        const P last = boost::icl::last(interval);
        // Every block overlapping the range goes, and leaves this page's list.
        const auto visit = [&](std::vector<Entry>& entries) {
            for (size_t i = 0; i < entries.size();) {
                if (entries[i].first <= last && first <= entries[i].last) {
                    erase_locations.insert(entries[i].location);
                    const P last_owner_page = entries[i].last >> page_bits;
                    for (P owner_page = entries[i].first >> page_bits;; ++owner_page) {
                        purge_pages.push_back(owner_page);
                        if (owner_page == last_owner_page)
                            break;
                    }
                    entries[i] = entries.back();
                    entries.pop_back();
                } else {
                    ++i;
                }
            }
        };
        const P first_page = first >> page_bits;
        const P last_page = last >> page_bits;
        if (static_cast<u64>(last_page - first_page) < pages.size()) {
            for (P page = first_page;; ++page) {
                if (const auto it = pages.find(page); it != pages.end())
                    visit(it->second);
                if (page == last_page)
                    break;
            }
        } else {
            // A range wider than the pages holding blocks visits those pages only.
            for (auto& [page, entries] : pages) {
                if (page >= first_page && page <= last_page)
                    visit(entries);
            }
        }
    }
    // Purge every remaining reference to invalidated blocks, even on
    // guest pages not touched by this write. Never scan unrelated buckets.
    std::sort(purge_pages.begin(), purge_pages.end());
    purge_pages.erase(std::unique(purge_pages.begin(), purge_pages.end()),
                      purge_pages.end());
    for (const P page : purge_pages) {
        const auto it = pages.find(page);
        if (it == pages.end())
            continue;
        auto& entries = it->second;
        std::erase_if(entries, [&](const Entry& entry) {
            return erase_locations.find(entry.location) != erase_locations.end();
        });
        if (entries.empty())
            pages.erase(it);
    }
    return erase_locations;
}

template class BlockRangeInformation<u32>;
template class BlockRangeInformation<u64>;

}  // namespace Dynarmic::Backend
