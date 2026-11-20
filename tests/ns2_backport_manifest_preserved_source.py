#!/usr/bin/env python3
"""Check current shipping overlay hashes and immutable upstream source."""
import hashlib, json
from pathlib import Path
R=Path(__file__).resolve().parents[1]
manifest=json.loads((R/"encore-active/eden-backports/manifest.json").read_text())
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
    raw=(R/"encore-active/eden-backports"/rel).read_bytes()
    assert len(raw)==receipts["bytes"],rel
    assert hashlib.sha256(raw).hexdigest()==receipts["active"],rel
    assert manifest["entries"][rel]["active"]==receipts["active"],rel
    assert manifest["entries"][rel]["bytes"]==receipts["bytes"],rel
original=(R/"sources/eden/src/core/device_memory_manager.h").read_bytes()
assert hashlib.sha256(original).hexdigest()=="b98873fdb1d3bef10d0b328d303970b969454ec59ccc51b3effc019ac593b163"
print("NS2_BACKPORT_MANIFEST_PRESERVED_PASS changed=3 upstream=immutable")
