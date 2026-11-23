#!/usr/bin/env python3
"""Verify both direct kernel slab writers precommit sparse PS5 guest backing.

Source guard only; PS5 execution and residency are independently qualified.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
relative = "encore-active/ns2-runtime/sparse-residency/core/hle/kernel/init/init_slab_setup.cpp"
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
kernel = (root / "encore-active/ns2-runtime/sparse-residency/core/hle/kernel/k_memory_manager.cpp").read_text()
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
shared = (root / "encore-active/ns2-runtime/sparse-residency/core/hle/kernel/k_shared_memory.cpp").read_text()
init = shared.split("Result KSharedMemory::Initialize(", 1)[1].split(
    "void KSharedMemory::Finalize(", 1)[0]
assert "EdenNs2CommitGuestBacking(" not in shared  # ClearBackingRegion admits itself
assert init.index("m_page_group->AddBlock(") < init.index("buffer.ClearBackingRegion(")
assert init.index("buffer.ClearBackingRegion(") < init.index("memory_reservation.Commit();")
assert init.index("catch (const std::bad_alloc&)") < init.index("m_is_initialized = true;")
assert "m_page_group->CloseAndReset(kernel);" in init
assert "kernel.MemoryManager().Close(m_physical_address, num_pages);" in init
assert "R_THROW(ResultOutOfMemory);" in init

print("NS2_KERNEL_SLAB_DIRECT_PHYSICAL_PRECOMMIT_OK writers=2")
