#!/usr/bin/env python3
"""Keep the selected NS2 GPU-access SVC header in lockstep with maintained GetInfo enums."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
active = (root / "encore-active/eden/src/core/hle/kernel/svc_types.h").read_text()
shadow = (root / "encore-active/eden/src/core/hle/kernel/svc_types.h").read_text()

permission = "    PermissionLocked = (1 << 4),\n"
wait32 = "    WaitIfEqual = 2,\n"
assert active.count(permission) == active.count(wait32) == 1
expected = active.replace(
    permission,
    permission + "    GpuAccessible = (1 << 5), // NS2 SVC 0x80; preserve maintained NS1 source ABI\n",
).replace(
    wait32,
    wait32 + "    WaitIfEqual64 = 3, // 8-byte aligned compare, no truncated upper word\n",
)
assert shadow == expected, "gpu_access_svc svc_types.h drifted from maintained Eden backport"
for marker in (
    "Nx2RegionAddress = 29",
    "Nx2RegionSize = 30",
    "Nx2RemoteMemorySize = 31",
    "Nx2RemoteMemoryUsed = 32",
    "Nx2VammSize = 33",
    "GpuAccessible = (1 << 5)",
    "WaitIfEqual64 = 3",
):
    assert marker in shadow, marker

compiler = shutil.which("clang++") or shutil.which("g++")
assert compiler, "C++20 compiler unavailable"
fixture = r"""
#include "core/hle/kernel/svc_types.h"
#include <type_traits>
static_assert(static_cast<unsigned>(Kernel::Svc::InfoType::Nx2RegionAddress) == 29);
static_assert(static_cast<unsigned>(Kernel::Svc::InfoType::Nx2RegionSize) == 30);
static_assert(static_cast<unsigned>(Kernel::Svc::InfoType::Nx2RemoteMemorySize) == 31);
static_assert(static_cast<unsigned>(Kernel::Svc::InfoType::Nx2RemoteMemoryUsed) == 32);
static_assert(static_cast<unsigned>(Kernel::Svc::InfoType::Nx2VammSize) == 33);
static_assert(static_cast<unsigned>(Kernel::Svc::MemoryAttribute::GpuAccessible) == 0x20);
static_assert(static_cast<unsigned>(Kernel::Svc::ArbitrationType::WaitIfEqual64) == 3);
int main() {}
"""
with tempfile.TemporaryDirectory(prefix="ns2-gpu-svc-types-") as td:
    src = Path(td) / "test.cpp"
    exe = Path(td) / "test"
    src.write_text(fixture)
    subprocess.run(
        [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
         "-I", str(root / "encore-active/eden/src"),
         "-I", str(root / "sources/eden/src"),
         str(src), "-o", str(exe)],
        check=True,
        timeout=90,
    )
    subprocess.run([str(exe)], check=True, timeout=30)
print("NS2_GPU_ACCESS_SVC_INFO_TYPES_PASS getinfo=29,30,31,32,33 gpu_attr=0x20 wait64=3 remote_protocol=complete")
