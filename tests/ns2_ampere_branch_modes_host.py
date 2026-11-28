#!/usr/bin/env python3
"""Host regression for Ampere BRA.CONV/BRA.DIV convergence modes.

Backports the reviewed Lichii nx2 semantics only: in Encore's converged
translation model BRA.CONV branches and BRA.DIV falls through. NS1 never
selects this Ampere lowering path.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
owner=root/"encore-active/eden/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h"
source=owner.read_text()
for marker in (
    "const u32 mode = insn.Bits(32, 2);",
    "if (mode == 2)",
    "(insn.words[1] & ~3U)",
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

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

static std::size_t count_branch(const auto& lowered) {
    std::size_t count=0;
    for (const std::uint64_t word : lowered.instructions)
        count += (word >> 52) == 0xe24;
    return count;
}

int main() {
    const std::array<Instruction,3> convergent{{
        {{0x06007947, 0x10 | 3, 0x0b800000, 0}},
        EXIT, EXIT,
    }};
    const auto taken=LowerToMaxwell(convergent);
    assert(taken);
    assert(count_branch(taken)==1);

    const std::array<Instruction,3> divergent{{
        {{0xff007947, 0x10 | 2, 0x0b800000, 0}},
        EXIT, EXIT,
    }};
    const auto dropped=LowerToMaxwell(divergent);
    assert(dropped);
    assert(count_branch(dropped)==0);

    // Ordinary BRA keeps using the same relocation path.
    const std::array<Instruction,3> ordinary{{
        {{0x00007947, 0x10, 0x03800000, 0}},
        EXIT, EXIT,
    }};
    const auto normal=LowerToMaxwell(ordinary);
    assert(normal);
    assert(count_branch(normal)==1);
    std::cout << "conv=" << taken.instructions.size()
              << " div=" << dropped.instructions.size()
              << " bra=" << normal.instructions.size() << "\n";
}
"""
with tempfile.TemporaryDirectory(prefix="ns2-ampere-branch-mode-") as td:
    p=Path(td)
    (p/"t.cpp").write_text(code)
    compiler=os.getenv("CXX","c++")
    c=subprocess.run([
        compiler,"-std=c++20","-O2","-Wall","-Wextra","-Werror","-pedantic",
        "-fsanitize=undefined","-fno-sanitize-recover=all",
        "-I",str(root/"encore-active/eden/src"),
        "-I",str(root/"sources/eden/src"),
        str(p/"t.cpp"),"-o",str(p/"test")
    ],text=True,capture_output=True,timeout=90)
    if c.returncode:
        raise RuntimeError("Ampere branch-mode harness compile failed:\n"+c.stdout+c.stderr)
    r=subprocess.run([str(p/"test")],text=True,capture_output=True,timeout=30)
    if r.returncode:
        raise RuntimeError("Ampere branch-mode harness failed:\n"+r.stdout+r.stderr)
print("NS2_AMPERE_BRANCH_MODES_HOST_PASS conv=branch div=fallthrough ordinary=branch ps5=unverified")
