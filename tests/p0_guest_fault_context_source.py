#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact real guest-fault context source owner; pin predecessor, not a false replay."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
# The baseline already includes fault registers and the previous SVC dumper,
# so removing the oldest OWNER/SP_MAP slice alone is no longer reversible.
# First recover the *exact* selected predecessor (vendor main), then check
# the source-owned snapshot and full fault context. Do not rewrite history.
PREDECESSOR = {
    "encore-active/eden/src/core/hle/kernel/physical_core_dev.cpp":
        "4521fd48028b9852cc6929c642bfd906421ea474",
    "encore-active/eden/src/core/hle/kernel/physical_core_standard.cpp":
        "bbd7f7a5a069e6f4407a1e4f0e2b4b333f611911",
}
OLD_DECL = 'extern "C" void eden_ns2_dump_recent_svc(std::uint64_t) noexcept __attribute__((weak));'
NEW_DECL = '''extern "C" void eden_ns2_dump_recent_svc(
    std::uint64_t pid, std::uint64_t program_id, std::uint64_t fault_pc, std::uint64_t fault_lr,
    std::uint64_t fault_sp) noexcept __attribute__((weak));'''
OLD_CALL = "eden_ns2_dump_recent_svc(process->GetProcessId());"
NEW_CALL = '''eden_ns2_dump_recent_svc(process->GetProcessId(),
                            process->GetProgramId(), fault_context.pc,
                            fault_context.lr, fault_context.sp);'''

def blob(text: str) -> str:
    raw = text.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()

for rel, previous in PREDECESSOR.items():
    source = (ROOT / rel).read_text()
    assert source.count(NEW_DECL) == 1, rel
    assert source.count(NEW_CALL) == 1, rel
    for marker in ("EDEN_GUEST_FAULT_OWNER", "EDEN_GUEST_FAULT_REGS",
                   "EDEN_GUEST_FAULT_SP_MAP", "EDEN_GUEST_FAULT_LR_MAP"):
        assert source.count(marker) == 1, (rel, marker)
    assert source.index("EDEN_GUEST_FAULT_OWNER") < source.index("EDEN_GUEST_FAULT_REGS") < (
        source.index("eden_ns2_dump_recent_svc(process->GetProcessId(),")
    ) < source.index("EDEN_GUEST_FAULT_SP_MAP") < source.index("EDEN_GUEST_FAULT_LR_MAP"), rel
    predecessor = source.replace(NEW_DECL, OLD_DECL, 1).replace(NEW_CALL, OLD_CALL, 1)
    assert blob(predecessor) == previous, (rel, blob(predecessor), previous)
    fatal = source[source.index("EDEN_GUEST_FAULT_OWNER") - 800:
                   source.index("EDEN_GUEST_FAULT_LR_MAP")]
    assert "GetMemory()" not in fatal and "ReadMemory(" not in fatal, rel

pinned = (ROOT / "sources/eden/src/core/hle/kernel/physical_core.cpp").read_text()
assert "EDEN_GUEST_FAULT_OWNER" not in pinned
assert "EDEN_GUEST_FAULT_SP_MAP" not in pinned
print("P0_GUEST_FAULT_CONTEXT_SOURCE_PASS title=1 registers=8 sp_lr_maps=1 quiet_bridge=1 immutable_original=1")
