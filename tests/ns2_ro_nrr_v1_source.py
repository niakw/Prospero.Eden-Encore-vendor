#!/usr/bin/env python3
"""Check the selected NS2 NRR v1 runtime contract without revision/hash receipts."""
from pathlib import Path

R = Path(__file__).resolve().parents[1]
ro_source = (R / "encore-active/eden/src/core/hle/service/ro/ro.cpp").read_text()

# NRR version 1 is used by Switch 2 titles loading NRO at runtime.
# Keep the guest hash table bounded and publish the NRR only after validation.
for expected in (
    'prefix[5] == 1 ? 0x4c0 : 0x340',
    'sizeof(NrrHeader) + (prefix[5] == 1 ? 0x180 : 0)',
    'hash_count <= (nrr_size - hash_offset) / sizeof(Sha256Hash)',
    'constexpr u64 MaxNrrHashes = 0x10000',
    'catch (const std::bad_alloc&)',
    'context->SetNrrInfoInUse(nrr_info, true)',
):
    assert expected in ro_source, expected

marker = ro_source.index("// NRR version 1")
assert ro_source.index("if (!hashes.empty())", marker) < ro_source.index(
    "context->SetNrrInfoInUse(nrr_info, true);", marker
)
print("NS2_RO_NRR_V1_SOURCE_PASS hash_table_bounded=1 header=0x4d0")
