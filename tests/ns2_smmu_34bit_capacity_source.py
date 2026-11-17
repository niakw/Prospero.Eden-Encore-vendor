#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
for rel in (
    "sources/eden/src/core/device_memory_manager.h",
    "encore-active/eden-backports/src/core/device_memory_manager.h",
):
    text=(R/rel).read_text()
    assert "physical_max_bits = 34;" in text,rel
    assert "physical_max_bits = 33;" not in text,rel
# PS5 must size reverse mappings from actual configured DRAM, not reserve a
# worst-case 16-GiB reverse map just because the representable range is 34-bit.
engine_hint=R.parent/"headless/backports/eden-ps5-gpu-physical-capacity.patch"
if engine_hint.exists():
    patch=engine_hint.read_text()
    assert "GetIntendedMemorySize() >> Memory::YUZU_PAGEBITS" in patch
print("NS2_SMMU_34BIT_CAPACITY_SOURCE_PASS physical_bits=34 ps5_actual_dram_guard=external")
