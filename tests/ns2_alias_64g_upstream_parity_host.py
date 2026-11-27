#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Qualify current Lichii/Eglantine NS2 alias/VAMM geometry.

Current nx2 uses the ordinary 39-bit 64 GiB alias region and exposes its upper
half (32 GiB) as InfoType 29/30 VAMM. The unmerged Lichii #1773 Xenoblade
branch doubled the alias to 128 GiB. That experiment is deliberately not part
of Encore's common NS2 production geometry.

Host/source proof only: no PS5 execution or title boot is claimed.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WAIT = ROOT / "encore-active/eden/src/core/hle/kernel"
KPROCESS = ROOT / "encore-active/eden/src/core/hle/kernel/k_process.cpp"
PAGE_SOURCE = ROOT / "encore-active/eden/src/core/hle/kernel/k_page_table_base.cpp"
ADDR_SOURCE = ROOT / "encore-active/eden/src/core/hle/kernel/k_address_space_info.cpp"
SVC_INFO = ROOT / "encore-active/eden/src/core/hle/kernel/svc/svc_info.cpp"

address = ADDR_SOURCE.read_text()
address_h = (WAIT / "k_address_space_info.h").read_text()
page = PAGE_SOURCE.read_text()
page_h = (WAIT / "k_page_table_base.h").read_text()
process_table = (WAIT / "k_process_page_table.h").read_text()
kprocess_h = (WAIT / "k_process.h").read_text()
kprocess = KPROCESS.read_text()
svc = SVC_INFO.read_text()

# Current Lichii/Eglantine nx2 geometry: 39-bit alias stays 64 GiB. Preserve
# Encore's separate 12-GiB guest-layout heap expansion to 16 GiB.
assert ".size = 64_GiB           , .type = KAddressSpaceInfo::Type::Alias" in address
assert "Ns2HeapInfo" in address and ".size = 16_GiB" in address
for forbidden in (
    "Ns2AliasRegionSize",
    "GetAddressSpaceSize(size_t width, KAddressSpaceInfo::Type type,\n"
    "                                                   bool is_switch2)",
):
    assert forbidden not in address, forbidden
assert address.count(
    "std::size_t KAddressSpaceInfo::GetAddressSpaceSize(size_t width, KAddressSpaceInfo::Type type)"
) == 1
assert "return GetAddressSpaceInfo(width, type).size;" in address

# Remove every API surface introduced solely to propagate the #1773 generation
# flag. KProcess metadata still identifies NS2 for the existing memory budget.
assert address_h.count("GetAddressSpaceSize(std::size_t width, Type type);") == 1
assert "bool is_switch2" not in address_h
assert "bool is_switch2" not in page_h
assert "bool is_switch2" not in process_table
assert "bool is_switch2" not in kprocess_h
assert "aslr_space_start, is_switch2" not in page
assert "aslr_space_start, is_switch2" not in kprocess
assert "aslr_space_start, metadata.IsSwitch2())" not in kprocess
assert kprocess.count("metadata.IsSwitch2()") == 1
assert "m_page_table.GetHeapRegionSize() + m_page_table.GetAliasRegionSize()" in kprocess

# InfoType 29/30 must expose the upper half of the actual alias region: with
# 64 GiB alias this is a 32 GiB VAMM region, matching current nx2.
needle = """        case InfoType::Nx2RegionAddress:
        case InfoType::Nx2RegionSize: {"""
assert svc.count(needle) == 1
block = svc.split(needle, 1)[1].split("        case InfoType::Nx2RemoteMemorySize:", 1)[0]
assert "const u64 half = page_table.GetAliasRegionSize() / 2;" in block
assert "GetInteger(page_table.GetAliasRegionStart()) + half" in block
assert ": half;" in block

# Compile the actual GetAddressSpaceSize implementation in a tiny C++20 model,
# so the source contract also proves the numerical 64/16/6 GiB behavior.
start = address.index(
    "std::size_t KAddressSpaceInfo::GetAddressSpaceSize(size_t width, KAddressSpaceInfo::Type type) {"
)
end = address.index("\n}\n\n} // namespace Kernel", start) + 2
method = address[start:end]
harness = r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
namespace Kernel {
struct KAddressSpaceInfo {
    enum class Type : std::uint32_t { MapSmall, MapLarge, Map39Bit, Heap, Stack, Alias, Count };
    static std::size_t GetAddressSpaceSize(std::size_t width, Type type);
    std::size_t size{};
};
const KAddressSpaceInfo& GetAddressSpaceInfo(std::size_t width, KAddressSpaceInfo::Type type) {
    static const KAddressSpaceInfo alias39{64ULL * 1024 * 1024 * 1024};
    static const KAddressSpaceInfo heap39{16ULL * 1024 * 1024 * 1024};
    static const KAddressSpaceInfo alias36{6ULL * 1024 * 1024 * 1024};
    if (width == 39 && type == KAddressSpaceInfo::Type::Alias) return alias39;
    if (width == 39 && type == KAddressSpaceInfo::Type::Heap) return heap39;
    return alias36;
}
__METHOD__
}
int main() {
    using K = Kernel::KAddressSpaceInfo;
    constexpr std::size_t GiB = 1024ULL * 1024 * 1024;
    const auto alias = K::GetAddressSpaceSize(39, K::Type::Alias);
    assert(alias == 64 * GiB);
    assert(alias / 2 == 32 * GiB);
    assert(K::GetAddressSpaceSize(39, K::Type::Heap) == 16 * GiB);
    assert(K::GetAddressSpaceSize(36, K::Type::Alias) == 6 * GiB);
}
""".replace("__METHOD__", method)

with tempfile.TemporaryDirectory(prefix="ns2-alias64-") as td:
    root = Path(td)
    source = root / "test.cpp"
    exe = root / "test"
    source.write_text(harness)
    compiler = os.getenv("CXX", "c++")
    build = subprocess.run(
        [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
         "-pedantic", "-fsanitize=undefined", "-fno-sanitize-recover=all",
         str(source), "-o", str(exe)],
        text=True, capture_output=True, timeout=60,
    )
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=10)
    if run.returncode:
        raise RuntimeError(run.stdout + run.stderr)

print("NS2_ALIAS_64G_UPSTREAM_PARITY_HOST_PASS alias_gib=64 vamm_gib=32 heap_gib=16 no_1773=1 ps5=unverified")
