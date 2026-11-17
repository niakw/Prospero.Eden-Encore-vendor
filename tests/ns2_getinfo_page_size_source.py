#!/usr/bin/env python3
"""Source contract for Switch 2 svcGetInfo InfoType 33 page granule.

Upstream evidence:
- nxnative Bananza profile: GetInfo 0x21 is the page size used by nn::ro::ProtectRelro.
- Lichii/Eglantine current NS2 svc_info.cpp return their 4 KiB guest page constant.
InfoTypes 29..32 are qualified by the separate VAMM/remote-memory contract. This test keeps
InfoType 33's page-granule proof independent while requiring the combined protocol to stay present.
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
# Keep the page-size proof composable with the separately reviewed 29..32 contract.
assert types.count("Nx2RegionAddress = 29,") == 1
assert types.count("Nx2RegionSize = 30,") == 1
assert types.count("Nx2RemoteMemorySize = 31,") == 1
assert types.count("Nx2RemoteMemoryUsed = 32,") == 1

assert info.count("case InfoType::Nx2VammSize:") == 2
assert '*result = Core::Memory::YUZU_PAGESIZE;' in info
assert 'Switch 2 InfoType 33' in info
assert '#include "core/memory.h"' in info

assert "constexpr std::size_t YUZU_PAGEBITS = 12;" in mem
assert "constexpr u64 YUZU_PAGESIZE = 1ULL << YUZU_PAGEBITS;" in mem

print("NS2_GETINFO_PAGE_SIZE_SOURCE_PASS info=33 page=0x1000 remote_types=29,30,31,32 composed=1 ps5=unverified")
