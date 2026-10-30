// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* virtual_buffer.h */
// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <bit>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "common/alignment.h"
#include "common/assert.h"

namespace Common {

#ifdef _WIN32
constexpr u64 HostPageSize = 0x1000;
constexpr u64 HostPageBits = 12;
constexpr u64 HostPageMask = ~(HostPageSize - 1);
bool CommitVectorPage(uintptr_t addr, bool write) noexcept;
#else
const u64 HostPageSize = sysconf(_SC_PAGESIZE);
const u64 HostPageBits = std::countr_zero(HostPageSize);
const u64 HostPageMask = ~(HostPageSize - 1);
#endif

void* AllocateMemoryPages(std::size_t size) noexcept;
void FreeMemoryPages(void* base, std::size_t size) noexcept;
void* AllocateSparsePages(std::size_t size) noexcept;
void FreeSparsePages(void* base, std::size_t size) noexcept;
void CommitSparsePage(std::uintptr_t page) noexcept;
void DecommitSparsePage(std::uintptr_t page) noexcept;
std::size_t SparseCommitSpan() noexcept;

/// A large page-aligned buffer that has optimized memory usage for zero-writes.
template <typename T>
requires std::is_trivially_copyable_v<T>
class SparseLargeVector final {
public:
    constexpr SparseLargeVector() = default;

    explicit SparseLargeVector(std::size_t count) noexcept
        : alloc_size{count * sizeof(T)}
    {
        base_ptr = static_cast<T*>(AllocateSparsePages(alloc_size));

        // each item in vector holds information for 64 pages
        auto denom = HostPageSize * 64;
        committed_pages = std::vector<std::atomic<u64>>((alloc_size + denom - 1) / denom);
    }

    ~SparseLargeVector() noexcept {
        FreeSparsePages(base_ptr, alloc_size);
    }

    SparseLargeVector(const SparseLargeVector&) = delete;
    SparseLargeVector& operator=(const SparseLargeVector&) = delete;
    SparseLargeVector(SparseLargeVector&& other) = delete;
    SparseLargeVector& operator=(SparseLargeVector&& other) = delete;

    void ResizeAndClear(std::size_t count) noexcept {
        if (auto const new_size = count * sizeof(T); new_size != alloc_size) {
            FreeSparsePages(base_ptr, alloc_size);
            alloc_size = new_size;
            base_ptr = static_cast<T*>(AllocateSparsePages(alloc_size));

            auto denom = HostPageSize * 64;
            committed_pages = std::vector<std::atomic<u64>>((alloc_size + denom - 1) / denom);
        }
    }

    /// Returns a reference to the value of the requested index and allocates memory if needed.
    T& GetAndFault(std::size_t index) noexcept {
        if (index >= alloc_size / sizeof(T)) {
            UNREACHABLE_MSG("Out of bounds RW access on SparseLargeVector @ {}", index);
        }

        if (!IsCommittedPage(index)) {
            CommitPage(index);
        }
        return base_ptr[index];
    }

    /// Returns a reference to the value of the requested index if initialized, or will otherwise return a zero-initialized object.
    const T& GetOrDefault(std::size_t index) const {
#ifdef _WIN32
        if (!IsCommittedPage(index)) {
            return *reinterpret_cast<const T*>(&default_val);
        }
#endif
        // On non-Windows, OS page table should optimize this by pointing to a zero page if unallocated.
        return base_ptr[index];
    }

    void Set(std::size_t index, const T& value) noexcept {
        if (index >= alloc_size / sizeof(T)) {
            LOG_CRITICAL(Common_Memory, "Out of bounds write on SparseLargeVector @ {}", index);
            return;
        }
        if (!IsCommittedPage(index))
            CommitPage(index);
        base_ptr[index] = value;
    }

    void ZeroRegion(std::size_t start, std::size_t end_) noexcept {
        // Callers normally provide a half-open in-bounds interval. Reject
        // reversed/empty ranges and clip an oversized tail before constructing
        // pointers, so untrusted guest extents cannot wrap into a huge memset.
        const std::size_t limit = alloc_size / sizeof(T);
        if (start >= end_ || start >= limit) return;
        end_ = (std::min)(end_, limit);
        std::lock_guard<std::mutex> mutation_lock{sparse_mutation_mutex};
        u64 base = reinterpret_cast<u64>(&base_ptr[start]);
        const u64 end = reinterpret_cast<u64>(&base_ptr[end_]);

        const u64 end_page = AlignUp(base, HostPageSize);
        const u64 first_size = (std::min)(end_page, end) - base;

        if (IsCommittedPage(start)) {
            std::memset(reinterpret_cast<void*>(base), 0, first_size);
        }

        if (end <= end_page)
            return;

        base = end_page;

        for (u64 page = base; page < end; page += HostPageSize) {
            if (!IsCommittedPage((page - reinterpret_cast<u64>(base_ptr)) / sizeof(T))) {
                continue;
            }

            const auto index = (page - reinterpret_cast<u64>(base_ptr)) / sizeof(T);
            if (end - page >= HostPageSize) {
                DecommitPage(index);
            } else {
                std::memset(reinterpret_cast<void*>(page), 0, end - page);
            }
        }
    }

    constexpr void CommitRegion(size_t index, size_t end_) {
        // The same sparse backing is shared by CPU and GPU page-table owners.
        // An invalid range must never overflow the byte conversion or spin
        // over the entire virtual address space while committing pages.
        const std::size_t limit = alloc_size / sizeof(T);
        if (index >= end_ || index >= limit) return;
        end_ = (std::min)(end_, limit);
        const u64 base = static_cast<u64>(index) * sizeof(T);
        const u64 end = static_cast<u64>(end_) * sizeof(T);

        for (u64 page = AlignDown(base, HostPageSize); page < end; page += HostPageSize) {
            if (!IsCommittedPage(page / sizeof(T))) {
                CommitPage(page / sizeof(T));
            }
        }
    }

    constexpr T& GetUnchecked(size_t index) {
        return base_ptr[index];
    }

    [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept {
        return GetOrDefault(index);
    }

    [[nodiscard]] constexpr const T* data() const noexcept {
        return base_ptr;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return alloc_size / sizeof(T);
    }

private:
    [[nodiscard]] constexpr bool IsCommittedPage(std::size_t index) const noexcept {
        if (index >= alloc_size / sizeof(T)) {
            LOG_CRITICAL(Common_Memory, "Out of bounds access on large vector @ {}", index);
            return false;
        }

        auto page = (index * sizeof(T)) >> HostPageBits;
        auto val = committed_pages[page >> 6].load(std::memory_order_acquire);
        return (val >> (page & 63)) & 1;
    }

    void DecommitPage(std::size_t index) noexcept {
        const auto page_index = (index * sizeof(T)) >> HostPageBits;
        const auto vector_base = reinterpret_cast<std::uintptr_t>(base_ptr);
        const auto page_address = reinterpret_cast<std::uintptr_t>(base_ptr + index) & HostPageMask;
        // A host page may be cleared while *other* pages in its 2 MiB
        // physical slot remain in use. Clearing only the commit bitmap then
        // retains stale PageEntryData in the still-writable private backing.
        // CommitSparsePage does not re-zero an already owned slot, so remap
        // would resurrect old CPU/GPU address translations. Also zero the
        // dense fallback, where DecommitSparsePage intentionally does nothing.
        // ZeroRegion holds sparse_mutation_mutex and only calls here for a
        // committed/writable host page.
        std::memset(reinterpret_cast<void*>(page_address), 0, HostPageSize);
        committed_pages[page_index >> 6].fetch_and(~(1ULL << (page_index & 63)), std::memory_order_acq_rel);

        const std::size_t span = SparseCommitSpan();
        const std::size_t slot_offset = ((page_address - vector_base) / span) * span;
        const std::size_t first_page = slot_offset / HostPageSize;
        const std::size_t total_pages = alloc_size / HostPageSize +
                                        (alloc_size % HostPageSize != 0);
        const std::size_t last_page = (std::min)(first_page + span / HostPageSize,
                                                  total_pages);
        if (last_page <= first_page) return;
        // The caller owns sparse_mutation_mutex. Read each 64-page bitmap word
        // once instead of re-reading it for every retired host page.
        const std::size_t last_word = (last_page - 1) >> 6;
        for (std::size_t word = first_page >> 6; word <= last_word; ++word) {
            u64 bits = committed_pages[word].load(std::memory_order_acquire);
            if (word == (first_page >> 6))
                bits &= ~u64{0} << (first_page & 63);
            if (word == last_word && (last_page & 63) != 0)
                bits &= (u64{1} << (last_page & 63)) - 1;
            if (bits != 0) return;
        }
        DecommitSparsePage(vector_base + slot_offset);
    }

    void CommitPage(std::size_t index) noexcept {
        std::lock_guard<std::mutex> mutation_lock{sparse_mutation_mutex};
        if (IsCommittedPage(index)) return;
        auto page_index = (index * sizeof(T)) >> HostPageBits;
        auto page = reinterpret_cast<uintptr_t>(base_ptr + index) & HostPageMask;
#if defined(_WIN32)
        CommitVectorPage(page, true);
#else
        CommitSparsePage(page);
#endif

        committed_pages[page_index >> 6].fetch_or(1ULL << (page_index & 63), std::memory_order_release);
    }

    std::mutex sparse_mutation_mutex;
    std::size_t alloc_size{};
    T* base_ptr{};

    std::vector<std::atomic<u64>> committed_pages{};
#ifdef _WIN32
    const std::array<u8, sizeof(T)> default_val{};
#endif
};

} // namespace Common
