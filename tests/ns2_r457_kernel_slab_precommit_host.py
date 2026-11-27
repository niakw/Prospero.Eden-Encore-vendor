#!/usr/bin/env python3
"""Verify both direct kernel slab writers precommit sparse PS5 guest backing.

Source guard only; PS5 execution and residency are independently qualified.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
relative = "encore-active/eden/src/core/hle/kernel/init/init_slab_setup.cpp"
target = (root / relative).read_text()
baseline = (root / "sources/eden/src/core/hle/kernel/init/init_slab_setup.cpp").read_text()
assert "EdenNs2CommitGuestBacking" not in baseline
assert target.count("EdenNs2CommitGuestBacking(") == 3  # extern + two guarded admissions
assert target.count("#if defined(PS5_NATIVE) && defined(EDEN_NS2_SPARSE_BACKING)") == 3
old_direct = "system.DeviceMemory().GetPointer<void>(slab_address)"
first = target.split("KVirtualAddress InitializeSlabHeap(", 1)[1].split("size_t CalculateSlabHeapGapSize()", 1)[0]
second = target.split("void KPageBufferSlabHeap::Initialize(", 1)[1].split("} // namespace Kernel", 1)[0]
assert first.index("EdenNs2CommitGuestBacking(") < first.index("system.DeviceMemory().GetPointer<void>(")
assert second.index("EdenNs2CommitGuestBacking(") < second.index(old_direct)
assert "physical - Core::DramMemoryMap::Base, size" in first
assert "physical - Core::DramMemoryMap::Base, slab_size" in second
assert "throw std::bad_alloc{};" in first and "throw std::bad_alloc{};" in second
# Optimized process page fills are a separate raw pointer pathway. A failed
# sparse commit must return guest pages before bitmap publication, too.
kernel = (root / "encore-active/eden/src/core/hle/kernel/k_memory_manager.cpp").read_text()
alloc = kernel.split("Result KMemoryManager::AllocateForProcess(", 1)[1].split(
    "size_t KMemoryManager::Impl::Initialize(", 1)[0]
preflight = alloc.split("// Perform optimized memory tracking", 1)[0]
assert "#if defined(PS5_NATIVE) && defined(EDEN_NS2_SPARSE_BACKING)" in preflight
assert preflight.index("CommitKernelDirectGuestRange(m_system, block.GetAddress()") < preflight.index(
    "catch (const std::bad_alloc&)")
assert "out->Finalize();" in preflight
assert "R_THROW(ResultOutOfMemory);" in preflight
assert "manager.Free(address, pages);" in preflight
assert alloc.index("CommitKernelDirectGuestRange(m_system, block.GetAddress()") < alloc.index(
    "manager.ProcessOptimizedAllocation(") < alloc.index("manager.TrackOptimizedAllocation(")
assert "any_new |= manager.ProcessOptimizedAllocation(" in alloc

# Shared-memory initialization is a third raw guest writer and can
# fail when the sparse physical budget is full. It must remain unexposed
# until the clear succeeds, and return its opened guest pages on OOM.
shared = (root / "encore-active/eden/src/core/hle/kernel/k_shared_memory.cpp").read_text()
init = shared.split("Result KSharedMemory::Initialize(", 1)[1].split(
    "void KSharedMemory::Finalize(", 1)[0]
assert "EdenNs2CommitGuestBacking(" not in shared  # ClearBackingRegion admits itself
assert init.index("m_page_group->AddBlock(") < init.index("buffer.ClearBackingRegion(")
assert init.index("buffer.ClearBackingRegion(") < init.index("memory_reservation.Commit();")
assert init.index("catch (const std::bad_alloc&)") < init.index("m_is_initialized = true;")
assert "m_page_group->CloseAndReset(kernel);" in init
assert "kernel.MemoryManager().Close(m_physical_address, num_pages);" in init
assert "R_THROW(ResultOutOfMemory);" in init

# Demand-zero optimized allocations: zero fill uses HostMemory clear and
# must not precommit the entire page group. Nonzero fill still preflights.
kernel = (root / "encore-active/eden/src/core/hle/kernel/k_memory_manager.cpp").read_text()
alloc = kernel.split("Result KMemoryManager::AllocateForProcess(", 1)[1].split(
    "size_t KMemoryManager::Impl::Initialize(", 1)[0]
optimized = kernel.split("bool KMemoryManager::Impl::ProcessOptimizedAllocation", 1)[1].split(
    "size_t KMemoryManager::Impl::CalculateManagementOverheadSize", 1)[0]
assert "if (optimized && fill_pattern != 0)" in alloc
assert "device_memory.buffer.ClearBackingRegion(" in optimized
assert optimized.index("ClearBackingRegion(") < optimized.index("return any_new;")
assert "CommitKernelDirectGuestRange(kernel.System(), block, num_pages * PageSize);" not in optimized

# Optimized bitmap control metadata is deliberately OWN on the current
# runtime. Unlike bulk guest pages, its first mutation cannot safely discover
# physical OOM from the SIGSEGV promotion path, so admission happens before
# the direct pointer and zero initialization. This supersedes the earlier
# demand-zero metadata experiment while leaving bulk guest data sparse.
metadata = kernel.split("void KMemoryManager::Impl::InitializeOptimizedMemory", 1)[1].split(
    "void KMemoryManager::Impl::TrackUnoptimizedAllocation", 1)[0]
assert "CommitKernelDirectGuestRange(kernel.System(), optimize_pa, bytes);" in metadata
assert metadata.index("CommitKernelDirectGuestRange(") < metadata.index("GetPointer<u64>(")
assert metadata.index("GetPointer<u64>(") < metadata.index("std::memset(")
assert "buffer.ClearBackingRegion(" not in metadata

# Clearing an already-zero optimized bit must still be a read-only no-op; it
# must not perform a redundant write to this already admitted control chunk.
track_unoptimized = kernel.split("void KMemoryManager::Impl::TrackUnoptimizedAllocation", 1)[1].split(
    "void KMemoryManager::Impl::TrackOptimizedAllocation", 1)[0]
assert "if (word & mask)" in track_unoptimized
assert "word &= ~mask;" in track_unoptimized

# Optimized bitmap control metadata is deliberately OWN: unlike bulk zero
# guest data, its first write must not discover OOM inside a signal handler.
outer_opt = kernel.split("Result KMemoryManager::InitializeOptimizedMemory", 1)[1].split(
    "void KMemoryManager::FinalizeOptimizedMemory", 1)[0]
assert "catch (const std::bad_alloc&)" in outer_opt
assert "m_has_optimized_process[pool_index] = false;" in outer_opt
assert "R_THROW(ResultOutOfMemory);" in outer_opt

print("NS2_KERNEL_SLAB_DIRECT_PHYSICAL_PRECOMMIT_OK writers=2")
