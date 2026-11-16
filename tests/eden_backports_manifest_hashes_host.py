#!/usr/bin/env python3
"""Fail closed when maintained Eden backport bytes drift from manifest.json."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ACTIVE = ROOT / "encore-active/eden-backports"
MANIFEST = ACTIVE / "manifest.json"

meta = json.loads(MANIFEST.read_text())
entries = meta["entries"]
assert entries, "eden-backports manifest must not be empty"

checked = 0
for rel, contract in sorted(entries.items()):
    path = ACTIVE / rel
    assert path.is_file(), f"missing maintained Eden backport: {rel}"
    data = path.read_bytes()
    observed = hashlib.sha256(data).hexdigest()
    assert len(data) == contract["bytes"], (
        f"maintained Eden backport byte-count drift: {rel}: "
        f"{len(data)} != {contract['bytes']}"
    )
    assert observed == contract["active"], (
        f"maintained Eden backport SHA256 drift: {rel}: "
        f"{observed} != {contract['active']}"
    )
    checked += 1

print(f"EDEN_BACKPORT_MANIFEST_ACTIVE_HASH_PASS entries={checked}")
