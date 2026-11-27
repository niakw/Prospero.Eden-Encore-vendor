#!/usr/bin/env python3
"""CPU6 A64 must preserve the selected G11 Bananza low-write witness exactly."""
from pathlib import Path
import hashlib

ROOT=Path(__file__).resolve().parents[1]
G11=ROOT/"encore-active/eden/src/core/arm/dynarmic/g11/arm_dynarmic_64_native_normal.cpp"
CPU6=ROOT/"encore-active/ns2-runtime/cpu6/maintained/core/arm/dynarmic/arm_dynarmic_64.cpp"

def blob(text: str) -> str:
    raw=text.encode()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

g11=G11.read_text()
cpu6=CPU6.read_text()
for needle in (
    "EDEN_GUEST_LOW_WRITE",
    "low_guest_write_sequence",
    "ReportLowGuestWrite",
    '#include "core/hle/kernel/k_thread.h"',
):
    assert needle in g11 and needle in cpu6, needle

old="    u64 amortized_ticks = ticks / Core::Hardware::NUM_CPU_CORES;"
new="""    // Preserve Switch 1 tick cadence even when the shared kernel exposes six cores.
    // Physical affinity, never TitleID, selects the six-core division.
    const u64 extended_cores = m_process->GetPhysicalCoreMask() & ((u64{1} << 4) | (u64{1} << 5));
    u64 amortized_ticks = ticks / (extended_cores ? Core::Hardware::NUM_CPU_CORES
                                                     : Core::Hardware::NUM_LEGACY_CPU_CORES);"""
assert g11.count(old)==1
expected=g11.replace(old,new,1)
assert cpu6==expected,(blob(cpu6),blob(expected))
assert blob(cpu6)=="90ef34cf1ce04ea2a199622beb4ac0a8c89cb128"
print("CPU6_BANANZA_LOW_WRITE_PARITY_PASS g11=1 cpu6=1 low_write=1 tick_split=1 ps5=unverified")
