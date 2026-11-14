#!/usr/bin/env python3
"""Bounded source proof for Bananza pre-PC4 low guest writes."""
from pathlib import Path
import hashlib

ROOT=Path(__file__).resolve().parents[1]
FILES={
    "encore-active/eden-core/src/core/arm/dynarmic/g11/arm_dynarmic_64_native_normal.cpp":
        "484ddb27e353979b4398b940045b14dc2ce5af10",
    "encore-active/eden-core/src/core/arm/dynarmic/g11/arm_dynarmic_64_native_dev.cpp":
        "849b0416ad647e39bbc8c6f0f5734b6e0b3a5133",
}

def blob(path: Path) -> str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

for rel,expected in FILES.items():
    p=ROOT/rel
    src=p.read_text()
    assert blob(p)==expected,(rel,blob(p))
    assert src.count("std::atomic<u64> low_guest_write_sequence{0};")==1
    assert src.count("low_guest_write_sequence.fetch_add(1, std::memory_order_relaxed) + 1")==1
    assert src.count("sequence > 64 && (sequence & (sequence - 1)) != 0")==1
    assert src.count("EDEN_GUEST_LOW_WRITE seq={}") == 1
    # Five ordinary stores + five exclusive stores. Reads are untouched.
    assert src.count("if (vaddr < 0x1000) ReportLowGuestWrite(")==10
    read_region=src.split("std::optional<u32> DynarmicCallbacks64::MemoryReadCode",1)[0]
    assert "ReportLowGuestWrite(" not in read_region
    assert "address >= 0x1000" in src
    # The diagnostic records before CheckMemoryAccess, so an invalid low write
    # is still visible even if the guest write itself is rejected.
    first=src.index("if (vaddr < 0x1000) ReportLowGuestWrite(")
    check=src.index("CheckMemoryAccess",first)
    assert first < check

print("P0_BANANZA_LOW_WRITE_SOURCE_PASS files=2 first=64 sampled_after=power_of_two reads_untouched=1 ps5_unverified=1")
