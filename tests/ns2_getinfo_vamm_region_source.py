#!/usr/bin/env python3
"""Source contract for Switch 2 svcGetInfo VAMM/remote-memory types 29..33.

Upstream evidence:
- nxnative documents nnosInitialize building RemoteRegionManager over [InfoType 29, 29 + 30).
- Lichii and Eglantine expose the upper half of the process alias region for these two values.
- Encore's selected NS2 39-bit layout has a 64 GiB alias region, so the contract exposes 32 GiB
  of virtual VAMM space without allocating physical remote memory.

InfoTypes 31/32 are protocol-complete with zero allowance/usage on base PS5. Current Lichii
also returns zero for titles without an explicitly qualified remote-memory pool; Encore must not
copy a title-specific 512 MiB physical reservation into the constrained PS5 host budget.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
TYPES = ROOT / "encore-active/eden/src/core/hle/kernel/svc_types.h"
INFO = ROOT / "encore-active/eden/src/core/hle/kernel/svc/svc_info.cpp"
ADDR = ROOT / "encore-active/ns2-runtime/kernel/core/hle/kernel/k_address_space_info.cpp"

types = TYPES.read_text()
info = INFO.read_text()
addr = ADDR.read_text()

assert types.count("Nx2RegionAddress = 29,") == 1
assert types.count("Nx2RegionSize = 30,") == 1
assert types.count("Nx2RemoteMemorySize = 31,") == 1
assert types.count("Nx2RemoteMemoryUsed = 32,") == 1
assert types.count("Nx2VammSize = 33,") == 1

assert info.count("case InfoType::Nx2RegionAddress:") == 2
assert info.count("case InfoType::Nx2RegionSize:") == 2
assert "const u64 half = page_table.GetAliasRegionSize() / 2;" in info
assert "GetInteger(page_table.GetAliasRegionStart()) + half" in info
assert info.count("case InfoType::Nx2RemoteMemorySize:") == 2
assert info.count("case InfoType::Nx2RemoteMemoryUsed:") == 2
remote = info.split("case InfoType::Nx2RemoteMemorySize:", 2)[2].split(
    "case InfoType::Nx2VammSize:", 1
)[0]
assert "*result = 0;" in remote
for forbidden in ("512_MiB", "AllocateAndOpen", "GetApplicationPoolGrowth", "Nx2RemoteMemorySize()"):
    assert forbidden not in remote, forbidden

# Selected NS2 kernel layout: the same table row must bind 39-bit + 64 GiB + Alias.
alias39 = re.search(
    r"\{\s*\.bit_width\s*=\s*39\s*,[^\n]*\.size\s*=\s*64_GiB\s*,[^\n]*"
    r"KAddressSpaceInfo::Type::Alias",
    addr,
)
assert alias39, "selected NS2 kernel must expose a 64 GiB alias region"

print("NS2_GETINFO_VAMM_REGION_SOURCE_PASS info=29,30,31,32,33 remote_size=0 remote_used=0 physical_growth=0 ps5=unverified")
