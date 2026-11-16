#!/usr/bin/env python3
"""Source contract for Switch 2 svcGetInfo InfoType 33 page granule.

Upstream evidence:
- nxnative Bananza profile: GetInfo 0x21 is the page size used by nn::ro::ProtectRelro.
- Lichii/Eglantine current NS2 svc_info.cpp return their 4 KiB guest page constant.
This test deliberately does not admit InfoTypes 29..32; their remote-region semantics are not
qualified for Encore's PS5 graph by this patch.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TYPES = ROOT / "encore-active/eden-backports/src/core/hle/kernel/svc_types.h"
INFO = ROOT / "encore-active/eden-backports/src/core/hle/kernel/svc/svc_info.cpp"
MEM = ROOT / "sources/eden/src/core/memory.h"

types = TYPES.read_text()
info = INFO.read_text()
mem = MEM.read_text()

assert types.count("Nx2VammSize = 33,") == 1
# InfoTypes 29/30 may be supplied by the separate VAMM virtual-region contract.
# Physical remote-memory types 31/32 remain intentionally outside this page-size patch.
assert "Nx2RemoteMemorySize = 31" not in types
assert "Nx2RemoteMemoryUsed = 32" not in types

assert info.count("case InfoType::Nx2VammSize:") == 2
assert '*result = Core::Memory::YUZU_PAGESIZE;' in info
assert 'Switch 2 InfoType 33' in info
assert '#include "core/memory.h"' in info

assert "constexpr std::size_t YUZU_PAGEBITS = 12;" in mem
assert "constexpr u64 YUZU_PAGESIZE = 1ULL << YUZU_PAGEBITS;" in mem

print("NS2_GETINFO_PAGE_SIZE_SOURCE_PASS info=33 page=0x1000 remote_types=unchanged ps5=unverified")
