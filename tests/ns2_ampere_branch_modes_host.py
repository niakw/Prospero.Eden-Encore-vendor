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
#include "shader_recompiler/frontend/ampere/program_capture.h"
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::ProgramCaptureState;

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

    // Lichii f7be55d: CALL.REL.NOINC is a forward transfer, not a real
    // function call. Capture must complete at the target EXIT without RET.
    Instruction noinc{{0x00007944, 0x10, 0x03c00000, 0}};
    Instruction regular_call{{0x00007944, 0x10, 0x03800000, 0}};
    Instruction neutral{};
    ProgramCaptureState noinc_capture;
    assert(noinc_capture.Observe(noinc,0)==ProgramCaptureState::Result::Continue);
    assert(noinc_capture.Observe(neutral,1)==ProgramCaptureState::Result::Continue);
    assert(noinc_capture.Observe(EXIT,2)==ProgramCaptureState::Result::Complete);

    ProgramCaptureState regular_capture;
    assert(regular_capture.Observe(regular_call,0)==ProgramCaptureState::Result::Continue);
    assert(regular_capture.Observe(neutral,1)==ProgramCaptureState::Result::Continue);
    assert(regular_capture.Observe(EXIT,2)==ProgramCaptureState::Result::Continue);

    std::cout << "conv=" << taken.instructions.size()
              << " div=" << dropped.instructions.size()
              << " bra=" << normal.instructions.size()
              << " noinc_capture=complete regular_call=await_ret\n";
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
print("NS2_AMPERE_BRANCH_MODES_HOST_PASS conv=branch div=fallthrough ordinary=branch noinc_capture=forward ps5=unverified")
