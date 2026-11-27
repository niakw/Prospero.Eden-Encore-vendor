#!/usr/bin/env python3
"""Qualify Lichii #851 / cdeb7ace on Encore's real Ampere lowerer.

Covers the two generic instruction forms observed in The Duskbloods network
test without embedding game shader bytes:
- TLD4.B (0x364) with a register handle;
- IMAD (0x624) with a negated constant-buffer addend.

The harness also keeps unsupported modifiers fail-closed. Host-only; no PS5
runtime claim follows from this test.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / "encore-active/eden/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h"
source = OWNER.read_text()

for needle in (
    "case 0x364:",
    "native bindless texture gather mode",
    "Lichii #851 / cdeb7ace",
    "const bool negated_cbuf_addend = opcode == 0x624 && insn.Bits(63, 1);",
    "(opcode == 0x624 && insn.Bits(62, 1))",
):
    assert needle in source, needle

code = r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

static Instruction Exit() {
    Instruction insn{};
    insn.words[0] = 0x94dU | (7U << 12);
    insn.words[2] = 7U << 23;
    return insn;
}

static auto Program(Instruction body) {
    return std::array{body, Exit()};
}

static Instruction RegisterTld4() {
    Instruction insn{};
    // TLD4.B R10/R11, coordinates R20/R21, register handle R30.
    // 2D, LZ, two selected components, no offset/depth/F16/uniform mode.
    insn.words[0] = 0x364U | (7U << 12) | (10U << 16) | (20U << 24);
    insn.words[1] = 30U | (1U << 27) | (1U << 28) | (1U << 29);
    insn.words[2] = 255U | (3U << 8) | (7U << 17);
    return insn;
}

static Instruction CbufImad(bool negate) {
    Instruction insn{};
    // IMAD R4, R2, R3, cbuf[1][0], with optional -cbuf addend.
    insn.words[0] = 0x624U | (7U << 12) | (4U << 16) | (2U << 24);
    insn.words[1] = (1U << 22) | (negate ? (1U << 31) : 0U);
    insn.words[2] = 0x078e0000U | 3U;
    return insn;
}

int main() {
    const auto gather = LowerToMaxwell(Program(RegisterTld4()));
    assert(gather);
    assert(gather.source_size == 2 * sizeof(Instruction));
    bool saw_tld4 = false;
    for (const std::uint64_t word : gather.instructions) {
        if ((word & 0xffc0000000000000ULL) == 0xdec0000000000000ULL)
            saw_tld4 = true;
    }
    assert(saw_tld4);

    auto bad_dimension = RegisterTld4();
    bad_dimension.words[1] &= ~(7U << 29);
    bad_dimension.words[1] |= 2U << 29;
    const auto rejected_dimension = LowerToMaxwell(Program(bad_dimension));
    assert(!rejected_dimension);
    assert(rejected_dimension.error == "native bindless texture gather mode");

    auto bad_depth = RegisterTld4();
    bad_depth.words[2] |= 1U << 25; // native bit 89
    const auto rejected_depth = LowerToMaxwell(Program(bad_depth));
    assert(!rejected_depth);
    assert(rejected_depth.error == "native bindless texture gather mode");

    const auto positive = LowerToMaxwell(Program(CbufImad(false)));
    const auto negative = LowerToMaxwell(Program(CbufImad(true)));
    assert(positive);
    assert(negative);
    assert(negative.instructions.size() == positive.instructions.size() + 1);

    bool saw_negated_cbuf = false;
    for (const std::uint64_t word : negative.instructions) {
        if ((word >> 56) == 0x5c &&
            (word & 0xffU) == 252U &&
            ((word >> 8) & 0xffU) == 255U &&
            ((word >> 48) & 1U) == 1U) {
            saw_negated_cbuf = true;
        }
    }
    assert(saw_negated_cbuf);

    auto absolute = CbufImad(false);
    absolute.words[1] |= 1U << 30; // native bit 62
    const auto rejected_absolute = LowerToMaxwell(Program(absolute));
    assert(!rejected_absolute);
    assert(rejected_absolute.error == "native multiply wide addend mode");

    std::cout << "gather=" << gather.instructions.size()
              << " imad_pos=" << positive.instructions.size()
              << " imad_neg=" << negative.instructions.size() << '\n';
}
"""

with tempfile.TemporaryDirectory(prefix="ns2-duskbloods-851-") as td:
    work = Path(td)
    src = work / "test.cpp"
    src.write_text(code)
    compiler = os.getenv("CXX", "c++")
    exe = work / "test"
    cmd = [
        compiler,
        "-std=c++20",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-fsanitize=undefined",
        "-fno-sanitize-recover=all",
        "-I",
        str(ROOT / "encore-active/ns2-runtime/ampere"),
        "-I",
        str(ROOT / "sources/eden/src"),
        str(src),
        "-o",
        str(exe),
    ]
    build = subprocess.run(cmd, text=True, capture_output=True, timeout=90)
    if build.returncode:
        raise RuntimeError("Duskbloods #851 harness compile failed:\n" + build.stdout + build.stderr)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=30)
    if run.returncode:
        raise RuntimeError("Duskbloods #851 harness failed:\n" + run.stdout + run.stderr)

print(
    "NS2_DUSKBLOODS_TLD4_IMAD_HOST_PASS",
    "upstream=cdeb7ace",
    "tld4_register_handle=1",
    "imad_neg_cbuf=1",
    "invalid_modes=3",
    "ps5=unverified",
)
