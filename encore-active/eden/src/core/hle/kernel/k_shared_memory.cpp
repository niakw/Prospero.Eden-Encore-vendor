// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/k_system_resource.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/svc_results.h"

#if defined(PS5_NATIVE) && defined(EDEN_NS2_SPARSE_BACKING)
#include <cstdio>
#include <new>
// Kernel shared memory bypasses process PTE hooks. The maintained
// HostMemory::ClearBackingRegion owns both direct-memory admission and
// zeroing: never precommit the same range a second time here.
#endif

namespace Kernel {

KSharedMemory::KSharedMemory(KernelCore& kernel) : KAutoObjectWithSlabHeapAndContainer{kernel} {}
KSharedMemory::~KSharedMemory() = default;

Result KSharedMemory::Initialize(KernelCore& kernel, Core::DeviceMemory& device_memory, KProcess* owner_process,
                                 Svc::MemoryPermission owner_permission,
                                 Svc::MemoryPermission user_permission, std::size_t size) {
    // Set members.
    m_owner_process = owner_process;
    m_device_memory = std::addressof(device_memory);
    m_owner_permission = owner_permission;
    m_user_permission = user_permission;
    m_size = Common::AlignUp(size, PageSize);

    const size_t num_pages = Common::DivideUp(size, PageSize);

    // Get the resource limit.
    KResourceLimit* reslimit = kernel.GetSystemResourceLimit();

    // Reserve memory for ourselves.
    KScopedResourceReservation memory_reservation(kernel, reslimit, LimitableResource::PhysicalMemoryMax, size);
    R_UNLESS(memory_reservation.Succeeded(), ResultLimitReached);

    // Allocate the memory.

    //! HACK: Open continuous mapping from sysmodule pool.
    auto option = KMemoryManager::EncodeOption(KMemoryManager::Pool::Secure,
                                               KMemoryManager::Direction::FromBack);
    m_physical_address = kernel.MemoryManager().AllocateAndOpenContinuous(num_pages, 1, option);
    R_UNLESS(m_physical_address != 0, ResultOutOfMemory);

    //! Insert the result into our page group.
    m_page_group.emplace(kernel, std::addressof(kernel.GetSystemSystemResource().GetBlockInfoManager()));
    const Result add_result = m_page_group->AddBlock(m_physical_address, num_pages);
    if (R_FAILED(add_result)) {
        // AllocateAndOpenContinuous already took guest page references.
        // AddBlock may fail to allocate its descriptor: return those pages
        // explicitly before the uncommitted resource reservation unwinds.
        kernel.MemoryManager().Close(m_physical_address, num_pages);
        m_page_group.reset();
        m_physical_address = 0;
        R_RETURN(add_result);
    }

    // Clear all pages in the memory. In the PS5 sparse NS2 layout the
    // physical address exists in a reserved VA range but may have NO backing:
    // writing here without committing it crashes in InitializeHackSharedMemory
    // before a game reaches its first GPU command.
#if defined(PS5_NATIVE) && defined(EDEN_NS2_SPARSE_BACKING)
    try {
#endif
        for (const auto& block : *m_page_group) {
#if defined(PS5_NATIVE) && defined(EDEN_NS2_SPARSE_BACKING)
            const auto physical = GetInteger(block.GetAddress());
            if (physical < Core::DramMemoryMap::Base)
                throw std::bad_alloc{}; // invalid sparse DRAM physical address
            const auto offset = physical - Core::DramMemoryMap::Base;
            if (block.GetSize() >= (1u << 20))
                std::fprintf(stderr, "EDEN_NS2_SHARED_CLEAR begin offset=%zu bytes=%zu\n",
                             static_cast<std::size_t>(offset), block.GetSize());
            // Precommit and zero exactly once through the native owner.
            // A failed admission must be caught before this shared memory
            // becomes initialized, mapped, or charged as a live resource.
            m_device_memory->buffer.ClearBackingRegion(offset, block.GetSize(), 0);
            if (block.GetSize() >= (1u << 20))
                std::fprintf(stderr, "EDEN_NS2_SHARED_CLEAR done offset=%zu bytes=%zu\n",
                             static_cast<std::size_t>(offset), block.GetSize());
#else
            std::memset(m_device_memory->GetPointer<void>(block.GetAddress()), 0, block.GetSize());
#endif
        }
#if defined(PS5_NATIVE) && defined(EDEN_NS2_SPARSE_BACKING)
    } catch (const std::bad_alloc&) {
        // KPageGroup owns the references opened by AllocateAndOpenContinuous.
        // No guest PTE/IPC handle has been published, so close them and let
        // the still-uncommitted KScopedResourceReservation refund its limit.
        m_page_group->CloseAndReset(kernel);
        m_page_group.reset();
        m_physical_address = 0;
        R_THROW(ResultOutOfMemory);
    }
#endif

    // Publish only AFTER the entire sparse memory was admitted and cleared.
    memory_reservation.Commit();
    m_resource_limit = reslimit;
    m_resource_limit->Open(kernel);
    m_is_initialized = true;
    R_SUCCEED();
}

void KSharedMemory::Finalize(KernelCore& kernel) {
    // Close and finalize the page group.
    m_page_group->Close(kernel);
    m_page_group->Finalize();

    // Release the memory reservation.
    m_resource_limit->Release(kernel, LimitableResource::PhysicalMemoryMax, m_size);
    m_resource_limit->Close(kernel);
}

Result KSharedMemory::Map(KProcess& target_process, KProcessAddress address, std::size_t map_size,
                          Svc::MemoryPermission map_perm) {
    // Validate the size.
    R_UNLESS(m_size == map_size, ResultInvalidSize);

    // Validate the permission.
    const Svc::MemoryPermission test_perm =
        std::addressof(target_process) == m_owner_process ? m_owner_permission : m_user_permission;
    if (test_perm == Svc::MemoryPermission::DontCare) {
        ASSERT(map_perm == Svc::MemoryPermission::Read || map_perm == Svc::MemoryPermission::Write);
    } else {
        R_UNLESS(map_perm == test_perm, ResultInvalidNewMemoryPermission);
    }

    R_RETURN(target_process.GetPageTable().MapPageGroup(
        address, *m_page_group, KMemoryState::Shared, ConvertToKMemoryPermission(map_perm)));
}

Result KSharedMemory::Unmap(KProcess& target_process, KProcessAddress address,
                            std::size_t unmap_size) {
    // Validate the size.
    R_UNLESS(m_size == unmap_size, ResultInvalidSize);

    R_RETURN(
        target_process.GetPageTable().UnmapPageGroup(address, *m_page_group, KMemoryState::Shared));
}

} // namespace Kernel
