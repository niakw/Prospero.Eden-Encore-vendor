// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: 2021 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <utility>

#include "common/alignment.h"
#include "common/assert.h"
#include <limits>
#include <set>
#include <vector>
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/nvdrv/devices/native_gpu_kind.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/devices/ioctl_serialization.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "video_core/control/channel_state.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace Service::Nvidia::Devices {

nvhost_as_gpu::nvhost_as_gpu(Core::System& system_, Module& module_, NvCore::Container& core)
    : nvdevice{system_}, module{module_}, container{core}, nvmap{core.GetNvMapFile()}, vm{},
      gmmu{} {}

nvhost_as_gpu::~nvhost_as_gpu() {
    // Closing an AS without explicit unmaps must not strand NvMap pins.
    const std::scoped_lock lock{mutex};
    for (const auto& [address, mapping] : mapping_map) {
        nvmap.UnpinHandle(mapping.handle);
    }
    mapping_map.clear();
    for (const auto& [handle, address] : native_sparse_pins) {
        nvmap.UnpinHandle(handle);
    }
    native_sparse_pins.clear();
    if (m_native_process) m_native_process->Close(system.Kernel());
}

NvResult nvhost_as_gpu::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<u8> output) {
    switch (command.group) {
    case 'A':
        switch (command.cmd) {
        case 0x1:
            return WrapFixed(this, &nvhost_as_gpu::BindChannel, input, output);
        case 0x2:
            return WrapFixed(this, &nvhost_as_gpu::AllocateSpace, input, output);
        case 0x3:
            return WrapFixed(this, &nvhost_as_gpu::FreeSpace, input, output);
        case 0x5:
            return WrapFixed(this, &nvhost_as_gpu::UnmapBuffer, input, output);
        case 0x6:
            return WrapFixed(this, &nvhost_as_gpu::MapBufferEx, input, output);
        case 0x8:
            return WrapFixed(this, &nvhost_as_gpu::GetVARegions1, input, output);
        case 0x9:
            return WrapFixed(this, &nvhost_as_gpu::AllocAsEx, input, output);
        case 0x14:
            return WrapVariable(this, &nvhost_as_gpu::Remap, input, output);
        default:
            break;
        }
        break;
    default:
        break;
    }

    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_as_gpu::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<const u8> inline_input, std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_as_gpu::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<u8> output, std::span<u8> inline_output) {
    switch (command.group) {
    case 'A':
        switch (command.cmd) {
        case 0x8:
            return WrapFixedInlOut(this, &nvhost_as_gpu::GetVARegions3, input, output,
                                   inline_output);
        default:
            break;
        }
        break;
    default:
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvhost_as_gpu::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {
    if (const auto* session = container.GetSession(session_id)) {
        m_native_process = session->process;
        if (m_native_process) m_native_process->Open(system.Kernel());
    }
}
void nvhost_as_gpu::OnClose(DeviceFD fd) {}

// NS2 research: operate on a real, exclusive, 4-KiB guest GMMU allocation.
// Unlike the old fixed IOCTL path, validation+reservation+mapping happen
// under one address-space mutex and no failed/overlap request returns success.
NvResult nvhost_as_gpu::MapNative4K(u32 handle_id, u64 address, u64 source, u64 size, u8 kind) {
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!m_native_process || !handle_id || !size || (address | source | size) & (page - 1) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max() ||
        source > std::numeric_limits<u64>::max() - size)
        return NvResult::BadParameter;
    const auto handle = nvmap.GetHandle(handle_id);
    if (!handle || !handle->allocated || source > handle->orig_size ||
        size > handle->orig_size - source)
        return NvResult::BadParameter;
    const auto* session = container.GetSession(handle->session_id);
    if (!session || session->process != m_native_process)
        return NvResult::BadParameter;

    const std::scoped_lock lock{mutex};
    if (!vm.initialised || address < vm.va_range_start || address >= vm.va_range_split ||
        size > vm.va_range_split - address || !vm.small_page_allocator)
        return NvResult::BadParameter;

    // Check both reserved mappings and raw legacy mappings, including the
    // predecessor; an unchecked AllocateFixed would silently overwrite them.
    const auto reservation = allocation_map.lower_bound(address);
    if ((reservation != allocation_map.end() && reservation->first < address + size) ||
        (reservation != allocation_map.begin() &&
         std::prev(reservation)->first + std::prev(reservation)->second.size > address))
        return NvResult::BadParameter;
    const auto next = mapping_map.lower_bound(address);
    if ((next != mapping_map.end() && next->first < address + size) ||
        (next != mapping_map.begin() &&
         std::prev(next)->first + std::prev(next)->second.size > address))
        return NvResult::BadParameter;

    // The 4K small-page allocator uses 32-bit page numbers, but the actual
    // address/size values and all overlap checks above remain 64-bit.
    if ((address >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max() ||
        ((address + size) >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const DAddr pinned = nvmap.PinHandle(handle_id, false);
    if (!pinned) return NvResult::InsufficientMemory;
    vm.small_page_allocator->AllocateFixed(static_cast<u32>(address >> VM::PAGE_SIZE_BITS),
                                           static_cast<u32>(size >> VM::PAGE_SIZE_BITS));
    allocation_map.emplace(address, Allocation{{}, size, VM::YUZU_PAGESIZE, false, false});
    allocation_map.at(address).mappings.push_back(address);
    mapping_map.emplace(address, Mapping(handle_id, pinned + source, address, size,
                                         true, false, false));
    gmmu->Map(address, pinned + source, size, static_cast<Tegra::PTEKind>(kind), false);
    return NvResult::Success;
}

NvResult nvhost_as_gpu::ReserveNativeSparse4K(u64 address, u64 size) {
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!m_native_process || !size || ((address | size) & (page - 1)) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!vm.initialised || !vm.small_page_allocator ||
        address < vm.va_range_start || address >= vm.va_range_split ||
        size > vm.va_range_split - address ||
        native_sparse_regions.size() >= 4096)
        return NvResult::BadParameter;
    const auto reservation = allocation_map.lower_bound(address);
    if ((reservation != allocation_map.end() && reservation->first < address + size) ||
        (reservation != allocation_map.begin() &&
         std::prev(reservation)->first + std::prev(reservation)->second.size > address))
        return NvResult::BadParameter;
    const auto mapping = mapping_map.lower_bound(address);
    if ((mapping != mapping_map.end() && mapping->first < address + size) ||
        (mapping != mapping_map.begin() &&
         std::prev(mapping)->first + std::prev(mapping)->second.size > address))
        return NvResult::BadParameter;
    if ((address >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max() ||
        ((address + size) >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const u32 base = static_cast<u32>(address >> VM::PAGE_SIZE_BITS);
    const u32 pages = static_cast<u32>(size >> VM::PAGE_SIZE_BITS);
    vm.small_page_allocator->AllocateFixed(base, pages);
    allocation_map.emplace(address, Allocation{{}, size, VM::YUZU_PAGESIZE, true, false});
    native_sparse_regions.emplace(address, size);
    gmmu->MapSparse(address, size, false);
    return NvResult::Success;
}

// Remap the contents of already-reserved native 4-KiB sparse VA only.
// Validate the COMPLETE batch, prepare new pins and mapping-table ownership,
// then modify GMMU pages in order. No guest page-table change occurs before
// all errors that can be reported have been checked.
NvResult nvhost_as_gpu::RemapNativeSparse(std::span<const NativeSparseRemapEntry> entries) {
    if (!m_native_process || entries.empty() || entries.size() > 256)
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!vm.initialised || !gmmu) return NvResult::InvalidState;

    NativeSparseMappingTable updated = native_sparse_mappings;
    std::vector<NativeSparseRange> ranges;
    std::vector<NativeMemoryKind> kinds;
    ranges.reserve(entries.size());
    kinds.reserve(entries.size());
    for (const auto& entry : entries) {
        const auto range = DecodeNativeSparseRemap(entry);
        if (!range || (u32{entry.flags} & 0x7000U) != 0x1000U)
            return NvResult::BadParameter;

        const auto region = native_sparse_regions.upper_bound(range->address);
        if (region == native_sparse_regions.begin()) return NvResult::BadParameter;
        const auto owner = std::prev(region);
        if (range->address < owner->first ||
            range->address - owner->first >= owner->second ||
            range->size > owner->second - (range->address - owner->first))
            return NvResult::BadParameter;

        NativeMemoryKind kind{};
        if (range->handle) {
            const auto handle = nvmap.GetHandle(range->handle);
            const auto* session = handle ? container.GetSession(handle->session_id) : nullptr;
            if (!handle || !handle->allocated || !session ||
                session->process != m_native_process ||
                range->source_offset > handle->orig_size ||
                range->size > handle->orig_size - range->source_offset)
                return NvResult::BadParameter;
            if (u16{entry.kind} > 255 || u16{entry.fallback_kind} > 255)
                return NvResult::NotSupported;
            const auto selected = SelectNativeMemoryKind(
                static_cast<u8>(u16{entry.kind}),
                static_cast<u8>(u16{entry.fallback_kind}));
            if (!selected) return NvResult::NotSupported;
            kind = *selected;
        }
        updated.Replace(*range);
        ranges.push_back(*range);
        kinds.push_back(kind);
    }
    if (updated.Ranges().size() > 65536) return NvResult::InsufficientMemory;
    const std::set<u32> retained = updated.ReferencedHandles();
    if (retained.size() > 4096) return NvResult::InsufficientMemory;

    // A handle overwritten later in this batch still needs a pin until
    // all intermediate GMMU updates are applied in order.
    auto needed = retained;
    for (const auto& range : ranges) if (range.handle) needed.insert(range.handle);
    if (needed.size() > 4096) return NvResult::InsufficientMemory;
    std::map<u32, DAddr> newly_pinned;
    for (const u32 handle : needed) {
        if (native_sparse_pins.contains(handle)) continue;
        const DAddr pinned = nvmap.PinHandle(handle, false);
        if (!pinned) {
            for (const auto& [id, address] : newly_pinned) nvmap.UnpinHandle(id);
            return NvResult::InsufficientMemory;
        }
        newly_pinned.emplace(handle, pinned);
    }

    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const auto& range = ranges[i];
        if (!range.handle) {
            gmmu->MapSparse(range.address, range.size, false);
            continue;
        }
        const auto old = native_sparse_pins.find(range.handle);
        const DAddr pinned = old == native_sparse_pins.end()
                                 ? newly_pinned.at(range.handle) : old->second;
        gmmu->Map(range.address, pinned + range.source_offset, range.size,
                  static_cast<Tegra::PTEKind>(kinds[i].backend), false);
    }
    // Replacement is now committed. Drop old pins only after their GMMU
    // entries have been replaced; one pin serves all ranges using a handle.
    native_sparse_mappings = std::move(updated);
    native_sparse_pins.merge(newly_pinned);
    for (auto it = native_sparse_pins.begin(); it != native_sparse_pins.end();) {
        if (retained.contains(it->first)) { ++it; continue; }
        nvmap.UnpinHandle(it->first);
        it = native_sparse_pins.erase(it);
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::ReleaseNativeSparse4K(u64 address, u64 size) {
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!size || ((address | size) & (page - 1)) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    const auto owned = native_sparse_regions.find(address);
    const auto allocation = allocation_map.find(address);
    if (!vm.initialised || !vm.small_page_allocator ||
        owned == native_sparse_regions.end() || owned->second != size ||
        allocation == allocation_map.end() || !allocation->second.sparse ||
        allocation->second.big_pages || allocation->second.page_size != page ||
        allocation->second.size != size || !allocation->second.mappings.empty())
        return NvResult::BadParameter;
    // Refuse disposal of a VA range overlaid by a legacy or direct map.
    auto mapping = mapping_map.lower_bound(address);
    if ((mapping != mapping_map.end() && mapping->first < address + size) ||
        (mapping != mapping_map.begin() &&
         std::prev(mapping)->first + std::prev(mapping)->second.size > address))
        return NvResult::BadParameter;
    // Purge all physical mappings covered by this reservation; preserve
    // handles still referenced by other independent native sparse regions.
    auto updated = native_sparse_mappings;
    updated.Replace(NativeSparseRange{address, size, 0, 0});
    const auto retained = updated.ReferencedHandles();
    gmmu->Unmap(address, size);
    native_sparse_mappings = std::move(updated);
    for (auto it = native_sparse_pins.begin(); it != native_sparse_pins.end();) {
        if (retained.contains(it->first)) { ++it; continue; }
        nvmap.UnpinHandle(it->first);
        it = native_sparse_pins.erase(it);
    }
    vm.small_page_allocator->Free(static_cast<u32>(address >> VM::PAGE_SIZE_BITS),
                                  static_cast<u32>(size >> VM::PAGE_SIZE_BITS));
    allocation_map.erase(allocation);
    native_sparse_regions.erase(owned);
    return NvResult::Success;
}

NvResult nvhost_as_gpu::UnmapNative4K(u64 address, u64 size) {
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!size || (address | size) & (page - 1) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    const auto mapping = mapping_map.find(address);
    const auto reservation = allocation_map.find(address);
    if (!vm.initialised || mapping == mapping_map.end() ||
        reservation == allocation_map.end() || mapping->second.size != size ||
        reservation->second.size != size || reservation->second.page_size != page ||
        reservation->second.mappings.size() != 1 ||
        reservation->second.mappings[0] != address || reservation->second.sparse)
        return NvResult::BadParameter;
    if (!FreeMappingLocked(address)) return NvResult::BadValue;
    vm.small_page_allocator->Free(static_cast<u32>(address >> VM::PAGE_SIZE_BITS),
                                  static_cast<u32>(size >> VM::PAGE_SIZE_BITS));
    allocation_map.erase(reservation);
    map_buffer_offsets.erase(static_cast<s64>(address));
    return NvResult::Success;
}

NvResult nvhost_as_gpu::ModifyNativeKind(u64 address, u64 size, u8 kind) {
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!size || (address | size) & (page - 1)) return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!vm.initialised) return NvResult::InvalidState;
    auto it = mapping_map.upper_bound(address);
    if (it == mapping_map.begin()) return NvResult::BadParameter;
    --it;
    const auto& mapping = it->second;
    if (mapping.big_page || address < it->first ||
        address - it->first >= mapping.size ||
        size > mapping.size - (address - it->first))
        return NvResult::BadParameter;
    const u64 delta = address - it->first;
    gmmu->Map(address, mapping.ptr + delta, size, static_cast<Tegra::PTEKind>(kind), false);
    return NvResult::Success;
}

NvResult nvhost_as_gpu::AllocAsEx(IoctlAllocAsEx& params) {
    LOG_DEBUG(Service_NVDRV, "called, big_page_size={:#x}", params.big_page_size);

    std::scoped_lock lock(mutex);

    if (vm.initialised) {
        ASSERT_MSG(false, "Cannot initialise an address space twice!");
        return NvResult::InvalidState;
    }

    if (params.big_page_size) {
        if (!std::has_single_bit(params.big_page_size)) {
            LOG_ERROR(Service_NVDRV, "Non power-of-2 big page size: {:#x}!", params.big_page_size);
            return NvResult::BadValue;
        }

        if ((params.big_page_size & VM::SUPPORTED_BIG_PAGE_SIZES) == 0) {
            LOG_ERROR(Service_NVDRV, "Unsupported big page size: {:#x}!", params.big_page_size);
            return NvResult::BadValue;
        }

        vm.big_page_size = params.big_page_size;
        vm.big_page_size_bits = static_cast<u32>(std::countr_zero(params.big_page_size));

        vm.va_range_start = params.big_page_size << VM::VA_START_SHIFT;
    }

    // If this is unspecified then default values should be used
    if (params.va_range_start) {
        vm.va_range_start = params.va_range_start;
        vm.va_range_split = params.va_range_split;
        vm.va_range_end = params.va_range_end;
    }

    const u64 max_big_page_bits = Common::Log2Ceil(vm.va_range_end);

    const auto start_pages{static_cast<u32>(vm.va_range_start >> VM::PAGE_SIZE_BITS)};
    const auto end_pages{static_cast<u32>(vm.va_range_split >> VM::PAGE_SIZE_BITS)};
    vm.small_page_allocator.emplace(start_pages, end_pages);

    const auto start_big_pages{static_cast<u32>(vm.va_range_split >> vm.big_page_size_bits)};
    const auto end_big_pages{
        static_cast<u32>((vm.va_range_end - vm.va_range_split) >> vm.big_page_size_bits)};
    vm.big_page_allocator.emplace(start_big_pages, end_big_pages);

    gmmu = std::make_unique<Tegra::MemoryManager>(system, max_big_page_bits, vm.va_range_split, vm.big_page_size_bits, VM::PAGE_SIZE_BITS);
    system.GPU().InitAddressSpace(*gmmu);
    vm.initialised = true;

    return NvResult::Success;
}

NvResult nvhost_as_gpu::AllocateSpace(IoctlAllocSpace& params) {
    LOG_DEBUG(Service_NVDRV, "called, pages={:X}, page_size={:X}, flags={:X}", params.pages,
              params.page_size, params.flags);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    if (params.page_size != VM::YUZU_PAGESIZE && params.page_size != vm.big_page_size) {
        return NvResult::BadValue;
    }

    if (params.page_size != vm.big_page_size &&
        ((params.flags & MappingFlags::Sparse) != MappingFlags::None)) {
        UNIMPLEMENTED_MSG("Sparse small pages are not implemented!");
        return NvResult::NotImplemented;
    }

    const u32 page_size_bits{params.page_size == VM::YUZU_PAGESIZE ? VM::PAGE_SIZE_BITS
                                                                   : vm.big_page_size_bits};

    auto& allocator{params.page_size == VM::YUZU_PAGESIZE ? *vm.small_page_allocator
                                                          : *vm.big_page_allocator};

    if ((params.flags & MappingFlags::Fixed) != MappingFlags::None) {
        allocator.AllocateFixed(static_cast<u32>(params.offset >> page_size_bits), params.pages);
    } else {
        params.offset = static_cast<u64>(allocator.Allocate(params.pages)) << page_size_bits;
        if (!params.offset) {
            ASSERT_MSG(false, "Failed to allocate free space in the GPU AS!");
            return NvResult::InsufficientMemory;
        }
    }

    u64 size{static_cast<u64>(params.pages) * params.page_size};

    if ((params.flags & MappingFlags::Sparse) != MappingFlags::None) {
        gmmu->MapSparse(params.offset, size);
    }

    allocation_map[params.offset] = {
        .mappings{},
        .size = size,
        .page_size = params.page_size,
        .sparse = (params.flags & MappingFlags::Sparse) != MappingFlags::None,
        .big_pages = params.page_size != VM::YUZU_PAGESIZE,
    };

    return NvResult::Success;
}

bool nvhost_as_gpu::FreeMappingLocked(u64 offset) noexcept {
    if (auto const it = mapping_map.find(offset); it != mapping_map.end()) {
        auto const mapping = it->second;
        if (!mapping.fixed) {
            auto& allocator{mapping.big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
            u32 page_size_bits{mapping.big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};
            u32 page_size{mapping.big_page ? vm.big_page_size : VM::YUZU_PAGESIZE};
            u64 aligned_size{Common::AlignUp(mapping.size, page_size)};
            allocator.Free(u32(mapping.offset >> page_size_bits), u32(aligned_size >> page_size_bits));
        }
        nvmap.UnpinHandle(mapping.handle);
        // Sparse mappings shouldn't be fully unmapped, just returned to their sparse state
        // Only FreeSpace can unmap them fully
        if (mapping.sparse_alloc) {
            gmmu->MapSparse(offset, mapping.size, mapping.big_page);
        } else {
            gmmu->Unmap(offset, mapping.size);
        }
        mapping_map.erase(offset);
        return true;
    }
    return false;
}

NvResult nvhost_as_gpu::FreeSpace(IoctlFreeSpace& params) {
    LOG_DEBUG(Service_NVDRV, "called, offset={:X}, pages={:X}, page_size={:X}", params.offset, params.pages, params.page_size);
    std::scoped_lock lock(mutex);
    if (!vm.initialised) {
        return NvResult::BadValue;
    }
    if (native_sparse_regions.contains(params.offset))
        return NvResult::BadParameter;
    if (auto const it = allocation_map.find(params.offset); it != allocation_map.end()) {
        auto const allocation = it->second;
        if (allocation.page_size != params.page_size || allocation.size != (u64(params.pages) * params.page_size))
            return NvResult::BadValue;

        for (const auto mapping_offset : allocation.mappings)
            if (!FreeMappingLocked(mapping_offset))
                return NvResult::BadValue;

        // Unset sparse flag if required
        if (allocation.sparse)
            gmmu->Unmap(params.offset, allocation.size);

        auto& allocator{params.page_size == VM::YUZU_PAGESIZE ? *vm.small_page_allocator : *vm.big_page_allocator};
        u32 page_size_bits{params.page_size == VM::YUZU_PAGESIZE ? VM::PAGE_SIZE_BITS : vm.big_page_size_bits};

        allocator.Free(u32(params.offset >> page_size_bits), u32(allocation.size >> page_size_bits));
        allocation_map.erase(params.offset);
        return NvResult::Success;
    }
    return NvResult::BadValue;
}

NvResult nvhost_as_gpu::Remap(std::span<IoctlRemapEntry> entries) {
    LOG_DEBUG(Service_NVDRV, "called, num_entries={:#x}", entries.size());

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    for (const auto& entry : entries) {
        GPUVAddr virtual_address{static_cast<u64>(entry.as_offset_big_pages)
                                 << vm.big_page_size_bits};
        u64 size{static_cast<u64>(entry.big_pages) << vm.big_page_size_bits};

        auto alloc{allocation_map.upper_bound(virtual_address)};

        if (alloc-- == allocation_map.begin() ||
            (virtual_address - alloc->first) + size > alloc->second.size) {
            LOG_WARNING(Service_NVDRV, "Cannot remap into an unallocated region!");
            return NvResult::BadValue;
        }

        if (!alloc->second.sparse) {
            LOG_WARNING(Service_NVDRV, "Cannot remap a non-sparse mapping!");
            return NvResult::BadValue;
        }

        const bool use_big_pages = alloc->second.big_pages;
        if (!entry.handle) {
            gmmu->MapSparse(virtual_address, size, use_big_pages);
        } else {
            auto handle{nvmap.GetHandle(entry.handle)};
            if (!handle) {
                return NvResult::BadValue;
            }

            DAddr base = nvmap.PinHandle(entry.handle, false);
            DAddr device_address{static_cast<DAddr>(
                base + (static_cast<u64>(entry.handle_offset_big_pages) << vm.big_page_size_bits))};

            gmmu->Map(virtual_address, device_address, size,
                      static_cast<Tegra::PTEKind>(entry.kind), use_big_pages);
        }
    }

    return NvResult::Success;
}

NvResult nvhost_as_gpu::MapBufferEx(IoctlMapBufferEx& params) {
    LOG_DEBUG(Service_NVDRV,
              "called, flags={:X}, nvmap_handle={:X}, buffer_offset={}, mapping_size={}"
              ", offset={:#x}",
              params.flags, params.handle, params.buffer_offset, params.mapping_size,
              params.offset);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    // Remaps a subregion of an existing mapping to a different PA
    if ((params.flags & MappingFlags::Remap) != MappingFlags::None) {
        if (auto const it = mapping_map.find(params.offset); it != mapping_map.end()) {
            auto const mapping = it->second;
            if (mapping.size < params.mapping_size) {
                LOG_WARNING(Service_NVDRV, "Cannot remap a partially mapped GPU address space region: {:#x}", params.offset);
                return NvResult::BadValue;
            }
            u64 gpu_address = u64(params.offset + params.buffer_offset);
            VAddr device_address{mapping.ptr + params.buffer_offset};
            gmmu->Map(gpu_address, device_address, params.mapping_size, Tegra::PTEKind(params.kind), mapping.big_page);
            return NvResult::Success;
        } else {
            LOG_WARNING(Service_NVDRV, "Cannot remap an unmapped GPU address space region: {:#x}", params.offset);
            return NvResult::BadValue;
        }
    }

    auto handle{nvmap.GetHandle(params.handle)};
    if (!handle) {
        return NvResult::BadValue;
    }

    DAddr device_address = DAddr(nvmap.PinHandle(params.handle, false) + params.buffer_offset);
    u64 size{params.mapping_size ? params.mapping_size : handle->orig_size};

    bool big_page{[&]() {
        if (Common::IsAligned(handle->align, vm.big_page_size)) {
            return true;
        } else if (Common::IsAligned(handle->align, VM::YUZU_PAGESIZE)) {
            return false;
        } else {
            ASSERT(false);
            return false;
        }
    }()};

    if ((params.flags & MappingFlags::Fixed) != MappingFlags::None) {
        auto alloc{allocation_map.upper_bound(params.offset)};

        if (alloc-- == allocation_map.begin() ||
            (params.offset - alloc->first) + size > alloc->second.size) {
            ASSERT_MSG(false, "Cannot perform a fixed mapping into an unallocated region!");
            return NvResult::BadValue;
        }

        const bool use_big_pages = alloc->second.big_pages && big_page;
        gmmu->Map(params.offset, device_address, size, static_cast<Tegra::PTEKind>(params.kind), use_big_pages);

        alloc->second.mappings.push_back(params.offset);
        mapping_map.insert_or_assign(params.offset, Mapping(params.handle, device_address, params.offset, size, true, use_big_pages, alloc->second.sparse));
    } else {
        auto& allocator{big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
        u32 page_size{big_page ? vm.big_page_size : VM::YUZU_PAGESIZE};
        u32 page_size_bits{big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};

        params.offset = u64(allocator.Allocate(u32(Common::AlignUp(size, page_size) >> page_size_bits))) << page_size_bits;
        if (!params.offset) {
            ASSERT_MSG(false, "Failed to allocate free space in the GPU AS!");
            return NvResult::InsufficientMemory;
        }
        gmmu->Map(params.offset, device_address, Common::AlignUp(size, page_size), Tegra::PTEKind(params.kind), big_page);
        mapping_map.insert_or_assign(params.offset, Mapping(params.handle, device_address, params.offset, size, false, big_page, false));
    }

    map_buffer_offsets.insert(params.offset);

    return NvResult::Success;
}

NvResult nvhost_as_gpu::UnmapBuffer(IoctlUnmapBuffer& params) {
    std::scoped_lock lock(mutex);
    if (auto const offset_it = map_buffer_offsets.find(params.offset); offset_it != map_buffer_offsets.end()) {
        LOG_DEBUG(Service_NVDRV, "called, offset={:#x}", params.offset);
        if (!vm.initialised) {
            return NvResult::BadValue;
        }

        auto const it = mapping_map.find(params.offset);
        auto const mapping = it->second;
        if (!mapping.fixed) {
            auto& allocator{mapping.big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
            u32 page_size_bits{mapping.big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};
            allocator.Free(u32(mapping.offset >> page_size_bits), u32(mapping.size >> page_size_bits));
        }

        // Sparse mappings shouldn't be fully unmapped, just returned to their sparse state
        // Only FreeSpace can unmap them fully
        if (mapping.sparse_alloc) {
            gmmu->MapSparse(params.offset, mapping.size, mapping.big_page);
        } else {
            gmmu->Unmap(params.offset, mapping.size);
        }

        nvmap.UnpinHandle(mapping.handle);
        mapping_map.erase(params.offset);
        map_buffer_offsets.erase(params.offset);
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::BindChannel(IoctlBindChannel& params) {
    LOG_DEBUG(Service_NVDRV, "called, fd={:X}", params.fd);

    auto gpu_channel_device = module.GetDevice<nvhost_gpu>(params.fd);
    gpu_channel_device->channel_state->memory_manager = gmmu.get();
    return NvResult::Success;
}

void nvhost_as_gpu::GetVARegionsImpl(IoctlGetVaRegions& params) {
    params.buf_size = 2 * sizeof(VaRegion);

    params.regions = std::array<VaRegion, 2>{
        VaRegion{
            .offset = vm.small_page_allocator->GetVAStart() << VM::PAGE_SIZE_BITS,
            .page_size = VM::YUZU_PAGESIZE,
            ._pad0_{},
            .pages = vm.small_page_allocator->GetVALimit() - vm.small_page_allocator->GetVAStart(),
        },
        VaRegion{
            .offset = vm.big_page_allocator->GetVAStart() << vm.big_page_size_bits,
            .page_size = vm.big_page_size,
            ._pad0_{},
            .pages = vm.big_page_allocator->GetVALimit() - vm.big_page_allocator->GetVAStart(),
        },
    };
}

NvResult nvhost_as_gpu::GetVARegions1(IoctlGetVaRegions& params) {
    LOG_DEBUG(Service_NVDRV, "called, buf_addr={:X}, buf_size={:X}", params.buf_addr, params.buf_size);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    GetVARegionsImpl(params);

    return NvResult::Success;
}

NvResult nvhost_as_gpu::GetVARegions3(IoctlGetVaRegions& params, std::span<VaRegion> regions) {
    LOG_DEBUG(Service_NVDRV, "called, buf_addr={:X}, buf_size={:X}", params.buf_addr,
              params.buf_size);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    GetVARegionsImpl(params);

    const size_t num_regions = (std::min)(params.regions.size(), regions.size());
    for (size_t i = 0; i < num_regions; i++) {
        regions[i] = params.regions[i];
    }

    return NvResult::Success;
}

Kernel::KEvent* nvhost_as_gpu::QueryEvent(u32 event_id) {
    LOG_CRITICAL(Service_NVDRV, "Unknown AS GPU Event {}", event_id);
    return nullptr;
}

} // namespace Service::Nvidia::Devices
