#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pin the narrow upstream NS2 KProcess memory-budget backport.

This is source qualification only. It proves the selected successor differs
from the pinned Eden owner only by Eglantine/Lichii dce3adbc's IsSwitch2
alias-region memory budget. It does not prove a PS5 guest stack maps.
"""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "sources/eden/src/core/hle/kernel/k_process.cpp"
ACTIVE = ROOT / "encore-active/ns2-runtime/process/core/hle/kernel/k_process.cpp"

base = BASE.read_text()
active = ACTIVE.read_text()

block = """    // Upstream Eglantine/Lichii dce3adbc: Switch 2 applications size
    // nninitStartup heap from total memory, which exceeds the ordinary
    // 64-bit heap region. Count the alias region too, bounded by the
    // process resource limit. This changes no KProcess class layout and
    // is inert for Switch 1 metadata.
    if (metadata.IsSwitch2()) {
        m_max_process_memory =
            m_page_table.GetHeapRegionSize() + m_page_table.GetAliasRegionSize();
    }

"""

assert active.count(block) == 1
assert base.count(block) == 0
assert active.count("metadata.IsSwitch2()") == 1
assert "m_is_switch2" not in active
assert "IsSwitch2() const" not in active

restored = active.replace(block, "", 1)
assert restored == base

def blob(text: str) -> str:
    raw = text.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()

assert blob(base) == "f338b293eb5feadceed8948b89ba0b77dcd17c26"
assert blob(active) == "dfef3558a4649b0c9ee92c1c5ed3a21e1c2910d3"

method = active.split("Result KProcess::LoadFromMetadata(", 1)[1].split(
    "\nvoid KProcess::LoadModule(", 1
)[0]
assert method.index("this->Initialize(") < method.index("metadata.IsSwitch2()")
assert method.index("metadata.IsSwitch2()") < method.index("InitializeInterfaces(kernel)")
assert method.count("GetHeapRegionSize() + m_page_table.GetAliasRegionSize()") == 1

print("NS2_KPROCESS_MEMORY_BUDGET_SOURCE_PASS upstream=dce3adbc abi_layout_unchanged=1")
