// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "common/common_types.h"

namespace Eden::DeferredCachedUpdates {

using Range = std::pair<DAddr, std::size_t>;

class Collector {
public:
    using Apply = void (*)(void*, std::span<const Range>, s32);

    [[nodiscard]] bool TryAdd(void* owner, Apply apply, std::span<const Range> ranges,
                              s32 delta) {
        if (flushing || delta >= 0 || ranges.empty()) {
            return false;
        }
        const std::size_t batch_count = batches.size();
        const std::size_t range_count = collected.size();
        try {
            batches.reserve(batch_count + 1);
            collected.reserve(range_count + ranges.size());
        } catch (const std::bad_alloc&) {
            // Keep ordering exact. If the reusable storage cannot grow, apply
            // earlier deferred decrements synchronously before the caller
            // falls back to its ordinary UpdatePagesCachedBatch path.
            Flush();
            return false;
        }
        collected.insert(collected.end(), ranges.begin(), ranges.end());
        batches.push_back(Batch{owner, apply, delta, range_count, ranges.size()});
        return true;
    }

    void Flush() {
        if (batches.empty()) {
            return;
        }
        flushing = true;
        for (const Batch& batch : batches) {
            batch.apply(batch.owner,
                        std::span<const Range>{collected}.subspan(batch.begin, batch.count),
                        batch.delta);
        }
        batches.clear();
        collected.clear();
        flushing = false;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return batches.empty();
    }

private:
    struct Batch {
        void* owner;
        Apply apply;
        s32 delta;
        std::size_t begin;
        std::size_t count;
    };

    std::vector<Batch> batches;
    std::vector<Range> collected;
    bool flushing{};
};

inline thread_local Collector* active_collector{};
inline std::atomic<u32> published_updates{0};

inline void BeginPublish() noexcept {
    published_updates.fetch_add(1, std::memory_order_relaxed);
}

inline void EndPublish() noexcept {
    published_updates.fetch_sub(1, std::memory_order_release);
}

inline void WaitForPublished() noexcept {
    while (published_updates.load(std::memory_order_acquire) != 0) {
        std::this_thread::yield();
    }
}

} // namespace Eden::DeferredCachedUpdates
