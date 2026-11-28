#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the selected NS2 SVC dispatcher as a real C++ TU under PS5 macros.

This is a Linux host syntax/ABI contract (not SDK link, PS5 package or game).
It intentionally compiles the production dispatcher, not only the ring header.
"""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
active = root / "encore-active"
deps = root / "sources/eden-cpm"
src = active / "eden/src/core/hle/kernel/svc.cpp"
assert src.is_file()
inc = [
    # All maintained NS2 SVC/kernel/GPU/HLE headers are folded into the
    # upstream-relative canonical Eden source root.
    active / "eden/src",
    root / "sources/eden/src",
    root / "sources/eden/src/dynarmic/src",
    root / "sources/fmt/include",
    *sorted((deps / "boost/boost-1.90.0/libs").glob("*/include"))
]
compiler = shutil.which("g++") or shutil.which("clang++")
assert compiler, "C++20 compiler required"
cmd = [
    compiler, "-std=c++20", "-fsyntax-only", "-O0",
    "-Wno-unused-parameter", "-DFMT_HEADER_ONLY",
    "-DPS5_NATIVE=1", "-DEDEN_NS2_SVC_TRACE=1",
    *["-I" + str(path) for path in inc if path.is_dir()],
    str(src)
]
result = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
if result.returncode:
    raise AssertionError(
        "Selected SVC TU native-macro compilation failed (" +
        str(result.returncode) + "):\n" + result.stderr[-8500:])
print("NS2_SVC_REAL_NATIVE_TU_HOST_PASS selected=gpu_access_svc PS5_NATIVE=1 EDEN_NS2_SVC_TRACE=1")
print("Host syntax only; not PS5 SDK, link or console.")
