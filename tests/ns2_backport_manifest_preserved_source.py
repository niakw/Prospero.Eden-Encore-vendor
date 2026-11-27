#!/usr/bin/env python3
"""Check current shipping overlay hashes and immutable upstream source."""
import hashlib, json
from pathlib import Path
R=Path(__file__).resolve().parents[1]
manifest=json.loads((R/"encore-active/eden/backports/manifest.json").read_text())
expected={
  "src/core/device_memory_manager.h": {
    "active": "87ac22b80a44c637d01f031bb26ab573a000baba5b522269c1f6f2cd4e9ba17a",
    "bytes": 11442
  },
  "src/core/hle/kernel/svc/svc_info.cpp": {
    "active": "c93ecde553212fd4b74858fb6e30aa99524b659ef909048f1325ca0cfc6a7849",
    "bytes": 15845
  },
  "src/core/hle/kernel/svc_types.h": {
    "active": "fe9c75c0a63cf3b5519de3790230e8828780dcd1988e023bc75dc5760d96cafd",
    "bytes": 14838
  }
}
for rel,receipts in expected.items():
    raw=(R/"encore-active/eden"/rel).read_bytes()
    assert len(raw)==receipts["bytes"],rel
    assert hashlib.sha256(raw).hexdigest()==receipts["active"],rel
    assert manifest["entries"][rel]["active"]==receipts["active"],rel
    assert manifest["entries"][rel]["bytes"]==receipts["bytes"],rel
original=(R/"sources/eden/src/core/device_memory_manager.h").read_bytes()
assert hashlib.sha256(original).hexdigest()=="b98873fdb1d3bef10d0b328d303970b969454ec59ccc51b3effc019ac593b163"
print("NS2_BACKPORT_MANIFEST_PRESERVED_PASS changed=3 upstream=immutable")

# NRR version 1 is used by Switch 2 titles loading NRO at runtime.
# Source and manifest must agree, and no unbounded guest count may allocate.
ro_rel="src/core/hle/service/ro/ro.cpp"
ro_data=(R/"encore-active/eden"/ro_rel).read_bytes()
assert manifest["entries"][ro_rel]["active"]==hashlib.sha256(ro_data).hexdigest()
assert manifest["entries"][ro_rel]["bytes"]==len(ro_data)
ro_source=ro_data.decode("utf-8")
for expected in ('prefix[5] == 1 ? 0x4c0 : 0x340',
                 'sizeof(NrrHeader) + (prefix[5] == 1 ? 0x180 : 0)',
                 'hash_count <= (nrr_size - hash_offset) / sizeof(Sha256Hash)',
                 'constexpr u64 MaxNrrHashes = 0x10000',
                 'catch (const std::bad_alloc&)',
                 'context->SetNrrInfoInUse(nrr_info, true)'):
    assert expected in ro_source, expected
assert ro_source.index("if (!hashes.empty())") < ro_source.index("context->SetNrrInfoInUse(nrr_info, true);",ro_source.index("// NRR version 1"))
print("NS2_RO_NRR_V1_SOURCE_PASS hash_table_bounded=1 header=0x4d0")
