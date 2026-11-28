#!/usr/bin/env python3
"""Qualify Lichii #1511 / 19be851d: Ampere HSETP2 constant-buffer form.

Host-only. Exercises Encore's real native lowerer with the logged constant-buffer
form, the already-known register form, invalid CBUF alignment, and proves the
existing constrained immediate path remains present. No game bytes or PS5 execution are used.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
owner = root / "encore-active/eden/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h"
source = owner.read_text()
assert "Lichii #1511 / 19be851d" in source
assert "case 0x234: case 0x634:" in source
assert "case 0x434: { // HSETP2.GEU.AND" in source

code = r"""
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

static std::size_t count_top16(const auto& lowered, std::uint16_t opcode) {
    std::size_t count = 0;
    for (const std::uint64_t word : lowered.instructions)
        count += static_cast<std::uint16_t>(word >> 48) == opcode;
    return count;
}

static std::uint64_t find_top16(const auto& lowered, std::uint16_t opcode) {
    for (const std::uint64_t word : lowered.instructions)
        if (static_cast<std::uint16_t>(word >> 48) == opcode)
            return word;
    return 0;
}

int main() {
    // Lichii #1511: HSETP2.AND P1, PT, RZ, c[0x7][0xfc], !P1.
    Instruction cbuf{{0xff007634, (0xfcU << 6) | (7U << 22),
                      (4U << 12) | (1U << 17) | (7U << 20) | (9U << 23), 0}};
    const auto lowered = LowerToMaxwell(program(cbuf));
    assert(lowered);
    assert(count_top16(lowered, 0x4c98) == 1);
    assert(count_top16(lowered, 0x5d20) == 1);
    const std::uint64_t load = find_top16(lowered, 0x4c98);
    assert(((load >> 20) & 0x3fffU) == 0xfcU / 4U);
    assert(((load >> 34) & 0x1fU) == 7U);

    // The original register form remains accepted without a CBUF load.
    Instruction reg = cbuf;
    reg.words[0] = (reg.words[0] & ~0xfffU) | 0x234U;
    reg.words[1] = 0;
    reg.words[2] &= ~(0x1fU << 0);
    const auto register_form = LowerToMaxwell(program(reg));
    assert(register_form);
    assert(count_top16(register_form, 0x4c98) == 0);
    assert(count_top16(register_form, 0x5d20) == 1);

    // Misaligned CBUF byte offsets remain rejected.
    auto bad = cbuf;
    bad.words[1] += 1U << 6;
    const auto rejected = LowerToMaxwell(program(bad));
    assert(!rejected);

    std::cout << "cbuf=" << lowered.instructions.size()
              << " register=" << register_form.instructions.size() << '\n';
}
"""

with tempfile.TemporaryDirectory(prefix="ns2-hsetp2-cbuf-") as td:
    p = Path(td)
    (p / "t.cpp").write_text(code)
    compiler = os.getenv("CXX", "c++")
    compile = subprocess.run(
        [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-fsanitize=undefined", "-fno-sanitize-recover=all",
         "-I", str(root / "encore-active/eden/src"),
         "-I", str(root / "sources/eden/src"),
         str(p / "t.cpp"), "-o", str(p / "test")],
        text=True, capture_output=True, timeout=90)
    if compile.returncode:
        raise RuntimeError("HSETP2 CBUF harness compile failed:\n" + compile.stdout + compile.stderr)
    run = subprocess.run([str(p / "test")], text=True, capture_output=True, timeout=30)
    if run.returncode:
        raise RuntimeError("HSETP2 CBUF harness failed:\n" + run.stdout + run.stderr)

print("NS2_AMPERE_HSETP2_CBUF_HOST_PASS upstream=19be851d cbuf=1 existing_immediate=1 register=1 invalid=1 ps5=unverified")
