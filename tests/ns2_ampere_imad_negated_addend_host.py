#!/usr/bin/env python3
"""Qualify Lichii 6df7441 / #2083: Ampere IMAD register addend negation.

Host-only. Exercises the real Encore lowerer with the logged Orbitals form,
its positive counterpart, RZ addend, and a still-invalid modifier.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
owner=root/"encore-active/eden/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h"
source=owner.read_text()
assert "Lichii 6df7441 / #2083" in source
assert "(insn.words[2] & ~0xaffU) != 0x078e0000" in source
assert "if (insn.Bits(75, 1) && addend != 255)" in source
assert "addend = 252;" in source

code=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

static Instruction exit_insn() {
    Instruction insn{};
    insn.words[0] = 0x94dU | (7U << 12);
    insn.words[2] = 7U << 23;
    return insn;
}

static auto program(Instruction body) {
    return std::array{body, exit_insn()};
}

int main() {
    // Lichii #2083: IMAD R3, R2, R3, -R5 (bit 75 negates the addend).
    Instruction negative{{0x02037224, 0x00000003, 0x078e0a05, 0x000fca00}};
    const auto neg = LowerToMaxwell(program(negative));
    assert(neg);
    assert(neg.source_size == 2 * sizeof(Instruction));

    auto positive = negative;
    positive.words[2] &= ~0x800U;
    const auto pos = LowerToMaxwell(program(positive));
    assert(pos);
    // The negative form materializes -R5 once before the same multiply sequence.
    assert(neg.instructions.size() == pos.instructions.size() + 1);
    bool saw_negated_r5 = false;
    for (const std::uint64_t word : neg.instructions) {
        if ((word >> 56) == 0x5c &&
            (word & 0xffU) == 252U &&
            ((word >> 8) & 0xffU) == 255U &&
            ((word >> 20) & 0xffU) == 5U &&
            ((word >> 48) & 1U) == 1U) {
            saw_negated_r5 = true;
        }
    }
    assert(saw_negated_r5);

    // Negating RZ is still zero and must not allocate a needless temporary.
    auto rz_negative = negative;
    rz_negative.words[2] = (rz_negative.words[2] & ~0xffU) | 0xffU;
    const auto rz_neg = LowerToMaxwell(program(rz_negative));
    assert(rz_neg);
    auto rz_positive = rz_negative;
    rz_positive.words[2] &= ~0x800U;
    const auto rz_pos = LowerToMaxwell(program(rz_positive));
    assert(rz_pos);
    assert(rz_neg.instructions.size() == rz_pos.instructions.size());

    // A separate unsupported modifier remains rejected; widening is bit75-only.
    auto bad = negative;
    bad.words[2] |= 1U << 12;
    const auto rejected = LowerToMaxwell(program(bad));
    assert(!rejected);
    assert(rejected.error == "native integer register multiply mode");

    std::cout << "neg=" << neg.instructions.size()
              << " pos=" << pos.instructions.size()
              << " rz=" << rz_neg.instructions.size() << '\n';
}
"""

with tempfile.TemporaryDirectory(prefix="ns2-imad-neg-addend-") as td:
    p=Path(td)
    (p/"t.cpp").write_text(code)
    compiler=os.getenv("CXX","c++")
    compile=subprocess.run(
        [compiler,"-std=c++20","-O2","-Wall","-Wextra","-Werror","-pedantic",
         "-fsanitize=undefined","-fno-sanitize-recover=all",
         "-I",str(root/"encore-active/eden/src"),
         "-I",str(root/"sources/eden/src"),
         str(p/"t.cpp"),"-o",str(p/"test")],
        text=True,capture_output=True,timeout=90)
    if compile.returncode:
        raise RuntimeError("IMAD negated-addend harness compile failed:\n"+compile.stdout+compile.stderr)
    run=subprocess.run([str(p/"test")],text=True,capture_output=True,timeout=30)
    if run.returncode:
        raise RuntimeError("IMAD negated-addend harness failed:\n"+run.stdout+run.stderr)

print("NS2_AMPERE_IMAD_NEGATED_ADDEND_HOST_PASS upstream=6df7441 orbitals=1 positive=1 rz=1 invalid=1 ps5=unverified")
