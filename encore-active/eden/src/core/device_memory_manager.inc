// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <limits>
#include <memory>
#include <type_traits>
#include <algorithm>
#include <vector>

#include <boost/container/deque.hpp>

#include "common/address_space.h"
#include "common/address_space.inc"
#include "common/alignment.h"
#include "common/assert.h"
#include "common/div_ceil.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/device_memory.h"
#include "core/device_memory_manager.h"
#include "core/memory.h"
#ifdef PS5_NATIVE
#include "core/hle/kernel/board/nintendo/nx/k_system_control.h"
#include "gpu_fault_rate_limit.h"
#endif

namespace Core {

namespace {

class MultiAddressContainer {
public:
    MultiAddressContainer() = default;
    ~MultiAddressContainer() = default;

    void GatherValues(u32 start_entry, Common::ScratchBuffer<u32>& buffer) {
        buffer.resize(8);
        const auto add_value = [&buffer](u32 value, size_t index) {
            if (buffer.size() < index + 1)
                buffer.resize(index + 8);
            buffer[index++] = value;
            return index;
        };

        size_t index = 0;
        u32 iter_entry = start_entry;
        Entry* current = &storage[iter_entry - 1];
        index = add_value(current->value, index);
        while (current->next_entry != 0) {
            iter_entry = current->next_entry;
            current = &storage[iter_entry - 1];
            index = add_value(current->value, index);
        }
        buffer.resize(index);
    }

    u32 Register(u32 value) {
        return RegisterImplementation(value);
    }

    void Register(u32 value, u32 start_entry) {
        auto entry_id = RegisterImplementation(value);
        u32 iter_entry = start_entry;
        Entry* current = &storage[iter_entry - 1];
        while (current->next_entry != 0) {
            iter_entry = current->next_entry;
            current = &storage[iter_entry - 1];
        }
        current->next_entry = entry_id;
    }


#ifdef PS5_NATIVE
    // The old Unregister assumes its value is always present. A stale/cyclic
    // reverse map must never walk past the end of storage into an invalid node.
    [[nodiscard]] bool Contains(u32 value, u32 start_entry) const noexcept {
        for (size_t steps = 0; start_entry && steps < storage.size(); ++steps) {
            if (start_entry > storage.size()) return false;
            const auto& node = storage[start_entry - 1];
            if (node.value == value) return true;
            start_entry = node.next_entry;
        }
        return false;
    }
#endif

    std::pair<bool, u32> Unregister(u32 value, u32 start_entry) {
        u32 iter_entry = start_entry;
        Entry* previous{};
        Entry* current = &storage[iter_entry - 1];
        Entry* next{};
        bool more_than_one_remaining = false;
        u32 result_start{start_entry};
        size_t count = 0;
        while (current->value != value) {
            count++;
            previous = current;
            iter_entry = current->next_entry;
            current = &storage[iter_entry - 1];
        }
        // Find next
        u32 next_entry = current->next_entry;
        if (next_entry != 0) {
            next = &storage[next_entry - 1];
            more_than_one_remaining = next->next_entry != 0 || previous != nullptr;
        }
        if (previous) {
            previous->next_entry = next_entry;
        } else {
            result_start = next_entry;
        }
        free_entries.emplace_back(iter_entry);
        return std::make_pair(more_than_one_remaining || count > 1, result_start);
    }

    u32 ReleaseEntry(u32 start_entry) {
        Entry* current = &storage[start_entry - 1];
        free_entries.emplace_back(start_entry);
        return current->value;
    }

private:
    u32 RegisterImplementation(u32 value) {
        auto entry_id = GetNewEntry();
        auto& entry = storage[entry_id - 1];
        entry.next_entry = 0;
        entry.value = value;
        return entry_id;
    }
    u32 GetNewEntry() {
        if (!free_entries.empty()) {
            u32 result = free_entries.front();
            free_entries.pop_front();
            return result;
        }
        storage.emplace_back();
        u32 new_entry = static_cast<u32>(storage.size());
        return new_entry;
    }

    struct Entry {
        u32 next_entry{};
        u32 value{};
    };

    std::vector<Entry> storage;
    boost::container::deque<u32> free_entries;
};

struct EmptyAllocator {
    EmptyAllocator([[maybe_unused]] DAddr address) {}
};

} // namespace

template <typename DTraits>
struct DeviceMemoryManagerAllocator {
    static constexpr size_t device_virtual_bits = DTraits::device_virtual_bits;
    static constexpr DAddr first_address = 1ULL << Memory::YUZU_PAGEBITS;
    static constexpr DAddr max_device_area = 1ULL << device_virtual_bits;

    DeviceMemoryManagerAllocator() : main_allocator(first_address) {}

    Common::FlatAllocator<DAddr, 0, device_virtual_bits> main_allocator;
    MultiAddressContainer multi_dev_address;

    /// Returns true when vaddr -> vaddr+size is fully contained in the buffer
    template <bool pin_area>
    [[nodiscard]] bool IsInBounds(VAddr addr, u64 size) const noexcept {
        return addr >= 0 && addr + size <= max_device_area;
    }

    DAddr Allocate(size_t size) {
        return main_allocator.Allocate(size);
    }

    void AllocateFixed(DAddr b_address, size_t b_size) {
        main_allocator.AllocateFixed(b_address, b_size);
    }

    void Free(DAddr b_address, size_t b_size) {
        main_allocator.Free(b_address, b_size);
    }
};

template <typename Traits>
DeviceMemoryManager<Traits>::DeviceMemoryManager(const DeviceMemory& device_memory_)
    : physical_base{uintptr_t(device_memory_.buffer.BackingBasePointer())}
    , device_inter{nullptr}
    , compressed_device_addr(
#ifdef PS5_NATIVE
        // Reverse mappings need actual configured 4/6/8/10/12 GiB DRAM.
        // SparseLargeVector reserves virtual space; it does not commit it.
        Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize() >> Memory::YUZU_PAGEBITS
#else
        1ULL << ((Settings::values.memory_layout_mode.GetValue() == Settings::MemoryLayout::Memory_4Gb ? physical_min_bits : physical_max_bits) - Memory::YUZU_PAGEBITS)
#endif
    )
    , tracked_entries(device_as_size >> Memory::YUZU_PAGEBITS)
{
    impl = std::make_unique<DeviceMemoryManagerAllocator<Traits>>();
    cached_pages = std::make_unique<CachedPages>();
}

template <typename Traits>
DeviceMemoryManager<Traits>::~DeviceMemoryManager() = default;

template <typename Traits>
void DeviceMemoryManager<Traits>::BindInterface(DeviceInterface* device_inter_) {
    device_inter = device_inter_;
}

template <typename Traits>
DAddr DeviceMemoryManager<Traits>::Allocate(size_t size) {
    return impl->Allocate(size);
}

template <typename Traits>
void DeviceMemoryManager<Traits>::AllocateFixed(DAddr start, size_t size) {
    return impl->AllocateFixed(start, size);
}

template <typename Traits>
void DeviceMemoryManager<Traits>::Free(DAddr start, size_t size) {
    impl->Free(start, size);
}

template <typename Traits>
void DeviceMemoryManager<Traits>::Map(DAddr address, VAddr virtual_address, size_t size, Asid asid,
                                      bool track) {
#ifdef PS5_NATIVE
    if (size == 0) return;
    if (address >= device_as_size || size > device_as_size - address ||
        virtual_address >= guest_as_size || size > guest_as_size - virtual_address) {
        if (::Eden::GpuFault::ShouldReportBadMapRange())
            LOG_ERROR(HW_Memory, "EDEN_GPU_MAP_BAD_RANGE gpu={:#x} guest={:#x} bytes={:#x}",
                      address, virtual_address, size);
        return;
    }
#endif
    // Keep the process alive in this registry during GPU map publication.
    // Lock order: registry (shared) before mapping_guard. Registration and
    // unregister use the registry's exclusive lock, never mapping_guard.
    std::shared_lock registry_lk(process_registry_guard);
    if (asid.id >= registered_processes.size() || registered_processes[asid.id] == nullptr) {
#ifdef PS5_NATIVE
        if (::Eden::GpuFault::ShouldReportBadMapRange())
            LOG_WARNING(HW_Memory, "EDEN_GPU_MAP_BAD_ASID id={} registered={}",
                        asid.id, registered_processes.size());
#endif
        return;
    }
    Core::Memory::Memory* process_memory = registered_processes[asid.id];
    size_t start_page_d = address >> Memory::YUZU_PAGEBITS;
    size_t num_pages = Common::AlignUp(size, Memory::YUZU_PAGESIZE) >> Memory::YUZU_PAGEBITS;
#ifdef PS5_NATIVE
    bool replaced_gpu_mapping = false;
    std::unique_lock lk(mapping_guard);
#else
    std::scoped_lock lk(mapping_guard);
#endif

    tracked_entries.CommitRegion(start_page_d, start_page_d + num_pages);
    for (size_t i = 0; i < num_pages; i++) {
        const VAddr new_vaddress = virtual_address + i * Memory::YUZU_PAGESIZE;
        auto* ptr = process_memory->GetPointerSilent(Common::ProcessAddress(new_vaddress));
#ifdef PS5_NATIVE
        const auto host = reinterpret_cast<std::uintptr_t>(ptr);
        const auto physical_size = compressed_device_addr.size() << Memory::YUZU_PAGEBITS;
        if (ptr && (host < physical_base || host - physical_base >= physical_size)) {
            if (::Eden::GpuFault::ShouldReportBadPhysical())
                LOG_WARNING(HW_Memory, "EDEN_GPU_MAP_OUTSIDE_DRAM guest={:#x} host={:#x}",
                            new_vaddress, host);
            ptr = nullptr;
        }
#endif
#ifdef PS5_NATIVE
        // Map may replace an existing GPU-page mapping without an Unmap call.
        // The previous forward page is NOT sufficient: unlink its reverse
        // physical->GPU association before installing the replacement.
        const u32 device_page = static_cast<u32>(start_page_d + i);
        auto& old_entry = tracked_entries.GetUnchecked(start_page_d + i);
        const u32 previous_physical = AtomicLoadPhysical(old_entry);
        const u32 replacement_physical = ptr
            ? static_cast<u32>(GetRawPhysicalAddr(ptr) >> Memory::YUZU_PAGEBITS) + 1U : 0U;
        if (previous_physical != 0 && previous_physical != replacement_physical)
            replaced_gpu_mapping = true;
        if (previous_physical != 0 && previous_physical == replacement_physical) {
            // Re-registering the same backing must not add a second reverse
            // address to MultiAddressContainer. Refresh its CPU-side alias.
            AtomicStoreContinuity(old_entry, 0);
            AtomicStoreBacking(old_entry, 0);
            InsertCPUBacking(start_page_d + i, new_vaddress, asid);
            continue;
        }
        if (previous_physical != 0) {
            u32& reverse = compressed_device_addr.GetAndFault(previous_physical - 1U);
            if ((AtomicLoadReverse(reverse) >> MULTI_FLAG_BITS) == 0) {
                if (AtomicLoadReverse(reverse) == device_page) {
                    AtomicStoreReverse(reverse, 0);
                    ::Eden::GpuFault::remap_replaced.fetch_add(1, std::memory_order_relaxed);
                } else if (::Eden::GpuFault::ShouldReportRemapMismatch()) {
                    LOG_WARNING(HW_Memory,
                        "EDEN_GPU_REMAP_REVERSE_MISMATCH guest={:#x} old_phys_page={:#x} reverse={:#x}",
                        device_page, previous_physical - 1U, AtomicLoadReverse(reverse));
                }
            } else {
                if (!impl->multi_dev_address.Contains(device_page, AtomicLoadReverse(reverse) & MULTI_MASK)) {
                    if (::Eden::GpuFault::ShouldReportRemapMismatch())
                        LOG_WARNING(HW_Memory,
                            "EDEN_GPU_REMAP_MULTI_MISSING guest={:#x} old_phys_page={:#x} head={:#x}",
                            device_page, previous_physical - 1U, AtomicLoadReverse(reverse) & MULTI_MASK);
                } else {
                    const auto [multiple, first] = impl->multi_dev_address.Unregister(
                        device_page, AtomicLoadReverse(reverse) & MULTI_MASK);
                    if (!multiple)
                        AtomicStoreReverse(reverse, first != 0 ? impl->multi_dev_address.ReleaseEntry(first) : 0);
                    else
                        AtomicStoreReverse(reverse, first | MULTI_FLAG);
                    ::Eden::GpuFault::remap_replaced.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
#endif
        if (ptr == nullptr) [[unlikely]] {
            auto& invalid = tracked_entries.GetUnchecked(start_page_d + i);
            AtomicStorePhysical(invalid, 0);
            AtomicStoreBacking(invalid, 0);
            AtomicStoreContinuity(invalid, 0);
            continue;
        }
        auto phys_addr = static_cast<u32>(GetRawPhysicalAddr(ptr) >> Memory::YUZU_PAGEBITS) + 1U;
        auto& valid = tracked_entries.GetUnchecked(start_page_d + i);
        AtomicStoreContinuity(valid, 0);
        AtomicStoreBacking(valid, 0);
        AtomicStorePhysical(valid, phys_addr);
        InsertCPUBacking(start_page_d + i, new_vaddress, asid);
        const u32 base_dev = AtomicLoadReverse(compressed_device_addr[phys_addr - 1U]);
        const u32 new_dev = static_cast<u32>(start_page_d + i);
        if (base_dev == 0) [[likely]] {
            AtomicStoreReverse(compressed_device_addr.GetAndFault(phys_addr - 1U), new_dev);
            continue;
        }
        u32 start_id = base_dev & MULTI_MASK;
        if ((base_dev >> MULTI_FLAG_BITS) == 0) {
            start_id = impl->multi_dev_address.Register(base_dev);
            AtomicStoreReverse(compressed_device_addr.GetAndFault(phys_addr - 1U), MULTI_FLAG | start_id);
        }
        impl->multi_dev_address.Register(new_dev, start_id);
    }
    t_slot = {};
    if (track) {
        TrackContinuityImpl(address, virtual_address, size, asid);
    }
#ifdef PS5_NATIVE
    // Invalidate only real remaps, outside mapping_guard because the
    // renderer callback may itself gather translated GPU addresses.
    lk.unlock();
    if (replaced_gpu_mapping && device_inter != nullptr) {
        device_inter->InvalidateRegion(address, size);
        ::Eden::GpuFault::remap_cache_evictions.fetch_add(1, std::memory_order_relaxed);
    }
#endif
}

template <typename Traits>
void DeviceMemoryManager<Traits>::Unmap(DAddr address, size_t size) {
#ifdef PS5_NATIVE
    if (size == 0) return;
    if (address >= device_as_size || size > device_as_size - address) {
        if (::Eden::GpuFault::ShouldReportBadUnmapRange())
            LOG_ERROR(HW_Memory, "EDEN_GPU_UNMAP_BAD_RANGE gpu={:#x} bytes={:#x}", address, size);
        return;
    }
#endif
    size_t start_page_d = address >> Memory::YUZU_PAGEBITS;
    size_t num_pages = Common::AlignUp(size, Memory::YUZU_PAGESIZE) >> Memory::YUZU_PAGEBITS;
    device_inter->InvalidateRegion(address, size);
    std::scoped_lock lk(mapping_guard);

    tracked_entries.CommitRegion(start_page_d, start_page_d + num_pages); // should already be committed, but just in case
    for (size_t i = 0; i < num_pages; i++) {
        auto& entry = tracked_entries.GetUnchecked(start_page_d + i);
        auto phys_addr = AtomicLoadPhysical(entry);
        AtomicStorePhysical(entry, 0);
        AtomicStoreBacking(entry, 0);
        AtomicStoreContinuity(entry, 0);
        if (phys_addr != 0) [[likely]] {
            u32& base_dev = compressed_device_addr.GetAndFault(phys_addr - 1U);
            if ((AtomicLoadReverse(base_dev) >> MULTI_FLAG_BITS) == 0) [[likely]] {
#ifdef PS5_NATIVE
                // Never erase a different GPU page's reverse mapping.
                // This can occur after malformed or repeated guest remaps.
                const u32 retiring_page = static_cast<u32>(start_page_d + i);
                if (AtomicLoadReverse(base_dev) != retiring_page) {
                    if (::Eden::GpuFault::ShouldReportRemapMismatch())
                        LOG_WARNING(HW_Memory,
                            "EDEN_GPU_UNMAP_REVERSE_MISMATCH guest={:#x} old_phys_page={:#x} reverse={:#x}",
                            retiring_page, phys_addr - 1U, AtomicLoadReverse(base_dev));
                    continue;
                }
#endif
                AtomicStoreReverse(base_dev, 0);
                continue;
            }
#ifdef PS5_NATIVE
            // An invalid stale multi-list used to index storage[-1] and crash
            // in Unregister(). Preserve the unrelated reverse association.
            if (!impl->multi_dev_address.Contains(static_cast<u32>(start_page_d + i),
                                                    AtomicLoadReverse(base_dev) & MULTI_MASK)) {
                if (::Eden::GpuFault::ShouldReportRemapMismatch())
                    LOG_WARNING(HW_Memory,
                        "EDEN_GPU_UNMAP_MULTI_MISSING guest={:#x} old_phys_page={:#x} head={:#x}",
                        static_cast<u32>(start_page_d + i), phys_addr - 1U, AtomicLoadReverse(base_dev) & MULTI_MASK);
                continue;
            }
#endif
            const auto [more_entries, new_start] = impl->multi_dev_address.Unregister(
                static_cast<u32>(start_page_d + i), AtomicLoadReverse(base_dev) & MULTI_MASK);
            if (!more_entries) {
                AtomicStoreReverse(base_dev, new_start != 0 ? impl->multi_dev_address.ReleaseEntry(new_start) : 0);
                continue;
            }
            AtomicStoreReverse(base_dev, new_start | MULTI_FLAG);
        }
    }
    t_slot = {};
}
template <typename Traits>
void DeviceMemoryManager<Traits>::TrackContinuityImpl(DAddr address, VAddr virtual_address,
                                                      size_t size, Asid asid) {
    // Called with process_registry_guard held by Map or TrackContinuity.
    // Do not decode stale ASIDs or underflow/overflow page intervals.
    if (size == 0 || address >= device_as_size || size > device_as_size - address ||
        virtual_address >= guest_as_size || size > guest_as_size - virtual_address ||
        asid.id >= registered_processes.size() || registered_processes[asid.id] == nullptr)
        return;
    Core::Memory::Memory* process_memory = registered_processes[asid.id];
    size_t start_page_d = address >> Memory::YUZU_PAGEBITS;
    size_t num_pages = Common::AlignUp(size, Memory::YUZU_PAGESIZE) >> Memory::YUZU_PAGEBITS;
    uintptr_t last_ptr = 0;
    size_t page_count = 1;

    tracked_entries.CommitRegion(start_page_d, start_page_d + num_pages);
    for (size_t i = num_pages; i > 0; i--) {
        size_t index = i - 1;
        const VAddr new_vaddress = virtual_address + index * Memory::YUZU_PAGESIZE;
        const uintptr_t new_ptr = reinterpret_cast<uintptr_t>(
            process_memory->GetPointerSilent(Common::ProcessAddress(new_vaddress)));
        if (new_ptr != 0 && last_ptr != 0 && new_ptr + page_size == last_ptr) {
            page_count++;
        } else {
            page_count = 1;
        }
        last_ptr = new_ptr;
        AtomicStoreContinuity(tracked_entries.GetUnchecked(start_page_d + index), static_cast<u32>(page_count) - 1);
    }
}
template <typename Traits>
u8* DeviceMemoryManager<Traits>::GetSpan(const DAddr src_addr, const std::size_t size) {
    if (size == 0 || src_addr >= device_as_size || size > device_as_size - src_addr)
        return nullptr;
    const size_t first_page = src_addr >> page_bits;
    const size_t page_count = 1 + ((size - 1 + (src_addr & page_mask)) >> page_bits);
    const u32 backing = AtomicLoadPhysical(tracked_entries[first_page]);
    if (backing == 0) return nullptr;
#ifdef PS5_NATIVE
    // Matching forward-page numbers do not by themselves prove that the
    // complete multi-page span fits in the host DRAM backing.
    const size_t first_phys = static_cast<size_t>(backing - 1U);
    if (first_phys >= compressed_device_addr.size() ||
        page_count > compressed_device_addr.size() - first_phys)
        return nullptr;
#endif
    // Validate each physical page, not a potentially stale continuity hint.
    for (size_t i = 1; i < page_count; ++i) {
        if (AtomicLoadPhysical(tracked_entries[first_page + i]) != backing + i)
            return nullptr;
    }
    return GetPointer<u8>(src_addr);
}

template <typename Traits>
const u8* DeviceMemoryManager<Traits>::GetSpan(const DAddr src_addr, const std::size_t size) const {
    if (size == 0 || src_addr >= device_as_size || size > device_as_size - src_addr)
        return nullptr;
    const size_t first_page = src_addr >> page_bits;
    const size_t page_count = 1 + ((size - 1 + (src_addr & page_mask)) >> page_bits);
    const u32 backing = AtomicLoadPhysical(tracked_entries[first_page]);
    if (backing == 0) return nullptr;
#ifdef PS5_NATIVE
    // Matching forward-page numbers do not by themselves prove that the
    // complete multi-page span fits in the host DRAM backing.
    const size_t first_phys = static_cast<size_t>(backing - 1U);
    if (first_phys >= compressed_device_addr.size() ||
        page_count > compressed_device_addr.size() - first_phys)
        return nullptr;
#endif
    // Validate each physical page, not a potentially stale continuity hint.
    for (size_t i = 1; i < page_count; ++i) {
        if (AtomicLoadPhysical(tracked_entries[first_page + i]) != backing + i)
            return nullptr;
    }
    return GetPointer<u8>(src_addr);
}

template <typename Traits>
void DeviceMemoryManager<Traits>::InnerGatherDeviceAddresses(Common::ScratchBuffer<u32>& buffer,
                                                             PAddr address) {
    size_t phys_addr = address >> page_bits;
#ifdef PS5_NATIVE
    if (phys_addr >= compressed_device_addr.size()) {
        if (::Eden::GpuFault::ShouldReportBadPhysical())
            LOG_WARNING(HW_Memory, "EDEN_GPU_REVERSE_PHYS_OOB phys={:#x}", address);
        buffer.resize(0);
        return;
    }
#endif
    std::scoped_lock lk(mapping_guard);
    u32 backing = AtomicLoadReverse(compressed_device_addr[phys_addr]);
    if ((backing >> MULTI_FLAG_BITS) != 0) {
        impl->multi_dev_address.GatherValues(backing & MULTI_MASK, buffer);
        return;
    }
    buffer.resize(1);
    buffer[0] = backing;
}

template <typename Traits>
template <typename T>
T* DeviceMemoryManager<Traits>::GetPointer(DAddr address) {
#ifdef PS5_NATIVE
    if (address >= device_as_size) return nullptr;
#endif
    const size_t index = address >> Memory::YUZU_PAGEBITS;
    const size_t offset = address & Memory::YUZU_PAGEMASK;
    auto phys_addr = AtomicLoadPhysical(tracked_entries[index]);
#ifdef PS5_NATIVE
    if (phys_addr == 0 || static_cast<size_t>(phys_addr - 1U) >= compressed_device_addr.size())
        return nullptr;
    const auto raw = (static_cast<PAddr>(phys_addr - 1U) << Memory::YUZU_PAGEBITS) + offset;
    const auto backing_size = compressed_device_addr.size() << Memory::YUZU_PAGEBITS;
    if (raw >= backing_size || sizeof(T) > backing_size - raw) return nullptr;
#endif
    if (phys_addr == 0) [[unlikely]]
        return nullptr;
    return GetPointerFromRaw<T>((PAddr(phys_addr - 1) << Memory::YUZU_PAGEBITS) + offset);
}

template <typename Traits>
template <typename T>
const T* DeviceMemoryManager<Traits>::GetPointer(DAddr address) const {
#ifdef PS5_NATIVE
    if (address >= device_as_size) return nullptr;
#endif
    const size_t index = address >> Memory::YUZU_PAGEBITS;
    const size_t offset = address & Memory::YUZU_PAGEMASK;
    auto phys_addr = AtomicLoadPhysical(tracked_entries[index]);
#ifdef PS5_NATIVE
    if (phys_addr == 0 || static_cast<size_t>(phys_addr - 1U) >= compressed_device_addr.size())
        return nullptr;
    const auto raw = (static_cast<PAddr>(phys_addr - 1U) << Memory::YUZU_PAGEBITS) + offset;
    const auto backing_size = compressed_device_addr.size() << Memory::YUZU_PAGEBITS;
    if (raw >= backing_size || sizeof(T) > backing_size - raw) return nullptr;
#endif
    if (phys_addr == 0)
        return nullptr;
    return GetPointerFromRaw<T>((PAddr(phys_addr - 1) << Memory::YUZU_PAGEBITS) + offset);
}

template <typename Traits>
template <typename T>
void DeviceMemoryManager<Traits>::Write(DAddr address, T value) {
    T* ptr = GetPointer<T>(address);
    if (!ptr) [[unlikely]] {
        return;
    }
    std::memcpy(ptr, &value, sizeof(T));
}

template <typename Traits>
template <typename T>
T DeviceMemoryManager<Traits>::Read(DAddr address) const {
    const T* ptr = GetPointer<T>(address);
    T result{};
    if (!ptr) [[unlikely]] {
        return result;
    }
    std::memcpy(&result, ptr, sizeof(T));
    return result;
}

template <typename Traits>
void DeviceMemoryManager<Traits>::WalkBlock(DAddr addr, std::size_t size, auto on_unmapped, auto on_memory, auto increment) {
    if (size == 0) return;
#ifdef PS5_NATIVE
    // Guest command streams can supply a corrupt GPU virtual address. Never
    // index sparse translation tables beyond their advertised address space.
    if (addr >= device_as_size || size > device_as_size - addr) {
        on_unmapped(size, addr);
        increment(size);
        return;
    }
#endif
    std::size_t remaining_size = size;
    std::size_t page_index = addr >> Memory::YUZU_PAGEBITS;
    std::size_t page_offset = addr & Memory::YUZU_PAGEMASK;
    while (remaining_size) {
        const auto first_backing = AtomicLoadPhysical(tracked_entries[page_index]);
        // A remap can invalidate continuity even on a preceding page.
        const size_t hint = static_cast<size_t>(AtomicLoadContinuity(tracked_entries[page_index])) + 1;
        const size_t remaining_pages = 1 + ((remaining_size - 1 + page_offset) >> Memory::YUZU_PAGEBITS);
        size_t next_pages = (std::min)({hint, remaining_pages, tracked_entries.size() - page_index});
#ifdef PS5_NATIVE
        // A continuity hint does not prove that all decoded physical pages
        // fit inside the configured 4/6/8/10/12-GiB backing allocation.
        // Split exactly at the physical boundary; keep the single-page path O(1).
        if (first_backing != 0) {
            const size_t first_phys = static_cast<size_t>(first_backing - 1U);
            next_pages = first_phys >= compressed_device_addr.size()
                ? 1 : (std::min)(next_pages, compressed_device_addr.size() - first_phys);
        }
#endif
        for (size_t n = 1; n < next_pages; ++n) {
            const auto observed = AtomicLoadPhysical(tracked_entries[page_index + n]);
            const bool contiguous = first_backing == 0 ? observed == 0 : observed == first_backing + n;
            if (!contiguous) { next_pages = n; break; }
        }
        const std::size_t copy_amount = (std::min)((next_pages << Memory::YUZU_PAGEBITS) - page_offset, remaining_size);
        const auto current_vaddr = u64((page_index << Memory::YUZU_PAGEBITS) + page_offset);
        SCOPE_EXIT{
            page_index += next_pages;
            page_offset = 0;
            increment(copy_amount);
            remaining_size -= copy_amount;
        };

        auto phys_addr = AtomicLoadPhysical(tracked_entries[page_index]);
#ifdef PS5_NATIVE
        if (phys_addr != 0 &&
            static_cast<size_t>(phys_addr - 1U) >= compressed_device_addr.size()) {
            if (::Eden::GpuFault::ShouldReportBadPhysical())
                LOG_WARNING(HW_Memory,
                    "EDEN_GPU_READ_WRITE_PHYS_OOB gpu={:#x} phys_page={:#x}",
                    current_vaddr, phys_addr - 1U);
            on_unmapped(copy_amount, current_vaddr);
            continue;
        }
#endif
        if (phys_addr == 0) {
            on_unmapped(copy_amount, current_vaddr);
            continue;
        }
        auto* mem_ptr = GetPointerFromRaw<u8>((PAddr(phys_addr - 1) << Memory::YUZU_PAGEBITS) + page_offset);
        on_memory(copy_amount, mem_ptr);
    }
}

template <typename Traits>
void DeviceMemoryManager<Traits>::ReadBlock(DAddr address, void* dest_pointer, size_t size) {
#ifdef PS5_NATIVE
    if (size == 0) return;
    // Do not flush an invalid GPU virtual extent through the renderer.
    // The ordinary unmapped path zero-fills destination without a GPU fetch.
    if (address >= device_as_size || size > device_as_size - address) {
        if (::Eden::GpuFault::ShouldReportRead())
            LOG_ERROR(HW_Memory,
                      "Unmapped Device ReadBlock @ {:#016x} (start address = {:#016x}, size = {})",
                      address, address, size);
        std::memset(dest_pointer, 0, size);
        return;
    }
#endif
    device_inter->FlushRegion(address, size);
    const std::size_t page_offset = address & Memory::YUZU_PAGEMASK;
    if (size <= Memory::YUZU_PAGESIZE - page_offset
#ifdef PS5_NATIVE
        && address < device_as_size && size <= device_as_size - address
#endif
    ) {
        const DAddr guest_page = address & ~static_cast<DAddr>(Memory::YUZU_PAGEMASK);
#ifndef PS5_NATIVE
        for (size_t i = 0; i < 4; ++i) {
            if (t_slot[i].guest_page == guest_page && t_slot[i].host_ptr != nullptr) {
                std::memcpy(dest_pointer, t_slot[i].host_ptr + page_offset, size);
                return;
            }
        }
#endif

        const std::size_t page_index = address >> Memory::YUZU_PAGEBITS;
        const auto phys_addr = AtomicLoadPhysical(tracked_entries[page_index]);
        if (phys_addr != 0
#ifdef PS5_NATIVE
            && static_cast<size_t>(phys_addr - 1U) < compressed_device_addr.size()
#endif
        ) {
            auto* const mem_ptr = GetPointerFromRaw<u8>((PAddr(phys_addr - 1) << Memory::YUZU_PAGEBITS));
#ifndef PS5_NATIVE
            t_slot[cache_cursor % t_slot.size()] = TranslationEntry{.guest_page = guest_page, .host_ptr = mem_ptr};
            cache_cursor = (cache_cursor + 1) & 3U;
#endif
            std::memcpy(dest_pointer, mem_ptr + page_offset, size);
            return;
        }
    }
    WalkBlock(
        address, size,
        [&](size_t copy_amount, DAddr current_vaddr) {
#ifdef PS5_NATIVE
            if (::Eden::GpuFault::ShouldReportRead())
#endif
            LOG_ERROR(
                HW_Memory,
                "Unmapped Device ReadBlock @ {:#016x} (start address = {:#016x}, size = {})",
                current_vaddr, address, size);
            std::memset(dest_pointer, 0, copy_amount);
        },
        [&](size_t copy_amount, const u8* const src_ptr) {
            std::memcpy(dest_pointer, src_ptr, copy_amount);
        },
        [&](const std::size_t copy_amount) {
            dest_pointer = static_cast<u8*>(dest_pointer) + copy_amount;
        });
}

template <typename Traits>
void DeviceMemoryManager<Traits>::WriteBlock(DAddr address, const void* src_pointer, size_t size) {
    WalkBlock(
        address, size,
        [&](size_t copy_amount, DAddr current_vaddr) {
#ifdef PS5_NATIVE
            if (::Eden::GpuFault::ShouldReportWrite())
#endif
            LOG_ERROR(
                HW_Memory,
                "Unmapped Device WriteBlock @ {:#016x} (start address = {:#016x}, size = {})",
                current_vaddr, address, size);
        },
        [&](size_t copy_amount, u8* const dst_ptr) {
            std::memcpy(dst_ptr, src_pointer, copy_amount);
        },
        [&](const std::size_t copy_amount) {
            src_pointer = static_cast<const u8*>(src_pointer) + copy_amount;
        });
#ifdef PS5_NATIVE
    // WalkBlock already dropped and counted invalid writes. Do not pass the
    // rejected virtual range into renderer cache invalidation afterwards.
    if (size != 0 && address < device_as_size && size <= device_as_size - address)
#endif
        device_inter->InvalidateRegion(address, size);
}

template <typename Traits>
void DeviceMemoryManager<Traits>::ReadBlockUnsafe(DAddr address, void* dest_pointer, size_t size) {
    const std::size_t page_offset = address & Memory::YUZU_PAGEMASK;
    if (size <= Memory::YUZU_PAGESIZE - page_offset
#ifdef PS5_NATIVE
        && address < device_as_size && size <= device_as_size - address
#endif
    ) {
        const DAddr guest_page = address & ~static_cast<DAddr>(Memory::YUZU_PAGEMASK);
#ifndef PS5_NATIVE
        for (size_t i = 0; i < 4; ++i) {
            if (t_slot[i].guest_page == guest_page && t_slot[i].host_ptr != nullptr) {
                std::memcpy(dest_pointer, t_slot[i].host_ptr + page_offset, size);
                return;
            }
        }
#endif

        const std::size_t page_index = address >> Memory::YUZU_PAGEBITS;
        const auto phys_addr = AtomicLoadPhysical(tracked_entries[page_index]);
        if (phys_addr != 0
#ifdef PS5_NATIVE
            && static_cast<size_t>(phys_addr - 1U) < compressed_device_addr.size()
#endif
        ) {
            auto* const mem_ptr = GetPointerFromRaw<u8>((PAddr(phys_addr - 1) << Memory::YUZU_PAGEBITS));
#ifndef PS5_NATIVE
            t_slot[cache_cursor % t_slot.size()] = TranslationEntry{.guest_page = guest_page, .host_ptr = mem_ptr};
            cache_cursor = (cache_cursor + 1) & 3U;
#endif
            std::memcpy(dest_pointer, mem_ptr + page_offset, size);
            return;
        }
    }
    WalkBlock(
        address, size,
        [&](size_t copy_amount, DAddr current_vaddr) {
#ifdef PS5_NATIVE
            if (::Eden::GpuFault::ShouldReportRead())
#endif
            LOG_ERROR(
                HW_Memory,
                "Unmapped Device ReadBlock @ {:#016x} (start address = {:#016x}, size = {})",
                current_vaddr, address, size);
            std::memset(dest_pointer, 0, copy_amount);
        },
        [&](size_t copy_amount, const u8* const src_ptr) {
            std::memcpy(dest_pointer, src_ptr, copy_amount);
        },
        [&](const std::size_t copy_amount) {
            dest_pointer = static_cast<u8*>(dest_pointer) + copy_amount;
        });
}

template <typename Traits>
void DeviceMemoryManager<Traits>::WriteBlockUnsafe(DAddr address, const void* src_pointer,
                                                   size_t size) {
    WalkBlock(
        address, size,
        [&](size_t copy_amount, DAddr current_vaddr) {
#ifdef PS5_NATIVE
            if (::Eden::GpuFault::ShouldReportWrite())
#endif
            LOG_ERROR(
                HW_Memory,
                "Unmapped Device WriteBlock @ {:#016x} (start address = {:#016x}, size = {})",
                current_vaddr, address, size);
        },
        [&](size_t copy_amount, u8* const dst_ptr) {
            std::memcpy(dst_ptr, src_pointer, copy_amount);
        },
        [&](const std::size_t copy_amount) {
            src_pointer = static_cast<const u8*>(src_pointer) + copy_amount;
        });
}

template <typename Traits>
Asid DeviceMemoryManager<Traits>::RegisterProcess(Memory::Memory* memory_device_inter) {
    std::unique_lock registry_lk(process_registry_guard);
    // A recycled ASID can alias a delayed GPU mapping from an old process.
    // Keep unregistered entries as tombstones until the manager is destroyed.
    // One pointer-sized slot per process is cheaper than a stale GPU alias.
    constexpr size_t max_ids = size_t{1} << (sizeof(VAddr) * 8 - guest_max_as_bits);
    if (memory_device_inter == nullptr || registered_processes.size() >= max_ids)
        return Asid{static_cast<size_t>(-1)};
    registered_processes.emplace_back(memory_device_inter);
    return Asid{registered_processes.size() - 1U};
}

template <typename Traits>
void DeviceMemoryManager<Traits>::UnregisterProcess(Asid asid) {
    std::unique_lock registry_lk(process_registry_guard);
    // A duplicated unregister previously enqueued the same ASID twice,
    // permitting two live processes to share one registry index.
    if (asid.id >= registered_processes.size() || registered_processes[asid.id] == nullptr)
        return;
    // Never recycle this index: stale tracked GPU pages retain its ASID.
    registered_processes[asid.id] = nullptr;
}

template <typename Traits>
void DeviceMemoryManager<Traits>::UpdatePagesCachedCountNoLock(DAddr addr, size_t size, s32 delta) {
    if (size == 0 || addr >= device_as_size || size > device_as_size - addr)
        return;
    u64 uncache_begin = 0;
    u64 cache_begin = 0;
    u64 uncache_bytes = 0;
    u64 cache_bytes = 0;
    const auto MarkRegionCaching = &DeviceMemoryManager<Traits>::DeviceMethods::MarkRegionCaching;

    std::atomic_thread_fence(std::memory_order_acquire);
    const size_t page_end = Common::DivCeil(addr + size, Memory::YUZU_PAGESIZE);
    size_t page = addr >> Memory::YUZU_PAGEBITS;
    auto [asid, base_vaddress] = ExtractCPUBacking(page);
    auto* memory_device_inter = asid.id < registered_processes.size()
        ? registered_processes[asid.id] : nullptr;
    const auto release_pending = [&] {
        if (uncache_bytes > 0) {
            if (memory_device_inter != nullptr) {
                MarkRegionCaching(memory_device_inter, uncache_begin << Memory::YUZU_PAGEBITS,
                                  uncache_bytes, false);
            }
            uncache_bytes = 0;
        }
        if (cache_bytes > 0) {
            if (memory_device_inter != nullptr) {
                MarkRegionCaching(memory_device_inter, cache_begin << Memory::YUZU_PAGEBITS,
                                  cache_bytes, true);
            }
            cache_bytes = 0;
        }
    };
    size_t old_vpage = (base_vaddress >> Memory::YUZU_PAGEBITS) - 1;
    for (; page != page_end; ++page) {
        CounterAtomicType& count = cached_pages->at(page >> subentries_shift).Count(page);
        auto [asid_2, vpage] = ExtractCPUBacking(page);
        vpage >>= Memory::YUZU_PAGEBITS;

        if (vpage == 0 || asid_2.id >= registered_processes.size() ||
            registered_processes[asid_2.id] == nullptr) [[unlikely]] {
            release_pending();
            continue;
        }

        if (asid.id != asid_2.id) [[unlikely]] {
            release_pending();
            asid = asid_2;
            memory_device_inter = registered_processes[asid_2.id];
        }

        if (vpage != old_vpage + 1) [[unlikely]] {
            release_pending();
        }

        old_vpage = vpage;

        // Adds or subtracts 1, as count is a unsigned 8-bit value
        count.fetch_add(static_cast<CounterType>(delta), std::memory_order_release);

        // Assume delta is either -1 or 1
        if (count.load(std::memory_order::relaxed) == 0) {
            if (uncache_bytes == 0) {
                uncache_begin = vpage;
            }
            uncache_bytes += Memory::YUZU_PAGESIZE;
        } else if (uncache_bytes > 0) {
            if (memory_device_inter != nullptr)
                MarkRegionCaching(memory_device_inter, uncache_begin << Memory::YUZU_PAGEBITS,
                                  uncache_bytes, false);
            uncache_bytes = 0;
        }
        if (count.load(std::memory_order::relaxed) == 1 && delta > 0) {
            if (cache_bytes == 0) {
                cache_begin = vpage;
            }
            cache_bytes += Memory::YUZU_PAGESIZE;
        } else if (cache_bytes > 0) {
            if (memory_device_inter != nullptr)
                MarkRegionCaching(memory_device_inter, cache_begin << Memory::YUZU_PAGEBITS,
                                  cache_bytes, true);
            cache_bytes = 0;
        }
    }
    release_pending();
}

template <typename Traits>
void DeviceMemoryManager<Traits>::UpdatePagesCachedCount(DAddr addr, size_t size, s32 delta) {
    if (size == 0 || addr >= device_as_size || size > device_as_size - addr)
        return;
    std::shared_lock registry_lk(process_registry_guard);
    Common::ScopedRangeLock lk(counter_guard, addr, size);
    UpdatePagesCachedCountNoLock(addr, size, delta);
}

template <typename Traits>
void DeviceMemoryManager<Traits>::UpdatePagesCachedBatch(std::span<const std::pair<DAddr, size_t>> ranges, s32 delta) {
    if (ranges.empty()) {
        return;
    }
    // Reject malformed GPU ranges before any lock or wrapped addr+size.
    for (const auto& [start, length] : ranges)
        if (start >= device_as_size || length > device_as_size - start)
            return;
    std::shared_lock registry_lk(process_registry_guard);
    // Make a local copy and sort by address
    std::vector<std::pair<DAddr, size_t>> tmp(ranges.begin(), ranges.end());
    std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // Coalesce adjacent/overlapping ranges
    std::vector<std::pair<DAddr, size_t>> coalesced;
    DAddr cur_addr = tmp[0].first;
    size_t cur_size = tmp[0].second;
    for (size_t i = 1; i < tmp.size(); ++i) {
        DAddr next_addr = tmp[i].first;
        size_t next_size = tmp[i].second;
        if (cur_addr + cur_size >= next_addr) {
            // overlapping or contiguous
            const DAddr end = std::max(cur_addr + cur_size, next_addr + next_size);
            cur_size = end - cur_addr;
        } else {
            coalesced.emplace_back(cur_addr, cur_size);
            cur_addr = next_addr;
            cur_size = next_size;
        }
    }
    coalesced.emplace_back(cur_addr, cur_size);

    const DAddr lock_begin = coalesced.front().first;
    const DAddr lock_end = coalesced.back().first + coalesced.back().second;
    Common::ScopedRangeLock lk(counter_guard, lock_begin, static_cast<size_t>(lock_end - lock_begin));

    for (const auto& [addr, size] : coalesced) {
        UpdatePagesCachedCountNoLock(addr, size, delta);
    }
}

} // namespace Core
