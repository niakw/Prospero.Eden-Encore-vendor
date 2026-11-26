// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <new>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "common/assert.h"
#include "common/multi_level_page_table.h"

// Backport Lichii #957 (c9df50563e042b5dbc9415cc8df98c564bd25497).
// Keep the pinned Eden snapshot immutable; compile the maintained TU instead.
namespace Common {

#if defined(PS5_NATIVE)
// Match the active PS5 sparse-table allocator used by SparseLargeVector.
// Large virtual page tables must not consume the limited flexible mmap pool.
void* AllocateSparsePages(std::size_t size) noexcept;
void FreeSparsePages(void* base, std::size_t size) noexcept;
void CommitSparsePage(std::uintptr_t page) noexcept;
// Unlike the general sparse allocator's fatal first-commit contract, GPU
// page-table reservations must surface a recoverable allocation failure.
bool TryCommitSparsePage(std::uintptr_t page) noexcept;
std::size_t SparseCommitSpan() noexcept;
#endif

template <typename BaseAddr>
MultiLevelPageTable<BaseAddr>::MultiLevelPageTable(std::size_t address_space_bits_, std::size_t first_level_bits_, std::size_t page_bits_)
    : address_space_bits{address_space_bits_}
    , first_level_bits{first_level_bits_}
    , page_bits{page_bits_}
{
    if (page_bits == 0) {
        return;
    }
    first_level_shift = address_space_bits - first_level_bits;
    first_level_chunk_size = (1ULL << (first_level_shift - page_bits)) * sizeof(BaseAddr);
    alloc_size = (1ULL << (address_space_bits - page_bits)) * sizeof(BaseAddr);
    std::size_t first_level_size = 1ULL << first_level_bits;
    first_level_map.resize(first_level_size, nullptr);
#ifdef _WIN32
    void* base{VirtualAlloc(nullptr, alloc_size, MEM_RESERVE, PAGE_READWRITE)};
#else
#if defined(PS5_NATIVE)
    // PS5 has no demand-zero anonymous mapping for a large page table.
    // Reserve a sparse zero-backed VA range and commit only the slots written.
    void* base{AllocateSparsePages(alloc_size)};
#else
    void* base{mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)};
    if (base == MAP_FAILED)
        base = nullptr;
#endif
#endif
    // The PS5 assertion logger is not fatal. A failed sparse/dense GPU table
    // allocation must never publish a MemoryManager whose PTE base is null.
    // nvhost_as_gpu::AllocAsEx already translates bad_alloc to an NVDRV
    // InsufficientMemory result and leaves the address space uninitialized.
#if defined(PS5_NATIVE)
    if (!base) throw std::bad_alloc{};
#else
    ASSERT(base);
#endif
    base_ptr = reinterpret_cast<BaseAddr*>(base);
}

template <typename BaseAddr>
MultiLevelPageTable<BaseAddr>::~MultiLevelPageTable() noexcept {
    if (!base_ptr) {
        return;
    }
#ifdef _WIN32
    ASSERT(VirtualFree(base_ptr, 0, MEM_RELEASE));
#else
#if defined(PS5_NATIVE)
    FreeSparsePages(base_ptr, alloc_size);
#else
    ASSERT(munmap(base_ptr, alloc_size) == 0);
#endif
#endif
}

template <typename BaseAddr>
void MultiLevelPageTable<BaseAddr>::ReserveRange(u64 start, std::size_t size) {
    if (size == 0) {
        return;
    }
    const u64 new_start = start >> first_level_shift;
    const u64 new_end = (start + size - 1) >> first_level_shift;
    for (u64 i = new_start; i <= new_end; i++)
        if (!first_level_map[i])
            AllocateLevel(i);
}

template <typename BaseAddr>
void MultiLevelPageTable<BaseAddr>::AllocateLevel(u64 index) {
    void* ptr = reinterpret_cast<char *>(base_ptr) + index * first_level_chunk_size;
#ifdef _WIN32
    void* base = VirtualAlloc(ptr, first_level_chunk_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(base);
#else
    void* base = ptr;
#if defined(PS5_NATIVE)
    // Before MemoryManager writes the entries, give their sparse slots private
    // writable backing. CommitSparsePage is idempotent for overlapping chunks
    // and a no-op when AllocateSparsePages used its dense fallback.
    const auto chunk_begin = reinterpret_cast<std::uintptr_t>(ptr);
    const auto chunk_end = chunk_begin + first_level_chunk_size;
    const auto span = SparseCommitSpan();
    for (auto page = chunk_begin; page < chunk_end; page += span) {
        if (!TryCommitSparsePage(page)) throw std::bad_alloc{};
    }
    if (!TryCommitSparsePage(chunk_end - 1)) throw std::bad_alloc{};
#endif
#endif
    first_level_map[index] = base;
}

} // namespace Common

namespace Common {
template class MultiLevelPageTable<u64>;
template class MultiLevelPageTable<u32>;
} // namespace Common
