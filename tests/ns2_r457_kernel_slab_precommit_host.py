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
print("NS2_KERNEL_SLAB_DIRECT_PHYSICAL_PRECOMMIT_OK writers=2")
