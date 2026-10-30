// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <span>

#include "common/common_types.h"
#include "common/range_mutex.h"
#include "common/scratch_buffer.h"
#include "common/sparse_large_vector.h"

namespace Core {

constexpr size_t DEVICE_PAGEBITS = 12ULL;
constexpr size_t DEVICE_PAGESIZE = 1ULL << DEVICE_PAGEBITS;
constexpr size_t DEVICE_PAGEMASK = DEVICE_PAGESIZE - 1ULL;

class DeviceMemory;

namespace Memory {
class Memory;
}

template <typename DTraits>
struct DeviceMemoryManagerAllocator;

struct Asid {
    size_t id;
};

template <typename Traits>
class DeviceMemoryManager {
    using DeviceInterface = typename Traits::DeviceInterface;
    using DeviceMethods = typename Traits::DeviceMethods;

public:
    DeviceMemoryManager(const DeviceMemory& device_memory);
    ~DeviceMemoryManager();

    static constexpr bool HAS_FLUSH_INVALIDATION = true;
    static constexpr size_t AS_BITS = Traits::device_virtual_bits;

    void BindInterface(DeviceInterface* device_inter);

    DAddr Allocate(size_t size);
    void AllocateFixed(DAddr start, size_t size);
    void Free(DAddr start, size_t size);

    void Map(DAddr address, VAddr virtual_address, size_t size, Asid asid, bool track = false);

    void Unmap(DAddr address, size_t size);

    void TrackContinuityImpl(DAddr address, VAddr virtual_address, size_t size, Asid asid);
    void TrackContinuity(DAddr address, VAddr virtual_address, size_t size, Asid asid) {
        // Stable registered process across the whole continuity update.
        std::shared_lock registry_lk(process_registry_guard);
        std::scoped_lock lk(mapping_guard);
        TrackContinuityImpl(address, virtual_address, size, asid);
    }

    // Write / Read
    template <typename T>
    T* GetPointer(DAddr address);

    template <typename T>
    const T* GetPointer(DAddr address) const;

    template <typename Func>
    void ApplyOpOnPAddr(PAddr address, Common::ScratchBuffer<u32>& buffer, Func&& operation) {
        // A non-DRAM physical address must not index the sparse reverse
        // GPU table. Zero is the "no reverse mapping" sentinel, not GPU VA 0.
        if ((address >> page_bits) >= compressed_device_addr.size()) return;
        DAddr subbits = DAddr(address & page_mask);
        const u32 base = AtomicLoadReverse(compressed_device_addr[(address >> page_bits)]);
        if (base == 0) return;
        if ((base >> MULTI_FLAG_BITS) == 0) [[likely]] {
            const DAddr d_address = (DAddr(base) << page_bits) + subbits;
            operation(d_address);
            return;
        }
        InnerGatherDeviceAddresses(buffer, address);
        for (u32 value : buffer) {
            operation((DAddr(value) << page_bits) + subbits);
        }
    }

    template <typename Func>
    void ApplyOpOnPointer(const u8* p, Common::ScratchBuffer<u32>& buffer, Func&& operation) {
        // Some cached CPU pointers can outlive a guest page remap.
        // Compare BEFORE subtracting physical_base, otherwise a foreign
        // pointer wraps into a huge physical offset.
        const auto host = reinterpret_cast<uintptr_t>(p);
        if (host < physical_base ||
            host - physical_base >= (compressed_device_addr.size() << page_bits)) return;
        PAddr address = GetRawPhysicalAddr<u8>(p);
        ApplyOpOnPAddr(address, buffer, operation);
    }

    PAddr GetPhysicalRawAddressFromDAddr(DAddr address) const {
        if (address >= device_as_size) return 0;
        PAddr subbits = PAddr(address & page_mask);
        auto paddr = AtomicLoadPhysical(tracked_entries[(address >> page_bits)]);
        if (paddr == 0 || static_cast<size_t>(paddr - 1) >= compressed_device_addr.size()) {
            return 0;
        }
        return (PAddr(paddr - 1) << page_bits) + subbits;
    }

    template <typename T>
    void Write(DAddr address, T value);

    template <typename T>
    T Read(DAddr address) const;

    u8* GetSpan(const DAddr src_addr, const std::size_t size);
    const u8* GetSpan(const DAddr src_addr, const std::size_t size) const;

    void ReadBlock(DAddr address, void* dest_pointer, size_t size);
    void ReadBlockUnsafe(DAddr address, void* dest_pointer, size_t size);
    void WriteBlock(DAddr address, const void* src_pointer, size_t size);
    void WriteBlockUnsafe(DAddr address, const void* src_pointer, size_t size);

    Asid RegisterProcess(Memory::Memory* memory);
    void UnregisterProcess(Asid id);

    void UpdatePagesCachedCount(DAddr addr, size_t size, s32 delta);

    // New batch API to update multiple ranges with a single lock acquisition.
    void UpdatePagesCachedBatch(std::span<const std::pair<DAddr, size_t>> ranges, s32 delta);

private:
    struct TranslationEntry {
        DAddr guest_page{};
        u8* host_ptr{};
    };

    // Internal helper that performs the update assuming the caller already holds the necessary lock.
    void UpdatePagesCachedCountNoLock(DAddr addr, size_t size, s32 delta);

private:
    static constexpr size_t device_virtual_bits = Traits::device_virtual_bits;
    static constexpr size_t device_as_size = 1ULL << device_virtual_bits;
    static constexpr size_t physical_min_bits = 32;
    static constexpr size_t physical_max_bits = 33;
    static constexpr size_t page_bits = 12;
    static constexpr size_t page_size = 1ULL << page_bits;
    static constexpr size_t page_mask = page_size - 1ULL;
    static constexpr u32 physical_address_base = 1U << page_bits;
    static constexpr u32 MULTI_FLAG_BITS = 31;
    static constexpr u32 MULTI_FLAG = 1U << MULTI_FLAG_BITS;
    static constexpr u32 MULTI_MASK = ~MULTI_FLAG;

    template <typename T>
    T* GetPointerFromRaw(PAddr addr) {
        return reinterpret_cast<T*>(physical_base + addr);
    }

    template <typename T>
    const T* GetPointerFromRaw(PAddr addr) const {
        return reinterpret_cast<T*>(physical_base + addr);
    }

    template <typename T>
    PAddr GetRawPhysicalAddr(const T* ptr) const {
        return static_cast<PAddr>(reinterpret_cast<uintptr_t>(ptr) - physical_base);
    }

    void WalkBlock(const DAddr addr, const std::size_t size, auto on_unmapped, auto on_memory,
                   auto increment);

    void InnerGatherDeviceAddresses(Common::ScratchBuffer<u32>& buffer, PAddr address);

    std::unique_ptr<DeviceMemoryManagerAllocator<Traits>> impl;

    const uintptr_t physical_base;
    DeviceInterface* device_inter;

    struct TrackedEntry {
        VAddr cpu_backing_address;
        u32 continuity_tracker;
        u32 compressed_physical_ptr;
    };
    // On PS5, GPU remap writers publish field changes concurrently with
    // renderer reads. GNU __atomic builtins preserve SparseLargeVector's trivially-copyable
    // 16-byte record and its native physical layout unchanged. All C++
    // translation units use the same atomic publication protocol.
    static u32 AtomicLoadPhysical(const TrackedEntry& page) noexcept {
        return __atomic_load_n(&page.compressed_physical_ptr, __ATOMIC_ACQUIRE);
    }
    static void AtomicStorePhysical(TrackedEntry& page, u32 value) noexcept {
        __atomic_store_n(&page.compressed_physical_ptr, value, __ATOMIC_RELEASE);
    }
    static u32 AtomicLoadContinuity(const TrackedEntry& page) noexcept {
        return __atomic_load_n(&page.continuity_tracker, __ATOMIC_RELAXED);
    }
    static void AtomicStoreContinuity(TrackedEntry& page, u32 value) noexcept {
        __atomic_store_n(&page.continuity_tracker, value, __ATOMIC_RELAXED);
    }
    static VAddr AtomicLoadBacking(const TrackedEntry& page) noexcept {
        return __atomic_load_n(&page.cpu_backing_address, __ATOMIC_ACQUIRE);
    }
    static void AtomicStoreBacking(TrackedEntry& page, VAddr value) noexcept {
        __atomic_store_n(&page.cpu_backing_address, value, __ATOMIC_RELEASE);
    }
    // Scalar reverse physical-to-GPU slots are also read by header-inline
    // renderer callers. Both readers and writers use GNU __atomic builtins so those
    // calls do not race with Map/Unmap. MultiAddressContainer operations
    // still rely on the existing mapping_guard.
    static u32 AtomicLoadReverse(const u32& slot) noexcept {
        return __atomic_load_n(&slot, __ATOMIC_ACQUIRE);
    }
    static void AtomicStoreReverse(u32& slot, u32 value) noexcept {
        __atomic_store_n(&slot, value, __ATOMIC_RELEASE);
    }
    Common::SparseLargeVector<u32> compressed_device_addr;
    Common::SparseLargeVector<TrackedEntry> tracked_entries;

    // Process memory interfaces

    // Registration is infrequent. A shared lock protects Map/continuity
    // and cache bookkeeping against an ASID being unregistered/reused.
    // None of the hot GPU ReadBlock/WriteBlock/GetSpan paths acquire it.
    mutable std::shared_mutex process_registry_guard;
    // Tombstoned IDs are intentionally not recycled while this GPU manager lives.
    std::deque<Memory::Memory*> registered_processes;

    // Memory protection management

    static constexpr size_t guest_max_as_bits = 39;
    static constexpr size_t guest_as_size = 1ULL << guest_max_as_bits;
    static constexpr size_t guest_mask = guest_as_size - 1ULL;
    static constexpr size_t asid_start_bit = guest_max_as_bits;

    std::pair<Asid, VAddr> ExtractCPUBacking(size_t page_index) {
        auto content = AtomicLoadBacking(tracked_entries[page_index]);
        const VAddr address = content & guest_mask;
        const Asid asid{static_cast<size_t>(content >> asid_start_bit)};
        return std::make_pair(asid, address);
    }

    void InsertCPUBacking(size_t page_index, VAddr address, Asid asid) {
        AtomicStoreBacking(tracked_entries.GetUnchecked(page_index), address | (asid.id << asid_start_bit));
    }

    std::array<TranslationEntry, 4> t_slot{};
    u32 cache_cursor = 0;
    using CounterType = u8;
    using CounterAtomicType = std::atomic_uint8_t;
    static constexpr size_t subentries = 8 / sizeof(CounterType);
    static constexpr size_t subentries_mask = subentries - 1;
    static constexpr size_t subentries_shift =
        std::countr_zero(sizeof(u64)) - std::countr_zero(sizeof(CounterType));
    class CounterEntry final {
    public:
        CounterEntry() = default;

        CounterAtomicType& Count(std::size_t page) {
            return values[page & subentries_mask];
        }

        const CounterAtomicType& Count(std::size_t page) const {
            return values[page & subentries_mask];
        }

    private:
        std::array<CounterAtomicType, subentries> values{};
    };
    static_assert(sizeof(CounterEntry) == subentries * sizeof(CounterType),
                  "CounterEntry should be 8 bytes!");

    static constexpr size_t num_counter_entries =
        (1ULL << (device_virtual_bits - page_bits)) / subentries;
    using CachedPages = std::array<CounterEntry, num_counter_entries>;
    std::unique_ptr<CachedPages> cached_pages;
    Common::RangeMutex counter_guard;
    std::mutex mapping_guard;


};

} // namespace Core
