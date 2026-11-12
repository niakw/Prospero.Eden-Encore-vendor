#!/usr/bin/env python3
"""Reproduce upstream Lichii #972 null-loader teardown source contract.

A failed game admission happens after kernel initialization. Every PS5
Core::System variant must tear the initialized kernel down before returning.
This does not claim gameplay or on-console boot qualification.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
owners = (
    "encore-active/eden-core/src/core/core_native.cpp",
    "encore-active/eden-core/src/core/core_standard.cpp",
    "encore-active/ns2-runtime/process/core/core.cpp",
)
for path in owners:
    source = (root / path).read_text()
    if_null = source.split("        if (!app_loader) {", 1)[1].split("\n        }", 1)[0]
    assert if_null.count("ShutdownMainProcess();") == 1, path
    assert if_null.index("ShutdownMainProcess();") < if_null.index(
        "return SystemResultStatus::ErrorGetLoader;"), path
    assert source.count("ShutdownMainProcess();") >= 2, path
    assert 'if (load_result != Loader::ResultStatus::Success)' in source, path
base = (root / "sources/eden/src/core/core.cpp").read_text()
assert "if (!app_loader)" in base
print("NS1_NS2_MISSING_LOADER_KERNEL_TEARDOWN_OK owners=3")
