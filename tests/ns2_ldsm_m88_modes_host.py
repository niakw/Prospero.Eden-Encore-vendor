#!/usr/bin/env python3
"""Qualify the isolated LDSM.16.M88.{1,2,4} Ampere->Maxwell count selector."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"encore-active/ns2-runtime/ampere/shader_recompiler/frontend/ampere/lower_to_maxwell.h").read_text()
assert "Lichii e6e0304303ba" in source
assert "mode == 0 ? 1U : mode == 0x100 ? 2U : 4U" in source
assert "(mode != 0 && mode != 0x100 && mode != 0x200)" in source
code=r"""
#include <cassert>
#include <string_view>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
static Instruction ldsm(unsigned mode, unsigned dest=16, unsigned address=8) {
    Instruction insn{};
    insn.words[0] = 0x83bU | (7U << 12) | (dest << 16) | (address << 24);
    insn.words[2] = mode;
    return insn;
}
static Instruction exit_insn() {
    Instruction insn{};
    insn.words[0] = 0x94dU | (7U << 12);
    insn.words[2] = 7U << 23; // unconditional EXIT condition in bits 87..90
    return insn;
}
template <std::size_t N>
static auto with_exit(const std::array<Instruction,N>& body) {
    std::array<Instruction,N+1> code{};
    for(std::size_t i=0;i<N;++i) code[i]=body[i];
    code[N]=exit_insn();
    return code;
}
int main() {
    const auto one=LowerToMaxwell(with_exit(std::array{ldsm(0x000,17)}));
    const auto two=LowerToMaxwell(with_exit(std::array{ldsm(0x100,16)}));
    const auto four=LowerToMaxwell(with_exit(std::array{ldsm(0x200,16)}));
    std::cerr << "LDSM one ok=" << bool(one) << " error=" << one.error
              << " out=" << one.instructions.size() << std::endl
              << "LDSM two ok=" << bool(two) << " error=" << two.error
              << " out=" << two.instructions.size() << std::endl
              << "LDSM four ok=" << bool(four) << " error=" << four.error
              << " out=" << four.instructions.size() << std::endl;
    assert(one && two && four);
    assert(one.source_size==2*sizeof(Instruction));
    assert(two.source_size==2*sizeof(Instruction));
    assert(four.source_size==2*sizeof(Instruction));
    assert(!one.instructions.empty());
    assert(two.instructions.size()>one.instructions.size());
    assert(four.instructions.size()>two.instructions.size());
    const auto invalid=LowerToMaxwell(with_exit(std::array{ldsm(0x300,16)}));
    assert(!invalid && invalid.error=="native shared matrix load mode");
    const auto misaligned_two=LowerToMaxwell(with_exit(std::array{ldsm(0x100,17)}));
    const auto misaligned_four=LowerToMaxwell(with_exit(std::array{ldsm(0x200,18)}));
    assert(!misaligned_two && !misaligned_four);
}
"""
with tempfile.TemporaryDirectory(prefix="ns2-ldsm-m88-") as td:
    p=Path(td)
    (p/"t.cpp").write_text(code)
    compiler=os.getenv("CXX","c++")
    compile = subprocess.run(
        [compiler,"-std=c++20","-O2","-Wall","-Wextra","-Werror","-pedantic",
         "-I",str(root/"encore-active/ns2-runtime/ampere"),
         "-I",str(root/"sources/eden/src"),
         str(p/"t.cpp"),"-o",str(p/"test")],
        text=True,capture_output=True,timeout=90)
    if compile.returncode:
        raise RuntimeError("LDSM harness compile failed:\n" + compile.stdout + compile.stderr)
    run = subprocess.run([str(p/"test")],text=True,capture_output=True,timeout=30)
    if run.returncode:
        raise RuntimeError("LDSM harness execution failed:\n" + run.stdout + run.stderr)
print("NS2_LDSM_M88_MODES_HOST_PASS one=1 two=1 four=1 invalid=1 alignment=1 ps5=unverified")
