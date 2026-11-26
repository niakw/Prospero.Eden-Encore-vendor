// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Native GPU registration and selected dense 214 backing own real guest
// page-table / NVMap resources. Ampere command submission is separate.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <utility>

#include "common/alignment.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_page_group.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_system_resource.h"
#include "video_core/gpu.h"
#ifdef PS5_NATIVE
#include "gpu_failure.h"
#endif
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/gpu_memory_registration.h"
#include "core/hle/service/am/service/gpu_worker.h"
#include "core/hle/service/am/service/gpu_worker_shared.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"

namespace Service::AM {
namespace {
constexpr std::size_t MaxOwnedGpuMemoryHandles = 4096;

struct BlockingBoundaryTrace {
    BlockingBoundaryTrace(const char* operation_, u32 address_space_, u64 address_, u64 size_)
        : operation{operation_}, address_space{address_space_}, address{address_}, size{size_} {
#ifdef PS5_NATIVE
        static std::atomic<unsigned> sequence{0};
        ticket = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        active = ticket <= 32 || (ticket & (ticket - 1)) == 0;
        if (active) {
            start = std::chrono::steady_clock::now();
            std::fprintf(stderr,
                         "EDEN_NS2_BLOCKING_BOUNDARY op=%s phase=begin ticket=%u as=%u address=%llx size=%llx\n",
                         operation, ticket, address_space,
                         static_cast<unsigned long long>(address),
                         static_cast<unsigned long long>(size));
        }
#endif
    }
    ~BlockingBoundaryTrace() {
#ifdef PS5_NATIVE
        if (active) {
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
            std::fprintf(stderr,
                         "EDEN_NS2_BLOCKING_BOUNDARY op=%s phase=end ticket=%u elapsed_us=%lld as=%u address=%llx size=%llx\n",
                         operation, ticket, static_cast<long long>(us), address_space,
                         static_cast<unsigned long long>(address),
                         static_cast<unsigned long long>(size));
        }
#endif
    }
    const char* operation;
    u32 address_space;
    u64 address;
    u64 size;
    unsigned ticket{};
    bool active{};
    std::chrono::steady_clock::time_point start{};
};
static void TraceNs2WorkerResult(unsigned command, u32 result) {
#ifdef PS5_NATIVE
    // Failures are actionable only with their actual guest-visible result.
    // Bound the trace even if the guest retries an unsupported operation.
    static std::atomic<unsigned> responses{0};
    const auto n = responses.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 64 || (n & (n - 1)) == 0)
        std::fprintf(stderr,
                     "EDEN_NS2_GPU_WORKER_RESULT cmd=%u result=%08x count=%u\n",
                     command, static_cast<unsigned>(result), n);
#else
    (void)command;
    (void)result;
#endif
}

// 214 may be a sparse reservation without any physical pages. Expose the
// precise request shape once per bounded sample; a successful reservation
// MUST NOT be mistaken for resident render-target memory or a guest frame.
static void TraceNs2GpuRegion214(const u32* packet,
                                  const std::optional<GpuSparseRegionRequest>& request,
                                  u32 result, bool has_physical_backing) {
#ifdef PS5_NATIVE
    static std::atomic<unsigned> seen{0};
    const auto n = seen.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 24 || (n & (n - 1)) == 0) {
        std::fprintf(stderr,
                     "EDEN_NS2_GPU_214 decoded=%u hdr=%08x/%08x/%08x "
                     "field7=%08x field8=%08x field9=%08x field10=%08x "
                     "as=%u device=%u mode=%u page_shift=%u "
                     "begin=%llx end=%llx result=%08x backing=%s count=%u\n",
                     request ? 1U : 0U,
                     packet[0], packet[1], packet[2], packet[7], packet[8],
                     packet[9], packet[10],
                     request ? request->address_space : 0U,
                     request ? unsigned(request->device) : 0U,
                     request ? unsigned(request->allocation_mode) : 0U,
                     request ? unsigned(request->page_shift) : 0U,
                     static_cast<unsigned long long>(request ? request->begin : 0),
                     static_cast<unsigned long long>(request ? request->end : 0),
                     result, has_physical_backing ? "dense_nvmap" : "unbound", n);
    }
#else
    (void)packet;
    (void)request;
    (void)result;
    (void)has_physical_backing;
#endif
}
static void TraceNs2GpuSparse216(u64 requested, u64 processed, u64 physical_entries, u32 result) {
#ifdef PS5_NATIVE
    static std::atomic<unsigned> seen{0};
    const auto n = seen.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 24 || (n & (n - 1)) == 0) {
        std::fprintf(stderr,
                     "EDEN_NS2_GPU_216 requested=%llu processed=%llu physical_entries=%llu result=%08x count=%u\n",
                     static_cast<unsigned long long>(requested),
                     static_cast<unsigned long long>(processed),
                     static_cast<unsigned long long>(physical_entries), result, n);
    }
#else
    (void)requested;
    (void)processed;
    (void)physical_entries;
    (void)result;
#endif
}
}

IGpuWorker::IGpuWorker(Core::System& system_, std::shared_ptr<Nvidia::Module> module,
                       Kernel::KProcess* process,
                       std::shared_ptr<GpuWorkerSharedState> shared)
    : ServiceFramework{system_, "IGpuWorker"}, m_shared{std::move(shared)},
      m_module{std::move(module)}, m_process{process} {
    m_process->Open(kernel);
    static const FunctionInfo functions[] = {
        {0, &IGpuWorker::Handshake, "Handshake"},
        {101, &IGpuWorker::CreateGpuAccessibleMemory, "CreateGpuAccessibleMemory"},
        {102, &IGpuWorker::DestroyGpuAccessibleMemory, "DestroyGpuAccessibleMemory"},
        {171, &IGpuWorker::EnsureGpuMemoryComptags, "EnsureGpuMemoryComptags"},
        {172, &IGpuWorker::SetGpuMemoryUserDebugLabel, "SetGpuMemoryUserDebugLabel"},
        {173, &IGpuWorker::SetGpuMemorySdkDebugLabel, "SetGpuMemorySdkDebugLabel"},
        {209, &IGpuWorker::SetGpuSegmentUserDebugLabel, "SetGpuSegmentUserDebugLabel"},
        {210, &IGpuWorker::SetGpuSegmentSdkDebugLabel, "SetGpuSegmentSdkDebugLabel"},
        {211, &IGpuWorker::NormalMapGpuMemory4K, "NormalMapGpuMemory4K"},
        {212, &IGpuWorker::NormalModifyGpuMemory4K, "NormalModifyGpuMemory4K"},
        {213, &IGpuWorker::NormalUnmapGpuMemory4K, "NormalUnmapGpuMemory4K"},
        {214, &IGpuWorker::CreateSparseRegion, "CreateSparseRegion"},
        {215, &IGpuWorker::DestroySparseRegion, "DestroySparseRegion"},
        {216, &IGpuWorker::ExecuteSparseMapRequests, "ExecuteSparseMapRequests"},
    };
    RegisterHandlersTipc(functions);
}

IGpuWorker::~IGpuWorker() {
    // Drain guest GPU work before removing any memory that can be consumed
    // by the native engine; do not release backing while it is still mapped.
    // Ordinary 211 GMMU mappings and 111 registered guest memory can also
    // be referenced by already submitted compute/graphics work. The old
    // drain only handled dense 214 backing; a worker without dense regions
    // could retire a live command buffer or GPU-written SSBO prematurely.
    // On ordinary worker/FD close the GPU is still running: retire every
    // previously submitted buffer before unmapping its GMMU/NvMap backing.
    // At full emulator shutdown Core::ShutdownMainProcess stops and joins
    // the GPU worker BEFORE CloseServices destroys this TIPC session. Never
    // dereference System::GPU() then: its optional was already reset.
    if (system.IsPoweredOn() && !system.IsShuttingDown() &&
        (!m_dense_regions.empty() || !m_mappings.Mappings().empty() ||
         !m_sparse_overlays.Mappings().empty() || !m_memory.empty())) {
        try {
            system.GPU().WaitForIdle();
        } catch (...) {
            // An unsuccessful drain does not prove the GPU stopped using
            // backing. The destructor must not terminate the host or free it.
            m_quarantined_nv_session = true;
#ifdef PS5_NATIVE
            ::Eden::RecordGpuFailure(std::current_exception());
#endif
        }
    }
    // A failed native 215/216 can poison a GMMU AFTER the original dense
    // 214 backing was published. The worker must detect this on teardown:
    // FreeHandle / CloseSession would otherwise unpin physical pages while
    // some partial guest PTEs still point at them.
    if (!m_quarantined_nv_session) {
        for (const auto id : m_shared->address_spaces.OwnedIds()) {
            if (!id) continue;
            std::shared_ptr<Nvidia::Devices::nvhost_as_gpu> as;
            {
                const std::scoped_lock lock{m_shared->mutex};
                as = m_shared->FindOwnedAddressSpace(*id);
            }
            if (as && as->IsNativeSparsePoisoned()) {
                m_quarantined_nv_session = true;
                break;
            }
        }
    }
    if (m_quarantined_nv_session)
        LOG_ERROR(Service_AM, "NS2 poisoned GPU AS at worker teardown: quarantine all owned NVMap backing");

    // 211 overlays must go first: their 213 equivalent restores sparse PTEs,
    // after which the owning 214 reservation can be released safely.
    if (!m_quarantined_nv_session)
    for (const auto& [key, mapping] : m_sparse_overlays.Mappings()) {
        (void)key;
        std::shared_ptr<Nvidia::Devices::nvhost_as_gpu> as;
        {
            const std::scoped_lock lock{m_shared->mutex};
            as = m_shared->FindOwnedAddressSpace(mapping.address_space);
        }
        if (!as || as->UnmapNative4K(mapping.address, mapping.size) !=
                       Nvidia::NvResult::Success) {
            LOG_ERROR(Service_AM, "NS2 sparse-overlay unmap failed as={} VA={:#x}",
                      mapping.address_space, mapping.address);
            m_quarantined_nv_session = true;
            break;
        }
        if (as->IsNativeSparsePoisoned()) {
            m_quarantined_nv_session = true;
            break;
        }
    }

    if (!m_quarantined_nv_session)
    for (const auto& [key, mapping] : m_mappings.Mappings()) {
        const auto dense = m_dense_regions.find({mapping.address_space, mapping.address});
        // The VA can already be released after a failed physical retirement.
        // Do not release it twice during teardown.
        if (mapping.sparse && dense != m_dense_regions.end() &&
            dense->second.gmmu_released)
            continue;
        std::shared_ptr<Nvidia::Devices::nvhost_as_gpu> as;
        {
            const std::scoped_lock lock{m_shared->mutex};
            as = m_shared->FindOwnedAddressSpace(mapping.address_space);
        }
        if (!as && !mapping.sparse) {
            const std::scoped_lock lock{m_shared->mutex};
            if (m_shared->CancelBeforeImport(mapping.address_space, mapping.address, mapping.size))
                continue;
            // Import can publish after the preceding lookup. Resolve again
            // while holding the same lock as the pending ledger.
            as = m_shared->FindOwnedAddressSpace(mapping.address_space);
        }
        const auto retired = as
            ? (mapping.sparse
                   ? as->ReleaseNativeSparse4K(mapping.address, mapping.size)
                   : as->UnmapNative4K(mapping.address, mapping.size))
            : Nvidia::NvResult::InvalidState;
        if (retired != Nvidia::NvResult::Success) {
            LOG_ERROR(Service_AM, "NS2 native GPU VA unmap failed as={} VA={:#x}",
                      mapping.address_space, mapping.address);
            // No proof that the PTE/pin was retired: never continue into
            // FreeHandle/CloseSession with potentially live GPU backing.
            m_quarantined_nv_session = true;
            break;
        }
        if (as->IsNativeSparsePoisoned()) {
            m_quarantined_nv_session = true;
            break;
        }
    }
    // Identity maps are deliberately outside MappingLedger. Remove only those
    // owned by this worker before its NVMap handles are released.
    if (!m_quarantined_nv_session)
    for (const u32 memory_id : m_memory) {
        const auto identity = std::find_if(
            m_shared->identity_gpu_mappings.begin(), m_shared->identity_gpu_mappings.end(),
            [memory_id](const IdentityGpuMapping& mapping) {
                return mapping.handle == memory_id;
            });
        if (identity == m_shared->identity_gpu_mappings.end()) continue;
        while (identity->mapped_count) {
            const u32 as_id = identity->mapped_spaces[identity->mapped_count - 1];
            const auto as = m_shared->FindOwnedAddressSpace(as_id);
            if (!as || as->UnmapNative4K(identity->address, identity->size) !=
                           Nvidia::NvResult::Success || as->IsNativeSparsePoisoned()) {
                LOG_ERROR(Service_AM, "NS2 identity GPU VA unmap failed as={} VA={:#x}",
                          as_id, identity->address);
                m_quarantined_nv_session = true;
                break;
            }
            identity->RemoveSpace(as_id);
        }
        if (m_quarantined_nv_session) break;
        m_shared->identity_gpu_mappings.erase(identity);
    }

    // The sparse reservations are gone: it is now safe to drop the NVMap pins,
    // unmap CPU aliases and close the page-group physical allocations.
    if (!m_quarantined_nv_session)
    while (!m_dense_regions.empty()) {
        const auto key = m_dense_regions.begin()->first;
        if (!RetireGpuOnlyRegion(key.first, key.second)) {
            LOG_ERROR(Service_AM, "NS2 dense backing retire failed as={} VA={:#x}",
                      key.first, key.second);
            // CloseSession would force-free the remaining NVMap owners.
            // Preserve the whole session as well as its physical page groups.
            m_quarantined_nv_session = true;
            break;
        }
    }
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    if (!m_quarantined_nv_session)
    for (const u32 id : m_memory) {
        if (const auto pending = m_pending_device_unlocks.find(id);
            pending != m_pending_device_unlocks.end()) {
            // No second FreeHandle after a destructive release. A surviving
            // NVMap reference must not lose its still-locked guest backing.
            if (!pending->second.nvmap_lifetime.expired()) {
                LOG_ERROR(Service_AM, "NS2 GPU memory still externally owned at worker teardown: {}", id);
                m_quarantined_nv_session = true;
                break;
            }
            const auto result = m_process->GetPageTable().UnlockForDeviceAddressSpace(
                pending->second.address, pending->second.size);
            if (result.IsError())
                LOG_ERROR(Service_AM, "NS2 deferred GPU memory unlock failed: {:#x}", result.raw);
            continue;
        }
        std::optional<Nvidia::NvCore::NvMap::FreeInfo> info;
        try {
            info = nvmap.FreeHandleIfUnpinned(id);
        } catch (...) {
            m_quarantined_nv_session = true;
            break;
        }
        if (!info || !info->can_unlock) {
            // External pins/duplicates (or an in-flight shared owner) are
            // not proof of retirement. Never follow this with CloseSession.
            m_quarantined_nv_session = true;
            break;
        }
        const auto result = m_process->GetPageTable().UnlockForDeviceAddressSpace(
            info->address, info->size);
        if (result.IsError()) {
            LOG_ERROR(Service_AM, "NS2 GPU memory unlock failed: {:#x}", result.raw);
        }
    }
    if (m_quarantined_nv_session) {
        // The poisoned AS still owns GPU PTEs. Retain every page-group
        // backing and NVMap session until native AS/process teardown.
        // Only this fail-closed path skips otherwise mandatory cleanup.
        for (auto& [key, region] : m_dense_regions) {
            (void)key;
            (void)region.pages.release();
        }
        m_dense_regions.clear();
    }
    if (m_nv_session && !m_quarantined_nv_session)
        m_module->GetContainer().CloseSession(Nvidia::NvCore::SessionId{*m_nv_session});
    if (m_nv_session && m_quarantined_nv_session)
        LOG_ERROR(Service_AM,
                  "NS2 poisoned GPU AS: retain NVDRV session and physical backing until process teardown");
    // A quarantined or normally destroyed worker must never leave a
    // logical pre-import map pointing to backing it no longer owns.
    {
        const std::scoped_lock lock{m_shared->mutex};
        std::erase_if(m_shared->pending_211, [this](const auto& entry) {
            return std::find(m_memory.begin(), m_memory.end(), entry.second.memory) != m_memory.end();
        });
    }
    // GpuWorkerSharedState owns the opened GPU address-space FDs and its
    // final destructor unregisters this exact process epoch. Release our
    // reference NOW, while our KProcess Open is still held. Waiting for the
    // automatic shared_ptr member destructor (after this body) could use an
    // already-destroyed process when this is the last worker/session owner.
    m_shared.reset();
    m_process->Close(kernel);
}

Result IGpuWorker::BackGpuOnlyRegion(u32 address_space, u64 address, u64 size,
                                    u8 page_shift) {
    R_UNLESS(page_shift == 12 || page_shift == 16, Kernel::ResultInvalidCombination);
    // Called only after the exact native sparse VA has been reserved.
    // Keep a bounded physical budget and leave room for the guest/application.
    auto& memory = kernel.MemoryManager();
    using Pool = Kernel::KMemoryManager::Pool;
    using Direction = Kernel::KMemoryManager::Direction;
    if (m_dense_regions.contains({address_space, address}) ||
        !CanBackNativeDenseGpuOnly(0, size, m_dense_bytes,
                                    memory.GetFreeSize(Pool::SystemNonSecure)))
        return Kernel::ResultOutOfResource;

    // Reserve the ledger's std::map node BEFORE allocating kernel pages or
    // publishing any guest-visible GMMU remap. The old emplace() ran after
    // RemapNativeSparse succeeded: a host OOM there orphaned a live mapping,
    // NVMap handle and CPU alias without a retryable owner.
    // The caller holds m_mutex for this entire transaction. Transferring a
    // preallocated std::map node with merge() needs no further allocation.
    decltype(m_dense_regions) pending_region;
    try {
        pending_region.try_emplace(std::make_pair(address_space, address));
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }
    std::unique_ptr<Kernel::KPageGroup> pages;
    try {
        pages = std::make_unique<Kernel::KPageGroup>(
            kernel, std::addressof(kernel.GetSystemSystemResource().GetBlockInfoManager()));
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }
    const auto allocated = memory.AllocateAndOpen(
        pages.get(), size / Kernel::PageSize,
        Kernel::KMemoryManager::EncodeOption(Pool::SystemNonSecure, Direction::FromBack));
    if (allocated.IsError()) return allocated;

    auto& table = m_process->GetPageTable();
    Kernel::KProcessAddress alias{};
    // KProcessPageTable exposes only the fixed-address four-argument
    // MapPageGroup overload, unlike KPageTableBase. Search bounded 2 MiB
    // aligned slots instead of guessing an address or spinning indefinitely.
    constexpr u64 AliasAlignment = 2ULL << 20;
    constexpr u64 MaxAliasAttempts = 4096;
    const u64 start = GetInteger(table.GetAliasCodeRegionStart());
    const u64 region_size = table.GetAliasCodeRegionSize();
    const u64 slots = region_size / AliasAlignment;
    bool mapped = false;
    {
        BlockingBoundaryTrace trace{"214_map_page_group", address_space, address, size};
        if (slots && size <= region_size) {
            const u64 attempts = std::min(slots, MaxAliasAttempts);
            for (u64 step = 0; step < attempts; ++step) {
                const u64 slot = (m_alias_slot_hint + step) % slots;
                const u64 offset = slot * AliasAlignment;
                if (offset > region_size || size > region_size - offset) continue;
                const Kernel::KProcessAddress candidate{start + offset};
                if (table.MapPageGroup(candidate, *pages, Kernel::KMemoryState::IoMemory,
                                       Kernel::KMemoryPermission::UserReadWrite).IsSuccess()) {
                    alias = candidate;
                    m_alias_slot_hint = (slot + (size + AliasAlignment - 1) /
                                        AliasAlignment) % slots;
                    mapped = true;
                    break;
                }
            }
        }
    }
    if (!mapped) {
        pages->Close(kernel);
        return Kernel::ResultOutOfResource;
    }

    // Native sparse pages already have a CPU alias at this point. An OOM
    // opening the NVDRV session must unmap and close those pages before
    // returning, without exposing a partially backed guest GPU range.
    if (!m_nv_session) {
        try {
            m_nv_session = m_module->GetContainer().OpenSession(m_process).id;
        } catch (const std::bad_alloc&) {
            const auto undone = table.UnmapPageGroup(alias, *pages, Kernel::KMemoryState::IoMemory);
            if (undone.IsError()) {
                LOG_ERROR(Service_AM, "NS2 GPU-only session rollback CPU alias failed: {:#x}",
                          undone.raw);
                // Preserve physical backing when the guest alias could not be removed.
                (void)pages.release();
                return undone;
            }
            pages->Close(kernel);
            return Kernel::ResultOutOfResource;
        }
    }
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    std::shared_ptr<Nvidia::NvCore::NvMap::Handle> handle;
    Nvidia::NvResult status = Nvidia::NvResult::InvalidState;
    bool nvmap_oom = false;
    try {
        status = nvmap.CreateHandle(size, handle);
        if (status == Nvidia::NvResult::Success) {
            status = handle->Alloc({}, Kernel::PageSize, 0, GetInteger(alias),
                                   Nvidia::NvCore::SessionId{*m_nv_session});
        }
    } catch (const std::bad_alloc&) {
        // CreateHandle may already have returned Success before Alloc
        // throws. Invalidate that status, otherwise RemapNativeSparse can
        // install an NVMap handle whose physical backing never existed.
        status = Nvidia::NvResult::InvalidState;
        nvmap_oom = true;
    }
    bool committed = false;
    std::shared_ptr<Nvidia::Devices::nvhost_as_gpu> mapping_as;
    if (status == Nvidia::NvResult::Success) {
        Nvidia::Devices::NativeSparseRemapEntry entry{};
        entry.flags = page_shift == 16 ? 0x2000U : 0x1000U;
        entry.handle = handle->id;
        entry.destination_pages = address >> page_shift;
        entry.pages = size >> page_shift;
        mapping_as = m_shared->FindOwnedAddressSpace(address_space);
        committed = mapping_as &&
                    mapping_as->RemapNativeSparse(std::span{&entry, size_t{1}}) ==
                        Nvidia::NvResult::Success;
    }
    if (!committed) {
        // RemapNativeSparse may have mapped a PREFIX of guest GPU PTEs
        // before running out of page-table memory. It then quarantines all
        // backing pins and marks the AS poisoned. NvMap::FreeHandle(final
        // dupe) forcibly unmaps pins, and UnmapPageGroup/Close would free
        // physical pages still referenced by those partial GPU PTEs.
        // Only in this poisoned case keep the NVMap owner, CPU alias and
        // page-group backing until process / AS teardown. Never retry
        // mapping on a poisoned address space or pretend 214 succeeded.
        if (mapping_as && mapping_as->IsNativeSparsePoisoned()) {
            LOG_ERROR(Service_AM,
                      "NS2 dense sparse 214 remap poisoned GMMU: quarantine backing as={} VA={:#x} size={:#x}",
                      address_space, address, size);
            // NvCore::CloseSession performs UnmapAllHandles and clears all
            // NvMap pins on the final duplicate. Preserve the session lease
            // as well as the KPageGroup or the worker destructor would undo
            // this quarantine while poisoned GPU PTEs are still reachable.
            m_quarantined_nv_session = true;
            (void)pages.release();
            return Kernel::ResultOutOfResource;
        }
        if (handle) {
            const auto id = handle->id;
            handle.reset();
            nvmap.FreeHandle(id, false);
        }
        const auto result = table.UnmapPageGroup(alias, *pages, Kernel::KMemoryState::IoMemory);
        if (result.IsError()) {
            LOG_ERROR(Service_AM, "NS2 GPU-only rollback CPU alias failed: {:#x}", result.raw);
            // Never free pages still mapped in the process.
            (void)pages.release();
        } else {
            pages->Close(kernel);
        }
        return nvmap_oom ? Kernel::ResultOutOfResource : Kernel::ResultInvalidState;
    }
    // Do not retain the preflight shared_ptr: FreeHandle needs to be able
    // to release the NVMap object after GMMU unpins on 215.
    const u32 handle_id = handle->id;
    const std::weak_ptr<void> nvmap_lifetime{handle};
    handle.reset();
    pending_region.begin()->second =
        DenseRegion{address, size, GetInteger(alias), handle_id,
                    std::move(pages), nvmap_lifetime};
    // Transfer the already allocated node. No new allocation or failure
    // point remains after the GMMU mapping becomes guest-visible.
    m_dense_regions.merge(pending_region);
    m_dense_bytes += size;
    return ResultSuccess;
}

bool IGpuWorker::RetireGpuOnlyRegion(u32 address_space, u64 address) {
    const auto it = m_dense_regions.find({address_space, address});
    if (it == m_dense_regions.end()) return true;
    auto& region = it->second;
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    // FreeHandle decrements dupes even when can_unlock is false; never
    // call it twice for this owner. Wait for the original object's weak
    // lifetime to expire before tearing down the CPU-visible page alias.
    if (!region.nvmap_released) {
        std::optional<Nvidia::NvCore::NvMap::FreeInfo> info;
        try {
            info = nvmap.FreeHandleIfUnpinned(region.nvmap_handle);
        } catch (...) {
            m_quarantined_nv_session = true;
            return false;
        }
        if (!info) return false;
        region.nvmap_released = true;
    }
    if (!region.nvmap_lifetime.expired()) return false;
    const auto result = m_process->GetPageTable().UnmapPageGroup(
        Kernel::KProcessAddress{region.alias}, *region.pages, Kernel::KMemoryState::IoMemory);
    if (result.IsError()) return false;
    region.pages->Close(kernel);
    m_dense_bytes -= region.size;
    m_dense_regions.erase(it);
    return true;
}

void IGpuWorker::Handshake(HLERequestContext& ctx) {
    // The acquired worker is a separate KSession but shares the same
    // NS2 TIPC-0 token-echo contract as its parent GPU session.
    // ResponseBuilder zeroes the shared command buffer on construction.
    // Capture both input words before creating it or the handshake echoes 0.
    const auto* command = ctx.CommandBuffer();
    const u32 token_lo = command[2];
    const u32 token_hi = command[3];
    IPC::ResponseBuilder rb{ctx, 4};
    rb.PushRaw<u32>(ResultSuccess.raw);
    rb.PushRaw<u32>(token_lo);
    rb.PushRaw<u32>(token_hi);
}

Result IGpuWorker::HandleSyncRequest(Kernel::KServerSession& session,
                                     HLERequestContext& ctx) {
    #ifdef PS5_NATIVE
    if (ctx.IsTipc()) {
        static std::atomic<unsigned> requests{0};
        const auto n = requests.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 48 || (n & (n - 1)) == 0)
            std::fprintf(stderr, "EDEN_NS2_IPC owner=gpu_worker cmd=%u known=%u count=%u\n",
                         static_cast<unsigned>(ctx.GetCommand()),
                         static_cast<unsigned>(handlers_tipc.contains(ctx.GetCommand())), n);
    }
    #endif
    if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand())) {
        return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
    }
    const auto guard = LockService();
    InvokeRequestTipc(ctx);
    // 216 remaps bounded, verified NVMap handles into native sparse VA;
    // Ampere graphics queues and GPU submission remain unsupported.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
    return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
}

namespace GW = GpuWorkerProtocol;
namespace {
Result TranslateMappingStatus(GW::LedgerStatus status) {
    switch (status) {
    case GW::LedgerStatus::Success: return ResultSuccess;
    case GW::LedgerStatus::Invalid: return Kernel::ResultInvalidCombination;
    case GW::LedgerStatus::Overlap: return Kernel::ResultInvalidState;
    case GW::LedgerStatus::NotFound: return Kernel::ResultNotFound;
    case GW::LedgerStatus::OutOfResource: return Kernel::ResultOutOfResource;
    }
    return Kernel::ResultInvalidState;
}
} // namespace

void IGpuWorker::NormalMapGpuMemory4K(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    std::array<GW::U32, 18> command{};
    std::copy_n(packet, command.size(), command.begin());
    Result result = Kernel::ResultInvalidCombination;
    u32 selected{};
    if (const auto request = GW::DecodeMap(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (!m_quarantined_nv_session &&
            m_shared->address_spaces.Owns(request->address_space) &&
            m_shared->devices.Get(request->device).has_value() &&
            m_shared->events[request->notification] &&
            !m_pending_device_unlocks.contains(request->memory) &&
            std::find(m_memory.begin(), m_memory.end(), request->memory) != m_memory.end()) {
            auto& nvmap = m_module->GetContainer().GetNvMapFile();
            const auto backing = nvmap.GetHandle(request->memory);
            result = Kernel::ResultInvalidCombination;
            if (backing && backing->allocated && request->memory_offset <= backing->orig_size &&
                request->size <= backing->orig_size - request->memory_offset) {
                // OunceVK maps registered backing into an already reserved VA
                // lease. A contained 214 reservation is therefore not an
                // overlap error: its 211 physical views live in a distinct
                // ledger and the AS restores sparse PTEs on 213.
                const bool sparse_overlay =
                    m_mappings.SparseReservationPageShift(
                        request->address_space, request->address, request->size).has_value();
                if (m_mappings.Count() + m_sparse_overlays.Count() >= GW::MaxMappings) {
                    result = Kernel::ResultOutOfResource;
                } else {
                    auto& ledger = sparse_overlay ? m_sparse_overlays : m_mappings;
                    GW::MappingLedger::Pending staged_ledger;
                    const auto preflight = ledger.PrepareMapped(*request, staged_ledger);
                    result = TranslateMappingStatus(preflight);
                    if (preflight == GW::LedgerStatus::Success) {
                        result = Kernel::ResultInvalidState;
                        const auto as = m_shared->FindOwnedAddressSpace(
                            static_cast<Nvidia::DeviceFD>(request->address_space));
                        auto identity = m_shared->identity_gpu_mappings.end();
                        if (as && !sparse_overlay && request->memory_offset == 0) {
                            identity = std::find_if(m_shared->identity_gpu_mappings.begin(),
                                m_shared->identity_gpu_mappings.end(),
                                [&](const IdentityGpuMapping& entry) {
                                    return entry.handle == request->memory &&
                                           entry.address == request->address &&
                                           entry.size == request->size &&
                                           entry.HasSpace(request->address_space);
                                });
                        }
                        const bool promote_identity =
                            identity != m_shared->identity_gpu_mappings.end();
                        // A previous raw NVDRV kind update may have changed this
                        // identity even when the requested kind is zero. Serialize
                        // the rare ownership transfer with GPU readers like 212.
                        if (promote_identity)
                            system.GPU().WaitForIdle();
                        const auto mapped = as
                            ? (promote_identity
                                   ? as->PromoteNativeIdentity4K(request->memory, request->address,
                                                                 request->size, request->backend_kind,
                                                                 request->cache != 0)
                                   : as->MapNative4K(request->memory, request->address,
                                                     request->memory_offset, request->size,
                                                     request->backend_kind,
                                                     request->cache != 0))
                            : (!sparse_overlay
                                   ? m_shared->StageBeforeImport(*request)
                                   : Nvidia::NvResult::InvalidState);
                        if (mapped == Nvidia::NvResult::InsufficientMemory)
                            result = Kernel::ResultOutOfResource;
                        if (mapped == Nvidia::NvResult::Success) {
                            const auto committed = ledger.CommitPrepared(staged_ledger);
                            if (committed == GW::LedgerStatus::Success) {
                                // 213/102 now retire this mapping through the 211 ledger.
                                if (promote_identity)
                                    identity->RemoveSpace(request->address_space);
                                selected = request->selected_kind;
                                result = ResultSuccess;
                                m_shared->events[request->notification]->Signal(kernel);
                            } else {
                                const bool undone = as
                                    ? (promote_identity
                                           ? as->PromoteNativeIdentity4K(request->memory,
                                                                         request->address,
                                                                         request->size, 0, false)
                                           : as->UnmapNative4K(request->address, request->size)) ==
                                          Nvidia::NvResult::Success
                                    : m_shared->CancelBeforeImport(request->address_space,
                                                                   request->address, request->size);
                                if (!undone) {
                                    // The AS still owns a pin/PTE not represented
                                    // by a committed worker ledger. Fail closed.
                                    m_quarantined_nv_session = true;
                                    result = Kernel::ResultInvalidState;
                                } else {
                                    result = TranslateMappingStatus(committed);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(selected);
}

void IGpuWorker::NormalModifyGpuMemory4K(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    std::array<GW::U32, 11> command{};
    std::copy_n(packet, command.size(), command.begin());
    Result result = Kernel::ResultInvalidCombination;
    u32 selected{};
    if (const auto request = GW::DecodeModify(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->address_spaces.Owns(request->address_space) &&
            m_shared->events[request->notification]) {
            auto preflight = m_mappings.CanModify(*request);
            if (preflight != GW::LedgerStatus::Success)
                preflight = m_sparse_overlays.CanModify(*request);
            result = TranslateMappingStatus(preflight);
            if (preflight == GW::LedgerStatus::Success) {
                result = Kernel::ResultInvalidState;
                const auto as = m_shared->FindOwnedAddressSpace(
                    static_cast<Nvidia::DeviceFD>(request->address_space));
                if (as) {
                    BlockingBoundaryTrace trace{"212_wait_idle", request->address_space,
                                                request->address, request->size};
                    system.GPU().WaitForIdle();
                }
                if (as ? as->ModifyNativeKind(request->address, request->size,
                                               request->backend_kind) == Nvidia::NvResult::Success
                       : m_shared->ModifyBeforeImport(*request)) {
                    selected = request->selected_kind;
                    result = ResultSuccess;
                    m_shared->events[request->notification]->Signal(kernel);
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(selected);
}

void IGpuWorker::NormalUnmapGpuMemory4K(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    std::array<GW::U32, 9> command{};
    std::copy_n(packet, command.size(), command.begin());
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = GW::DecodeUnmap(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->address_spaces.Owns(request->address_space) &&
            m_shared->events[request->notification]) {
            auto* ledger = &m_mappings;
            auto preflight = ledger->CanUnmap(*request);
            if (preflight != GW::LedgerStatus::Success) {
                const auto overlay = m_sparse_overlays.CanUnmap(*request);
                if (overlay == GW::LedgerStatus::Success) {
                    ledger = &m_sparse_overlays;
                    preflight = overlay;
                }
            }
            result = TranslateMappingStatus(preflight);
            if (preflight == GW::LedgerStatus::Success) {
                result = Kernel::ResultInvalidState;
                const auto as = m_shared->FindOwnedAddressSpace(
                    static_cast<Nvidia::DeviceFD>(request->address_space));
                if (as) {
                    BlockingBoundaryTrace trace{"213_wait_idle", request->address_space,
                                                request->address, request->size};
                    system.GPU().WaitForIdle();
                }
                if (as ? as->UnmapNative4K(request->address, request->size) ==
                             Nvidia::NvResult::Success
                       : m_shared->CancelBeforeImport(request->address_space,
                                                       request->address, request->size)) {
                    if (ledger->RecordUnmapped(*request) == GW::LedgerStatus::Success) {
                        result = ResultSuccess;
                        m_shared->events[request->notification]->Signal(kernel);
                    }
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    // libounce gsvGpuUnmap uses tipcDispatchIn: command 213 returns only
    // Result. The completion slot is the operation-complete signal.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::CreateSparseRegion(HLERequestContext& ctx) {
    const auto request = DecodeGpuSparseRegion(
        std::span<const u32>{ctx.CommandBuffer(), 12});
    Result result = Kernel::ResultInvalidCombination;
    bool dense_backed = false;
    if (request) {
        result = Kernel::ResultNotImplemented;
        if (IsValidGpuSparseRegionRange(*request) &&
            (request->page_shift == 12 || request->page_shift == 16) &&
            request->allocation_mode <= 1 && request->sync <= 1 &&
            request->notification < GW::MaxNotifications) {
            const std::scoped_lock lock{m_mutex, m_shared->mutex};
            result = Kernel::ResultNotFound;
            if (m_shared->events[request->notification] &&
                m_shared->address_spaces.Owns(request->address_space) &&
                m_shared->devices.Get(request->device).has_value()) {
                const u64 size = request->end - request->begin + 1;
                const auto replaced =
                    m_mappings.ExactSparse(request->address_space, request->begin);
                GW::MappingLedger::Pending staged_ledger;
                // Lichii/MKW: the title grows a sparse region at the exact
                // same start VA without a preceding 215. Preallocate the new
                // ledger node while the old owner is still intact, but never
                // permit replacement of an unrelated overlapping mapping.
                const auto preflight = replaced
                    ? m_mappings.PrepareSparseReplacingExact(
                          request->address_space, request->device, request->begin,
                          size, staged_ledger, request->page_shift)
                    : m_mappings.PrepareSparse(
                          request->address_space, request->device, request->begin,
                          size, staged_ledger, request->page_shift);
                result = TranslateMappingStatus(preflight);
                if (preflight == GW::LedgerStatus::Success) {
                    result = Kernel::ResultInvalidState;
                    const auto as = m_shared->FindOwnedAddressSpace(
                        static_cast<Nvidia::DeviceFD>(request->address_space));
                    bool old_retired = !replaced.has_value();
                    if (as && replaced) {
                        auto dense = m_dense_regions.find(
                            {request->address_space, request->begin});
                        const bool physical = dense != m_dense_regions.end();
                        const bool gmmu_already_released =
                            physical && dense->second.gmmu_released;
                        // Replacing 214 is destructive to the old PTEs and
                        // possibly their NVMap pin. Drain old GPU users first,
                        // just like an explicit 215.
                        if (!gmmu_already_released) {
                            BlockingBoundaryTrace trace{
                                "214_replace_wait_idle", request->address_space,
                                request->begin, replaced->size};
                            system.GPU().WaitForIdle();
                        }
                        const auto release_result = gmmu_already_released
                            ? Nvidia::NvResult::Success
                            : as->ReleaseNativeSparse4K(
                                  request->begin, replaced->size);
                        if (release_result == Nvidia::NvResult::InsufficientMemory) {
                            result = Kernel::ResultOutOfResource;
                        } else if (release_result == Nvidia::NvResult::Success) {
                            if (physical) dense->second.gmmu_released = true;
                            const bool physical_retired =
                                !physical || RetireGpuOnlyRegion(
                                    request->address_space, request->begin);
                            if (physical_retired) {
                                const auto released =
                                    m_mappings.RecordReleasedSparse(
                                        request->address_space, request->begin,
                                        replaced->size);
                                result = TranslateMappingStatus(released);
                                old_retired = released == GW::LedgerStatus::Success;
                            }
                        }
                    }
                    if (as && old_retired) {
                        // Retiring the predecessor can succeed while the new
                        // reservation still fails. Never leak that prior
                        // success into the 214 reply.
                        result = Kernel::ResultInvalidState;
                        const auto reserved = as->ReserveNativeSparse4K(
                            request->begin, size, request->page_shift);
                        if (reserved == Nvidia::NvResult::InsufficientMemory)
                            result = Kernel::ResultOutOfResource;
                        if (reserved == Nvidia::NvResult::Success) {
                            const auto committed =
                                m_mappings.CommitPrepared(staged_ledger);
                            if (committed == GW::LedgerStatus::Success) {
                                // TIPC 214 word 7 carries the real sparse flag
                                // used by MKW (0x60c/0xa0c). Preserve the older
                                // word-8 mode as a compatible sparse indication,
                                // but never densely back a flagged multi-GiB VA.
                                if (!IsSparseGpuRegion(*request)) {
                                    result = BackGpuOnlyRegion(
                                        request->address_space, request->begin, size,
                                        request->page_shift);
                                    dense_backed = result.IsSuccess();
                                    if (!dense_backed) {
                                        // Preserve the VA ledger if backend rollback
                                        // fails; otherwise a retry could overlap a
                                        // still-live GMMU VA.
                                        if (as->ReleaseNativeSparse4K(
                                                request->begin, size) ==
                                                Nvidia::NvResult::Success) {
                                            const auto released =
                                                m_mappings.RecordReleasedSparse(
                                                    request->address_space,
                                                    request->begin, size);
                                            if (released != GW::LedgerStatus::Success)
                                                LOG_ERROR(
                                                    Service_AM,
                                                    "NS2 sparse 214 ledger rollback failed as={} VA={:#x}",
                                                    request->address_space,
                                                    request->begin);
                                        } else {
                                            LOG_ERROR(
                                                Service_AM,
                                                "NS2 sparse 214 GMMU rollback failed as={} VA={:#x}",
                                                request->address_space,
                                                request->begin);
                                        }
                                    }
                                } else {
                                    result = ResultSuccess;
                                }
                                if (result.IsSuccess())
                                    m_shared->events[request->notification]->Signal(kernel);
                            } else {
                                as->ReleaseNativeSparse4K(request->begin, size);
                                result = TranslateMappingStatus(committed);
                            }
                        }
                    }
                }
            }
        }
    }
    TraceNs2GpuRegion214(ctx.CommandBuffer(), request, result.raw, dense_backed);
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::DestroySparseRegion(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    std::array<GW::U32, 9> command{};
    std::copy_n(packet, command.size(), command.begin());
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = GW::DecodeReleaseSparse(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->events[request->notification] &&
            m_shared->address_spaces.Owns(request->address_space)) {
            const auto preflight = m_mappings.CanReleaseSparse(
                request->address_space, request->address, request->size);
            result = TranslateMappingStatus(preflight);
            if (preflight == GW::LedgerStatus::Success) {
                result = Kernel::ResultInvalidState;
                const auto as = m_shared->FindOwnedAddressSpace(
                    static_cast<Nvidia::DeviceFD>(request->address_space));
                if (as) {
                    auto dense = m_dense_regions.find(
                        {request->address_space, request->address});
                    const bool physical = dense != m_dense_regions.end();
                    // A mode-1 sparse VA can have real NVMap-backed pages
                    // installed later by TIPC 216 even though it has no
                    // dense 214 owner. Releasing it unmaps GMMU and unpins
                    // those physical pages. Drain in-flight GPU work first
                    // for BOTH modes; only a previously completed dense
                    // release may skip the second drain on retry.
                    if (!physical || !dense->second.gmmu_released)
                        {
                            BlockingBoundaryTrace trace{"215_wait_idle", request->address_space,
                                                        request->address, request->size};
                            system.GPU().WaitForIdle();
                        }
                    // The native GMMU release is destructive. If NVMap or
                    // CPU-alias retirement fails, retry only the latter.
                    // Native sparse release stages the retained pin table before
                    // unmapping GMMU. Its allocation can fail without changing
                    // ownership; expose a retryable resource error, not an
                    // invalid guest GPU state.
                    const auto release_result =
                        (physical && dense->second.gmmu_released)
                            ? Nvidia::NvResult::Success
                            : as->ReleaseNativeSparse4K(request->address, request->size);
                    if (release_result == Nvidia::NvResult::InsufficientMemory)
                        result = Kernel::ResultOutOfResource;
                    if (release_result == Nvidia::NvResult::Success) {
                        if (physical) dense->second.gmmu_released = true;
                        const bool retired = !physical || RetireGpuOnlyRegion(
                            request->address_space, request->address);
                        if (retired) {
                            m_mappings.RecordReleasedSparse(
                                request->address_space, request->address, request->size);
                            result = ResultSuccess;
                            m_shared->events[request->notification]->Signal(kernel);
                        }
                    }
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::ExecuteSparseMapRequests(HLERequestContext& ctx) {
    std::array<GW::U32, 15> command{};
    std::copy_n(ctx.CommandBuffer(), command.size(), command.begin());
    u64 processed{};
    u64 physical_entries{};
    Result result = Kernel::ResultInvalidCombination;
    const auto request = GW::DecodeSparseBatch(command);
    if (request) {
        result = Kernel::ResultInvalidSize;
        const std::size_t bytes =
            static_cast<std::size_t>(request->count) * sizeof(GpuSparseMapRequest);
        // Bounds must be checked before reading untrusted guest memory.
        if (ctx.GetReadBufferSize() == bytes &&
            ctx.GetWriteBufferSize() >= request->count) {
            const auto input = ctx.ReadBuffer();
            if (input.size() == bytes) {
                const std::scoped_lock lock{m_mutex, m_shared->mutex};
                result = Kernel::ResultNotFound;
                if (m_shared->address_spaces.Owns(request->address_space) &&
                    m_shared->events[request->notification]) {
                    std::vector<Nvidia::Devices::NativeSparseRemapEntry> staged;
                    std::vector<u8> selected;
                    result = ResultSuccess;
                    // No guest GMMU change until both output and remap
                    // staging buffers have survived all host allocations.
                    // An untrusted batch must not unwind through the TIPC
                    // dispatcher if vector growth runs out of memory.
                    try {
                    staged.reserve(request->count);
                    selected.reserve(request->count);
                    for (std::size_t i = 0; i < request->count; ++i) {
                        GpuSparseMapRequest element{};
                        std::memcpy(&element, input.data() + i * sizeof(element),
                                    sizeof(element));
                        // No flags member exists in GpuSparseMapRequest.
                        // Resolve the page shift from the original 214 owner
                        // before constructing the NVDRV sparse entry.
                        const auto page_shift = m_mappings.SparsePageShift(
                            request->address_space, u64{element.address}, u64{element.size});
                        if (!page_shift) {
                            result = Kernel::ResultInvalidCurrentMemory;
                            break;
                        }
                        const auto entry = DecodeGpuSparseMap(element, *page_shift);
                        if (!entry) {
                            result = Kernel::ResultNotImplemented;
                            break;
                        }
                        const auto range = Nvidia::Devices::DecodeNativeSparseRemap(*entry);
                        if (!range || !m_mappings.ContainsSparse(
                                          request->address_space,
                                          range->address, range->size, *page_shift)) {
                            result = Kernel::ResultInvalidCurrentMemory;
                            break;
                        }
                        u8 effective = element.fallback_kind;
                        if (range->handle) {
                            if (std::find(m_memory.begin(), m_memory.end(),
                                          range->handle) == m_memory.end()) {
                                result = Kernel::ResultNotFound;
                                break;
                            }
                            const auto kind = Nvidia::Devices::SelectNativeMemoryKind(
                                element.kind, element.fallback_kind);
                            if (!kind) {
                                result = Kernel::ResultNotImplemented;
                                break;
                            }
                            auto& nvmap = m_module->GetContainer().GetNvMapFile();
                            const auto handle = nvmap.GetHandle(range->handle);
                            if (!handle || !handle->allocated ||
                                range->source_offset > handle->orig_size ||
                                range->size > handle->orig_size - range->source_offset) {
                                result = Kernel::ResultInvalidCurrentMemory;
                                break;
                            }
                            effective = kind->selected;
                        }
                        auto staged_entry = *entry;
                        if (!range->handle) {
                            // Lichii 216 GPU-only semantics: unmapping a tile
                            // inside a dense 214 region reveals that region's
                            // own physical backing again. Leaving handle=0
                            // would turn a valid render target into sparse zero
                            // pages until the region is destroyed.
                            auto dense = m_dense_regions.upper_bound(
                                {request->address_space, range->address});
                            if (dense != m_dense_regions.begin()) {
                                --dense;
                                const auto& region = dense->second;
                                if (dense->first.first == request->address_space &&
                                    !region.gmmu_released && !region.nvmap_released &&
                                    range->address >= region.address &&
                                    range->address - region.address < region.size &&
                                    range->size <=
                                        region.size - (range->address - region.address)) {
                                    staged_entry.handle = region.nvmap_handle;
                                    staged_entry.source_pages =
                                        (range->address - region.address) >> *page_shift;
                                }
                            }
                        }
                        staged.push_back(staged_entry);
                        selected.push_back(effective);
                    }
                    } catch (const std::bad_alloc&) {
                        result = Kernel::ResultOutOfResource;
                    }
                    if (result.IsSuccess()) {
                        result = Kernel::ResultInvalidState;
                        const auto as = m_shared->FindOwnedAddressSpace(
                            static_cast<Nvidia::DeviceFD>(request->address_space));
                        // Native 216 may replace and unpin old NVMap pages.
                        // Only a fully admitted batch reaches this point;
                        // complete all GPU reads/writes of its old PTEs
                        // before replacing those physical addresses.
                        if (as) {
                            BlockingBoundaryTrace trace{"216_wait_idle", request->address_space,
                                                        0, request->count};
                            system.GPU().WaitForIdle();
                        }
                        const auto remapped = as
                            ? as->RemapNativeSparse(staged)
                            : Nvidia::NvResult::InvalidState;
                        if (remapped == Nvidia::NvResult::InsufficientMemory)
                            result = Kernel::ResultOutOfResource;
                        if (remapped == Nvidia::NvResult::Success) {
                            processed = request->count;
                            physical_entries = std::count_if(staged.begin(), staged.end(),
                                [](const auto& entry) { return entry.handle != 0; });
                            ctx.WriteBuffer(selected);
                            result = ResultSuccess;
                            m_shared->events[request->notification]->Signal(kernel);
                        }
                    }
                }
            }
        }
    }
    TraceNs2GpuSparse216(request ? request->count : 0, processed, physical_entries, result.raw);
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push(processed);
}

Result IGpuWorker::RegisterMemory(u64 begin, u64 end, u32 flags,
                                  u32 device, u32& id) {
    // Caller holds BOTH m_mutex and m_shared->mutex until NVMap commit.
    R_UNLESS(!m_quarantined_nv_session, Kernel::ResultInvalidState);
    R_UNLESS(flags == 0, Kernel::ResultNotImplemented);
    R_UNLESS(device != 0 && device < GpuSessionProtocol::MaxDevices &&
             m_shared->devices.Get(device).has_value(), Kernel::ResultNotFound);
    R_UNLESS(end >= begin && end != std::numeric_limits<u64>::max(),
             Kernel::ResultInvalidSize);
    const u64 size = end - begin + 1;
    R_UNLESS(Common::IsAligned(begin, Kernel::PageSize),
             Kernel::ResultInvalidAddress);
    R_UNLESS(Common::IsAligned(size, Kernel::PageSize),
             Kernel::ResultInvalidSize);
    R_UNLESS(!m_shared->OverlapsPendingIdentity(begin, size), Kernel::ResultInvalidState);
    R_UNLESS(m_memory.size() < MaxOwnedGpuMemoryHandles,
             Kernel::ResultOutOfResource);

    auto& page_table = m_process->GetPageTable();
    R_UNLESS(page_table.Contains(begin, size),
             Kernel::ResultInvalidCurrentMemory);
    for (u64 address = begin; address <= end;) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page{};
        R_TRY(page_table.QueryInfo(&info, &page, address));
        // NS2 svc 0x80 records the GPU-accessible bit in the actual guest
        // KMemoryBlockManager. A native worker must not pin ordinary guest
        // pages that the game never registered for GPU use. The physical
        // mapping capabilities and lock guards remain mandatory as well.
        const auto required = Kernel::KMemoryState::FlagCanAlignedDeviceMap |
                              Kernel::KMemoryState::FlagReferenceCounted;
        R_UNLESS((info.m_state & required) == required &&
                 True(info.m_attribute & Kernel::KMemoryAttribute::GpuAccessible) &&
                 False(info.m_attribute & (Kernel::KMemoryAttribute::IpcLocked |
                                           Kernel::KMemoryAttribute::Locked)),
                 Kernel::ResultInvalidCurrentMemory);
        // Check progress and avoid overflowing a corrupt memory-info range.
        R_UNLESS(info.m_size > 0 &&
                 info.m_address <= std::numeric_limits<u64>::max() - info.m_size,
                 Kernel::ResultInvalidCurrentMemory);
        const u64 next = info.m_address + info.m_size;
        R_UNLESS(next > address, Kernel::ResultInvalidCurrentMemory);
        address = next;
    }

    // The bounded handle owner must not allocate after a page-table lock
    // or an NVMap handle has been created. Reserve before any such side
    // effect so vector growth cannot strand GPU-accessible guest pages.
    try {
        m_memory.reserve(MaxOwnedGpuMemoryHandles);
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }
    // A session allocation failure happens before any NVMap handle or
    // guest page lock is published for this worker memory registration.
    if (!m_nv_session) {
        try {
            m_nv_session = m_module->GetContainer().OpenSession(m_process).id;
        } catch (const std::bad_alloc&) {
            return Kernel::ResultOutOfResource;
        }
    }
    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    std::shared_ptr<Nvidia::NvCore::NvMap::Handle> handle;
    try {
        R_UNLESS(nvmap.CreateHandle(size, handle) == Nvidia::NvResult::Success,
                 Kernel::ResultOutOfResource);
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }

    bool is_io{};
    const Result locked = page_table.LockForMapDeviceAddressSpace(
        &is_io, begin, size, Kernel::KMemoryPermission::None, true, true);
    if (locked.IsError()) {
        nvmap.FreeHandle(handle->id, false);
        return locked;
    }
    // This process-page lock can identify device/IO mappings that are not
    // ordinary guest RAM. The supervisor's native backing owner rejects
    // those exact mappings; TIPC 101 must apply the same policy instead of
    // advertising them as GPU-accessible NVMap-backed system memory.
    if (is_io) {
        const Result undone = page_table.UnlockForDeviceAddressSpace(begin, size);
        if (undone.IsError()) {
            LOG_ERROR(Service_AM, "NS2 GPU IO registration rollback unlock failed: {:#x}",
                      undone.raw);
        }
        nvmap.FreeHandle(handle->id, false);
        return undone.IsError() ? undone : Kernel::ResultNotImplemented;
    }

    // NVMap allocation may fail after the guest page-table lock. Route
    // host OOM through the same unlock + FreeHandle transaction rollback.
    Nvidia::NvResult allocated = Nvidia::NvResult::InvalidState;
    try {
        allocated = handle->Alloc({}, Kernel::PageSize, 0, begin,
                                  Nvidia::NvCore::SessionId{*m_nv_session});
    } catch (const std::bad_alloc&) {
        // Unsuccessful allocation is handled by the rollback below.
    }
    if (allocated != Nvidia::NvResult::Success) {
        const auto undone = page_table.UnlockForDeviceAddressSpace(begin, size);
        if (undone.IsError())
            LOG_ERROR(Service_AM, "NS2 GPU rollback unlock failed: {:#x}", undone.raw);
        nvmap.FreeHandle(handle->id, false);
        return Kernel::ResultOutOfResource;
    }
    id = handle->id;

    // Lichii NX2 #30/#42: CreateGpuAccessibleMemory publishes an identity
    // mapping immediately. Do not wait for command 211 to make the range
    // visible to the GMMUs.
    try {
        m_shared->identity_gpu_mappings.push_back(
            IdentityGpuMapping{id, begin, size, {}, 0});
    } catch (const std::bad_alloc&) {
        const auto undone = page_table.UnlockForDeviceAddressSpace(begin, size);
        if (undone.IsError())
            LOG_ERROR(Service_AM, "NS2 identity registry rollback unlock failed: {:#x}", undone.raw);
        handle.reset();
        nvmap.FreeHandle(id, false);
        id = 0;
        return undone.IsError() ? undone : Kernel::ResultOutOfResource;
    }
    auto& identity = m_shared->identity_gpu_mappings.back();
    Result identity_result = ResultSuccess;
    bool rollback_safe = true;
    for (const auto owned : m_shared->address_spaces.OwnedIds()) {
        if (!owned) continue;
        const auto as = m_shared->FindOwnedAddressSpace(*owned);
        if (!as) continue;
        Nvidia::NvResult mapped = Nvidia::NvResult::InvalidState;
        try {
            mapped = as->MapNative4K(id, begin, 0, size, 0);
        } catch (const std::bad_alloc&) {
            // A thrown map may have partially published GPU PTEs.
            // Do not retire the physical backing unless rollback is proven.
            rollback_safe = false;
            identity_result = Kernel::ResultOutOfResource;
            break;
        }
        if (mapped != Nvidia::NvResult::Success) {
            if (as->IsNativeSparsePoisoned()) rollback_safe = false;
            identity_result = mapped == Nvidia::NvResult::InsufficientMemory
                ? Kernel::ResultOutOfResource : Kernel::ResultInvalidState;
            break;
        }
        if (!identity.AddSpace(*owned)) {
            // Never leave a successful but untracked identity mapping live.
            if (as->UnmapNative4K(begin, size) != Nvidia::NvResult::Success)
                rollback_safe = false;
            identity_result = Kernel::ResultInvalidState;
            break;
        }
    }
    if (identity_result.IsError()) {
        while (identity.mapped_count) {
            const u32 as_id = identity.mapped_spaces[identity.mapped_count - 1];
            if (const auto as = m_shared->FindOwnedAddressSpace(as_id)) {
                if (as->UnmapNative4K(begin, size) != Nvidia::NvResult::Success)
                    rollback_safe = false;
            } else {
                // A missing owner cannot prove that its channel dropped the GMMU.
                rollback_safe = false;
            }
            identity.RemoveSpace(as_id);
        }
        // Never replay a registration that was not delivered to the guest.
        m_shared->identity_gpu_mappings.pop_back();
        if (!rollback_safe) {
            // Keep the device lock and NVMap session alive until process
            // teardown, exactly as for a poisoned dense/sparse GPU backing.
            m_quarantined_nv_session = true;
            id = 0;
            return Kernel::ResultInvalidState;
        }
        const auto undone = page_table.UnlockForDeviceAddressSpace(begin, size);
        if (undone.IsError())
            LOG_ERROR(Service_AM, "NS2 identity-map rollback unlock failed: {:#x}", undone.raw);
        handle.reset();
        nvmap.FreeHandle(id, false);
        id = 0;
        return undone.IsError() ? undone : identity_result;
    }

    m_memory.push_back(id);
    if (size == 0x2000 &&
        !Nvidia::Devices::NativeGpuProcessRegistry::Instance()
             .RegisterSemaphoreRegion(m_process, begin, size)) {
        // The GPU memory is already fully published at this point. A missing
        // process epoch or impossible mirror-capacity mismatch makes it unsafe
        // to expose the handle without its queue-semaphore contract. Keep the
        // backing owned until worker teardown rather than returning an
        // untracked live allocation to the guest.
        m_quarantined_nv_session = true;
        return Kernel::ResultInvalidState;
    }
    return ResultSuccess;
}

Result IGpuWorker::ReleaseMemory(u32 id, u32 notification) {
    // Caller holds BOTH locks, so event ownership and GPU IDs stay stable.
    R_UNLESS(!m_quarantined_nv_session, Kernel::ResultInvalidState);
    R_UNLESS(notification < m_shared->events.size() &&
             m_shared->events[notification], Kernel::ResultInvalidCombination);
    const auto it = std::find(m_memory.begin(), m_memory.end(), id);
    R_UNLESS(it != m_memory.end(), Kernel::ResultNotFound);

    // FreeHandle is destructive, whereas unlocking the guest page table can
    // fail independently. Retry only the outstanding unlock for a handle
    // already freed; never call FreeHandle a second time on a stale ID.
    auto pending = m_pending_device_unlocks.find(id);
    if (pending != m_pending_device_unlocks.end()) {
        R_UNLESS(pending->second.nvmap_lifetime.expired(), Kernel::ResultBusy);
        R_TRY(m_process->GetPageTable().UnlockForDeviceAddressSpace(
            pending->second.address, pending->second.size));
        if (pending->second.size == 0x2000) {
            Nvidia::Devices::NativeGpuProcessRegistry::Instance()
                .UnregisterSemaphoreRegion(m_process, pending->second.address);
        }
        m_pending_device_unlocks.erase(pending);
        m_memory_debug_labels.erase(id);
        m_memory.erase(it);
        m_shared->events[notification]->Signal(kernel);
        return ResultSuccess;
    }

    auto& nvmap = m_module->GetContainer().GetNvMapFile();
    auto handle = nvmap.GetHandle(id);
    R_UNLESS(handle, Kernel::ResultInvalidState);

    // Lichii #1199: sub 102 must remove every GPU reference to this memory,
    // not only its identity/211 maps. Native 216 sparse remaps retain an NVMap
    // pin and otherwise make 102 return Busy forever. Snapshot those ranges
    // before any destructive mutation. If a range overlays a dense 214 region,
    // restore that region's own backing; mode-1 sparse VA becomes unbacked.
    struct SparseRestoreBatch {
        u32 address_space{};
        std::shared_ptr<Nvidia::Devices::nvhost_as_gpu> owner;
        std::vector<Nvidia::Devices::NativeSparseRemapEntry> entries;
    };
    std::vector<SparseRestoreBatch> sparse_restore;
    try {
        for (const auto owned : m_shared->address_spaces.OwnedIds()) {
            if (!owned) continue;
            auto as = m_shared->FindOwnedAddressSpace(*owned);
            if (!as) continue;
            std::vector<Nvidia::Devices::NativeSparseRange> ranges;
            const auto snapshot = as->SnapshotNativeSparseHandle(id, ranges);
            if (snapshot == Nvidia::NvResult::InsufficientMemory)
                return Kernel::ResultOutOfResource;
            if (snapshot != Nvidia::NvResult::Success)
                return Kernel::ResultInvalidState;
            if (ranges.empty()) continue;

            SparseRestoreBatch batch;
            batch.address_space = *owned;
            batch.owner = std::move(as);
            batch.entries.reserve(ranges.size());
            for (const auto& range : ranges) {
                const auto shift =
                    m_mappings.SparsePageShift(*owned, range.address, range.size);
                if (!shift) return Kernel::ResultInvalidCurrentMemory;

                Nvidia::Devices::NativeSparseRemapEntry entry{};
                entry.flags = *shift == 16 ? 0x2000U : 0x1000U;
                entry.destination_pages = range.address >> *shift;
                entry.pages = range.size >> *shift;

                auto dense = m_dense_regions.upper_bound({*owned, range.address});
                if (dense != m_dense_regions.begin()) {
                    --dense;
                    const auto& region = dense->second;
                    if (dense->first.first == *owned &&
                        range.address >= region.address &&
                        range.address - region.address < region.size &&
                        range.size <= region.size - (range.address - region.address)) {
                        entry.handle = region.nvmap_handle;
                        entry.source_pages = (range.address - region.address) >> *shift;
                    }
                }
                batch.entries.push_back(entry);
            }
            sparse_restore.push_back(std::move(batch));
        }
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }

    // Sub 102 destroys this memory object. Remove all 211 mappings owned by
    // this worker plus the sub-101 identity mappings before testing pins.
    struct Owned211Mapping {
        GW::Mapping mapping;
        bool sparse_overlay{};
    };
    std::vector<Owned211Mapping> mapped_memory;
    try {
        for (const auto& [key, mapping] : m_mappings.Mappings())
            if (!mapping.sparse && mapping.memory == id)
                mapped_memory.push_back(Owned211Mapping{mapping, false});
        for (const auto& [key, mapping] : m_sparse_overlays.Mappings())
            if (mapping.memory == id)
                mapped_memory.push_back(Owned211Mapping{mapping, true});
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }
    auto identity_it = std::find_if(
        m_shared->identity_gpu_mappings.begin(), m_shared->identity_gpu_mappings.end(),
        [id](const IdentityGpuMapping& mapping) { return mapping.handle == id; });
    if (!sparse_restore.empty() || !mapped_memory.empty() ||
        (identity_it != m_shared->identity_gpu_mappings.end() && identity_it->mapped_count))
        system.GPU().WaitForIdle();

    // All allocations completed before this point. Remap each address space
    // independently; a later failure is retryable because this memory handle
    // is not freed until every remaining sparse pin has disappeared.
    u64 restored_sparse_ranges{};
    for (const auto& batch : sparse_restore) {
        const auto remapped = batch.owner->RemapNativeSparse(batch.entries);
        if (remapped == Nvidia::NvResult::InsufficientMemory)
            return Kernel::ResultOutOfResource;
        if (remapped != Nvidia::NvResult::Success)
            return Kernel::ResultInvalidState;
        restored_sparse_ranges += batch.entries.size();
    }
#ifdef PS5_NATIVE
    if (restored_sparse_ranges) {
        static std::atomic<unsigned> destroy_sparse_reports{0};
        const auto n = destroy_sparse_reports.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 32 || (n % 1000) == 0) {
            std::fprintf(stderr,
                         "EDEN_NS2_GPU_102_SPARSE_RESTORE handle=%u ranges=%llu count=%u\n",
                         id, static_cast<unsigned long long>(restored_sparse_ranges), n);
        }
    }
#endif

    for (const auto& owned_map : mapped_memory) {
        const auto& mapping = owned_map.mapping;
        const auto as = m_shared->FindOwnedAddressSpace(mapping.address_space);
        const bool unmapped = as
            ? as->UnmapNative4K(mapping.address, mapping.size) == Nvidia::NvResult::Success
            : m_shared->CancelBeforeImport(mapping.address_space, mapping.address, mapping.size);
        if (!unmapped) {
            m_quarantined_nv_session = true;
            return Kernel::ResultInvalidState;
        }
        const GW::Unmap4K unmap{mapping.address_space, 0, mapping.address, mapping.size, 0};
        auto& ledger = owned_map.sparse_overlay ? m_sparse_overlays : m_mappings;
        if (ledger.RecordUnmapped(unmap) != GW::LedgerStatus::Success)
            return Kernel::ResultInvalidState;
    }

    if (identity_it != m_shared->identity_gpu_mappings.end()) {
        while (identity_it->mapped_count) {
            const u32 as_id = identity_it->mapped_spaces[identity_it->mapped_count - 1];
            const auto as = m_shared->FindOwnedAddressSpace(as_id);
            if (!as || as->UnmapNative4K(identity_it->address, identity_it->size) !=
                           Nvidia::NvResult::Success || as->IsNativeSparsePoisoned()) {
                m_quarantined_nv_session = true;
                return Kernel::ResultInvalidState;
            }
            identity_it->RemoveSpace(as_id);
        }
        m_shared->identity_gpu_mappings.erase(identity_it);
    }

    {
        const std::scoped_lock lock{handle->mutex};
        // Any remaining pins are external to this worker. Keep destruction
        // retryable rather than releasing live GPU backing.
        // A pinned/duplicated resource is not safe to release synchronously.
        // Do not signal completion while external GPU references remain.
        R_UNLESS(handle->dupes == 1 && handle->internal_dupes == 0 &&
                 handle->pins == 0, Kernel::ResultBusy);
    }
    // Stage the recovery node BEFORE freeing NVMap, while no guest address
    // space mutation has happened. Allocation failure preserves ownership.
    try {
        const auto [entry, inserted] = m_pending_device_unlocks.try_emplace(
            id, PendingDeviceUnlock{0, 0, std::weak_ptr<void>{handle}});
        if (!inserted) return Kernel::ResultInvalidState;
        pending = entry;
    } catch (const std::bad_alloc&) {
        return Kernel::ResultOutOfResource;
    }
    // Release our last strong reference so FreeHandle may report unlockable.
    handle.reset();
    std::optional<Nvidia::NvCore::NvMap::FreeInfo> info;
    try {
        info = nvmap.FreeHandleIfUnpinned(id);
    } catch (...) {
        // A failed destructive SMMU mutation cannot be retried as a clean
        // free. Preserve the remaining page lock and session until teardown.
        m_quarantined_nv_session = true;
        return Kernel::ResultInvalidState;
    }
    if (!info) {
        m_pending_device_unlocks.erase(pending);
        return Kernel::ResultBusy;
    }
    pending->second.address = info->address;
    pending->second.size = info->size;
    // If an external cache still owns the NVMap object, do not unlock its
    // process pages. A retry will recheck the weak lifetime without freeing
    // the already-destroyed handle again.
    R_UNLESS(info->can_unlock && pending->second.nvmap_lifetime.expired(),
             Kernel::ResultBusy);
    R_TRY(m_process->GetPageTable().UnlockForDeviceAddressSpace(
        pending->second.address, pending->second.size));
    if (pending->second.size == 0x2000) {
        Nvidia::Devices::NativeGpuProcessRegistry::Instance()
            .UnregisterSemaphoreRegion(m_process, pending->second.address);
    }
    m_pending_device_unlocks.erase(pending);
    m_memory_debug_labels.erase(id);
    m_memory.erase(it);
    m_shared->events[notification]->Signal(kernel);
    return ResultSuccess;
}

void IGpuWorker::CreateGpuAccessibleMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 8> command{
        packet[0], packet[1], packet[2], packet[3],
        packet[4], packet[5], packet[6], packet[7]};
    u32 id{};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto req = DecodeGpuMemoryRegistration(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = RegisterMemory(req->begin, req->end, req->flags, req->device, id);
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGpuWorker::DestroyGpuAccessibleMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 4> command{packet[0], packet[1], packet[2], packet[3]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto release = DecodeGpuMemoryRelease(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = ReleaseMemory(release->id, release->notification);
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::EnsureGpuMemoryComptags(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 3> command{packet[0], packet[1], packet[2]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto id = DecodeEnsureGpuMemoryComptags(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        const auto owned = std::find(m_memory.begin(), m_memory.end(), *id);
        result = Kernel::ResultNotFound;
        if (owned != m_memory.end()) {
            // A handle being destructively released is not a valid comptag
            // owner even though it remains in m_memory until the deferred
            // page-table unlock completes.
            result = Kernel::ResultBusy;
            if (!m_pending_device_unlocks.contains(*id)) {
                const auto handle = m_module->GetContainer().GetNvMapFile().GetHandle(*id);
                // This backend never publishes NVIDIA compressed PTEs:
                // SelectNativeMemoryKind returns the guest's uncompressed
                // fallback. Therefore a live owned handle already satisfies
                // the comptag requirement without allocating fake metadata.
                result = handle ? ResultSuccess : Kernel::ResultInvalidState;
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::SetGpuMemoryUserDebugLabel(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 7> command{
        packet[0], packet[1], packet[2], packet[3],
        packet[4], packet[5], packet[6]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = DecodeGpuMemoryUserDebugLabel(command)) {
        const std::scoped_lock lock{m_mutex};
        result = Kernel::ResultNotFound;
        if (std::find(m_memory.begin(), m_memory.end(), request->id) != m_memory.end()) {
            result = Kernel::ResultBusy;
            if (!m_pending_device_unlocks.contains(request->id)) {
                const auto handle = m_module->GetContainer().GetNvMapFile().GetHandle(request->id);
                result = Kernel::ResultInvalidState;
                if (handle) {
                    try {
                        auto [it, inserted] = m_memory_debug_labels.try_emplace(request->id);
                        (void)inserted;
                        it->second.user = request->value;
                        result = ResultSuccess;
                    } catch (const std::bad_alloc&) {
                        result = Kernel::ResultOutOfResource;
                    }
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::SetGpuMemorySdkDebugLabel(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 5> command{
        packet[0], packet[1], packet[2], packet[3], packet[4]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = DecodeGpuMemorySdkDebugLabel(command)) {
        const std::scoped_lock lock{m_mutex};
        result = Kernel::ResultNotFound;
        if (std::find(m_memory.begin(), m_memory.end(), request->id) != m_memory.end()) {
            result = Kernel::ResultBusy;
            if (!m_pending_device_unlocks.contains(request->id)) {
                const auto handle = m_module->GetContainer().GetNvMapFile().GetHandle(request->id);
                result = Kernel::ResultInvalidState;
                if (handle) {
                    try {
                        auto [it, inserted] = m_memory_debug_labels.try_emplace(request->id);
                        (void)inserted;
                        it->second.sdk = request->value;
                        result = ResultSuccess;
                    } catch (const std::bad_alloc&) {
                        result = Kernel::ResultOutOfResource;
                    }
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::SetGpuSegmentUserDebugLabel(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 11> command{
        packet[0], packet[1], packet[2], packet[3], packet[4], packet[5],
        packet[6], packet[7], packet[8], packet[9], packet[10]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = DecodeGpuSegmentUserDebugLabel(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->FindOwnedAddressSpace(request->address_space)) {
            const auto key = std::make_tuple(
                request->address_space, request->address, request->size);
            const auto existing = m_segment_debug_labels.find(key);
            result = Kernel::ResultOutOfResource;
            if (existing != m_segment_debug_labels.end() ||
                m_segment_debug_labels.size() < GpuWorkerProtocol::MaxMappings) {
                try {
                    auto [it, inserted] = m_segment_debug_labels.try_emplace(key);
                    (void)inserted;
                    it->second.user = request->value;
                    result = ResultSuccess;
                } catch (const std::bad_alloc&) {
                    result = Kernel::ResultOutOfResource;
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGpuWorker::SetGpuSegmentSdkDebugLabel(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 9> command{
        packet[0], packet[1], packet[2], packet[3], packet[4],
        packet[5], packet[6], packet[7], packet[8]};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request = DecodeGpuSegmentSdkDebugLabel(command)) {
        const std::scoped_lock lock{m_mutex, m_shared->mutex};
        result = Kernel::ResultNotFound;
        if (m_shared->FindOwnedAddressSpace(request->address_space)) {
            const auto key = std::make_tuple(
                request->address_space, request->address, request->size);
            const auto existing = m_segment_debug_labels.find(key);
            result = Kernel::ResultOutOfResource;
            if (existing != m_segment_debug_labels.end() ||
                m_segment_debug_labels.size() < GpuWorkerProtocol::MaxMappings) {
                try {
                    auto [it, inserted] = m_segment_debug_labels.try_emplace(key);
                    (void)inserted;
                    it->second.sdk = request->value;
                    result = ResultSuccess;
                } catch (const std::bad_alloc&) {
                    result = Kernel::ResultOutOfResource;
                }
            }
        }
    }
    TraceNs2WorkerResult(static_cast<unsigned>(ctx.GetCommand()), result.raw);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
} // namespace Service::AM
