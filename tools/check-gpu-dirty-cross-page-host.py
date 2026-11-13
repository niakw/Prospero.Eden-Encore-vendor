#!/usr/bin/env python3
"""Execute the PS5-selected GPU dirty collector on cross-2KiB CPU writes.

Regression for Lichii nx2 #1586/#1591 (2026-11-13). Never edit the pinned
upstream source or silently equate this host test with a PS5 pixel fix.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
active = ROOT / "encore-active/eden-core/overrides/core/gpu_dirty_memory_manager.h"
original = ROOT / "sources/eden/src/core/gpu_dirty_memory_manager.h"
source = active.read_text()
pristine = original.read_text()
assert "void CollectInPage(PAddr address, size_t size)" in source
assert "(std::min<size_t>)(size, page_size - (address & page_mask))" in source
assert source.index("while (size != 0)") < source.index("CollectInPage(address, in_page)")
assert "back_buffer.back() = current.exchange(t, std::memory_order_acq_rel);" in source
assert "std::scoped_lock gather_lock(gather_guard);" in source
assert "void CollectInPage" not in pristine
assert "void Collect(PAddr address, size_t size)" in pristine

test = r"""
#include "core/gpu_dirty_memory_manager.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <utility>
#include <iterator>

using Range = std::pair<PAddr, std::size_t>;
static std::set<PAddr> CellsFor(const std::vector<Range>& writes) {
    std::set<PAddr> cells;
    for (const auto& [address, bytes] : writes) {
        assert(bytes != 0 && address < UINT64_MAX - bytes - 64);
        const PAddr begin = address & ~PAddr{63};
        const PAddr end = (address + bytes + 63) & ~PAddr{63};
        for (PAddr at = begin; at < end; at += 64) cells.insert(at);
    }
    return cells;
}
static void Check(const std::vector<Range>& writes) {
    Core::GPUDirtyMemoryManager dirty;
    for (const auto& [addr, n] : writes) dirty.Collect(addr, n);
    std::set<PAddr> actual;
    unsigned callbacks = 0;
    std::function<void(PAddr, std::size_t)> observe = [&](PAddr address, std::size_t n) {
        ++callbacks;
        assert(n != 0 && n <= 256 * 1024 && address % 64 == 0 && n % 64 == 0);
        for (PAddr at = address; at < address + n; at += 64) actual.insert(at);
    };
    dirty.Gather(observe);
    assert(actual == CellsFor(writes));
    unsigned repeated = 0;
    std::function<void(PAddr, std::size_t)> must_be_empty =
        [&](PAddr, std::size_t) { ++repeated; };
    dirty.Gather(must_be_empty);
    assert(repeated == 0);
    assert(callbacks != 0);
}
int main() {
    // The original unsplit mask attempted shifts by >=32 on these cases.
    Check({{0x10000 + 2047, 2}});
    Check({{0x10000 + 600, 1960}});
    Check({{0x10000 + 64, 2048}});
    Check({{0x10000, 4096}});
    Check({{0x10000 + 2016, 9000}});
    Check({{0x10000 + 2047, 4096}, {0x20000 + 2, 5000}});
    Check({{0x10040, 64}, {0x10800, 64}, {0x10000 + 2040, 128}});
    // Fully deterministic stress across many 2KiB boundaries and overlaps.
    std::vector<Range> writes;
    std::uint64_t state = 0xc25f27a3e8a4ULL;
    for (unsigned i = 0; i < 4096; ++i) {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        PAddr a = 0x200000 + ((state >> 10) % 32) * 2048 + (state & 2047);
        std::size_t n = 1 + ((state >> 24) % 8192);
        writes.emplace_back(a, n);
    }
    Check(writes);
    // Repeated writes to the same page and two-sided boundaries.
    Check({{0x10000 + 2047, 2}, {0x10000 + 2047, 2},
           {0x10000 + 2048, 4096}, {0x10000 + 8191, 2}});
    std::puts("GPU_DIRTY_CROSS_PAGE_HOST_PASS cases=8 randomized=4096 2k_split=1 "
              "existing_cas_fix=1 asan_ubsan=1 no_ps5_claim=1");
}
"""
compiler = shutil.which("clang++-18") or shutil.which("clang++") or shutil.which("g++")
assert compiler, "Missing C++20 compiler"
with tempfile.TemporaryDirectory(prefix="encore-gpu-dirty-cross-page-") as tmp:
    directory = Path(tmp)
    stub = directory / "core"
    stub.mkdir()
    (stub / "gpu_dirty_memory_manager.h").write_text(source)
    (stub / "device_memory_manager.h").write_text(
        "#pragma once\n#include <cstddef>\n#include <cstdint>\n"
        "using PAddr=std::uint64_t; using u32=std::uint32_t; using u64=std::uint64_t;\n"
        "namespace Core { inline constexpr std::size_t DEVICE_PAGEBITS=12; }\n"
    )
    cpp = directory / "cross_page.cpp"
    cpp.write_text(test)
    exe = directory / "cross_page"
    subprocess.run(
        [compiler, "-std=c++20", "-O1", "-g", "-pthread", "-Wall", "-Wextra",
         "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         "-I", str(directory), str(cpp), "-o", str(exe)],
        check=True, timeout=90,
    )
    subprocess.run([str(exe)], check=True, timeout=40)
