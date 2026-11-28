#!/usr/bin/env python3
"""Host regression for Lichii 6b319471: uniform ATOMG immediate offsets.

Title 4 uses ATOMG.E.ADD through [UR + 4]. Ampere carries a signed 24-bit
byte offset while Maxwell ATOM accepts the representable signed 20-bit subset.
Host-only; PS5 behavior remains unverified until a console run.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
OWNER=ROOT/"encore-active/eden/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h"
source=OWNER.read_text()
for marker in (
    "Lichii 6b319471",
    "offset < -0x80000 || offset > 0x7ffff",
    "static_cast<u32>(offset) & 0xfffffU",
):
    assert marker in source, marker

code=r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

static Instruction Exit() {
    Instruction insn{};
    insn.words[0] = 0x94dU | (7U << 12);
    insn.words[2] = 7U << 23;
    return insn;
}

static Instruction Atomic(std::uint32_t offset24, std::uint32_t data = 12) {
    // Logged upstream encoding, with an independently selectable data GPR.
    Instruction insn{{0xff0479a8, 0x000000f0, 0x081ee144, 0x000fe200}};
    insn.words[1] = (offset24 << 8) | data;
    return insn;
}

static std::uint64_t AtomicWord(const auto& lowered) {
    for (const std::uint64_t word : lowered.instructions)
        if ((word >> 56) == 0xed)
            return word;
    assert(false);
    return 0;
}

int main() {
    const auto check = [](const auto& lowered) {
        if (!lowered) {
            std::cerr << lowered.error << " at " << lowered.error_offset << '\n';
        }
        assert(lowered);
    };
    for (const auto& [offset24, expected20] :
         {std::pair{0U, 0U}, std::pair{4U, 4U}, std::pair{0xfffffcU, 0xffffcU},
          std::pair{0x7ffffU, 0x7ffffU}, std::pair{0xf80000U, 0x80000U}}) {
        const auto lowered=LowerToMaxwell(std::array{Atomic(offset24), Exit()});
        check(lowered);
        const auto atom=AtomicWord(lowered);
        assert(((atom >> 28) & 0xfffffU) == expected20);
        assert(((atom >> 20) & 0xffU) == 12U);
    }
    // R240 overlaps the reserved bank: strict admission must reject it,
    // while the explicitly enabled spill path must preserve the offset.
    const auto high=std::array{Atomic(4U, 240U), Exit()};
    assert(!LowerToMaxwell(high));
    const auto spilled=LowerToMaxwell(high, {}, true);
    check(spilled);
    const auto atom=AtomicWord(spilled);
    assert(((atom >> 28) & 0xfffffU) == 4U);
    assert(((atom >> 20) & 0xffU) < 220U);
    for (const auto offset : {0x80000U, 0xf7ffffU, 0x800000U}) {
        const auto rejected=LowerToMaxwell(std::array{Atomic(offset), Exit()});
        assert(!rejected);
        assert(rejected.error=="native global atomic mode");
    }

}
"""

with tempfile.TemporaryDirectory(prefix="ns2-ampere-atomic-offset-") as td:
    p=Path(td)
    (p/"t.cpp").write_text(code)
    compiler=os.getenv("CXX","c++")
    build=subprocess.run([
        compiler,"-std=c++20","-O2","-Wall","-Wextra","-Werror","-pedantic",
        "-fsanitize=undefined","-fno-sanitize-recover=all",
        "-I",str(ROOT/"encore-active/eden/src"),
        "-I",str(ROOT/"sources/eden/src"),
        str(p/"t.cpp"),"-o",str(p/"test")
    ],text=True,capture_output=True,timeout=90)
    if build.returncode:
        raise RuntimeError("Ampere ATOMG offset harness compile failed:\n"+build.stdout+build.stderr)
    run=subprocess.run([str(p/"test")],text=True,capture_output=True,timeout=30)
    if run.returncode:
        raise RuntimeError("Ampere ATOMG offset harness failed:\n"+run.stdout+run.stderr)

print("NS2_AMPERE_ATOMG_OFFSET_HOST_PASS upstream=6b319471 strict=1 spill=1 signed_bounds=1 overflow=reject ps5=unverified")
