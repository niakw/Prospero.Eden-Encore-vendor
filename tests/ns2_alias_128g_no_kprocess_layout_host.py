#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host/source proof for Lichii #1773 128 GiB NS2 alias without KProcess layout drift."""

from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WAIT = ROOT / "encore-active/ns2-runtime/kernel_wait64/core/hle/kernel"
KPROCESS = ROOT / "encore-active/ns2-runtime/process/core/hle/kernel/k_process.cpp"
PAGE_SOURCE = ROOT / "encore-active/eden-core/src/core/hle/kernel/k_page_table_base.cpp"
ADDR_SOURCE = ROOT / "encore-active/ns2-runtime/kernel/core/hle/kernel/k_address_space_info.cpp"

def blob(text: str) -> str:
    raw = text.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()

# The selected KProcess header gains only a method overload: no new state/layout field.
kprocess_h = (WAIT / "k_process.h").read_text()
overload = """    Result Initialize(KernelCore& kernel, const Svc::CreateProcessParameter& params,
                      std::span<const u32> user_caps, KResourceLimit* res_limit,
                      KMemoryManager::Pool pool, KProcessAddress aslr_space_start,
                      bool is_switch2);
"""
assert kprocess_h.count(overload) == 1
assert "m_is_switch2" not in kprocess_h
assert blob(kprocess_h.replace(overload, "", 1)) == "4474a9e2727d95027e2216be21273322e9e7d595"

# Three new public overlays are exact pinned headers plus one overload each.
pairs = [
    ("k_address_space_info.h", ROOT / "sources/eden/src/core/hle/kernel/k_address_space_info.h",
     """    static std::size_t GetAddressSpaceSize(std::size_t width, Type type,
                                               bool is_switch2);
"""),
    ("k_page_table_base.h", ROOT / "sources/eden/src/core/hle/kernel/k_page_table_base.h",
     """    Result InitializeForProcess(Svc::CreateProcessFlag as_type, bool enable_aslr,
                                bool enable_device_address_space_merge, bool from_back,
                                KMemoryManager::Pool pool, KProcessAddress code_address,
                                size_t code_size, KSystemResource* system_resource,
                                KResourceLimit* resource_limit, Core::Memory::Memory& memory,
                                KProcessAddress aslr_space_start, bool is_switch2);
"""),
]
for name, base, addition in pairs:
    active = (WAIT / name).read_text()
    assert active.count(addition) == 1
    assert active.replace(addition, "", 1) == base.read_text()

process_table = (WAIT / "k_process_page_table.h").read_text()
base_process_table = (ROOT / "sources/eden/src/core/hle/kernel/k_process_page_table.h").read_text()
begin = """
    Result Initialize(Svc::CreateProcessFlag as_type, bool enable_aslr, bool enable_das_merge,
                      bool from_back, KMemoryManager::Pool pool, KProcessAddress code_address,
                      size_t code_size, KSystemResource* system_resource,
                      KResourceLimit* resource_limit, Core::Memory::Memory& memory,
                      KProcessAddress aslr_space_start, bool is_switch2) {
        R_RETURN(m_page_table.InitializeForProcess(
            as_type, enable_aslr, enable_das_merge, from_back, pool, code_address, code_size,
            system_resource, resource_limit, memory, aslr_space_start, is_switch2));
    }
"""
assert process_table.count(begin) == 1
assert process_table.replace(begin, "", 1) == base_process_table

kprocess = KPROCESS.read_text()
assert "m_is_switch2" not in kprocess
assert "aslr_space_start, metadata.IsSwitch2()" in kprocess
assert "aslr_space_start, is_switch2));" in kprocess
assert "aslr_space_start, false));" in kprocess
assert kprocess.index("metadata.IsSwitch2()") < kprocess.index(
    "m_page_table.GetHeapRegionSize() + m_page_table.GetAliasRegionSize()"
)

page = PAGE_SOURCE.read_text()
assert "aslr_space_start, false));" in page
assert "GetAddressSpaceSize(m_address_space_width, type, is_switch2)" in page

address = ADDR_SOURCE.read_text()
assert "Ns2AliasRegionSize = 128_GiB" in address
assert "is_switch2 && width == 39 && type == KAddressSpaceInfo::Type::Alias" in address
start = address.index(
    "std::size_t KAddressSpaceInfo::GetAddressSpaceSize(size_t width, KAddressSpaceInfo::Type type,\n"
    "                                                   bool is_switch2) {"
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
    static std::size_t GetAddressSpaceSize(std::size_t width, Type type, bool is_switch2);
    std::size_t size{};
};
constexpr std::size_t Ns2AliasRegionSize = 128ULL * 1024 * 1024 * 1024;
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
    assert(K::GetAddressSpaceSize(39, K::Type::Alias, false) == 64 * GiB);
    assert(K::GetAddressSpaceSize(39, K::Type::Alias, true) == 128 * GiB);
    assert(K::GetAddressSpaceSize(39, K::Type::Heap, true) == 16 * GiB);
    assert(K::GetAddressSpaceSize(36, K::Type::Alias, true) == 6 * GiB);
}
""".replace("__METHOD__", method)

with tempfile.TemporaryDirectory(prefix="ns2-alias128-") as td:
    p = Path(td)
    source = p / "test.cpp"
    source.write_text(harness)
    compiler = os.getenv("CXX", "c++")
    build = subprocess.run(
        [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-fsanitize=undefined", "-fno-sanitize-recover=all", str(source), "-o", str(p / "test")],
        text=True, capture_output=True, timeout=60,
    )
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    run = subprocess.run([str(p / "test")], text=True, capture_output=True, timeout=10)
    if run.returncode:
        raise RuntimeError(run.stdout + run.stderr)

print("NS2_ALIAS_128G_NO_KPROCESS_LAYOUT_HOST_PASS upstream=1773 ns1_alias_gib=64 ns2_alias_gib=128 kprocess_layout_unchanged=1 ps5=unverified")
