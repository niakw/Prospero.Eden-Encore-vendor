#!/usr/bin/env python3
"""Regression for Lichii #1825 / 0378cd27: BitFieldUExtract constant-fold UB.

The maintained shader owner must not shift a 32-bit value by 32 for
(count=0, shift=32) or build (1U << 32) for the full-width field.
Host/UBSan only; no game or PS5 runtime claim.
"""
from pathlib import Path
import os, shutil, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"encore-active/eden-shader/src/shader_recompiler/ir_opt/constant_propagation_pass.cpp"
s=SRC.read_text()
assert "if (count == 0)" in s
assert "const u32 mask{count == 32 ? ~0U : (1U << count) - 1};" in s
assert "return (base >> shift) & mask;" in s
assert "return (base >> shift) & ((1U << count) - 1);" not in s

cpp=r"""
#include <cassert>
#include <cstdint>
#include <limits>
using u32=std::uint32_t;
static u32 fold(u32 base,u32 shift,u32 count){
    if (static_cast<std::size_t>(shift)+static_cast<std::size_t>(count)>32) return 0xdeadbeefu;
    if (count==0) return 0U;
    const u32 mask{count==32 ? ~0U : (1U<<count)-1};
    return (base>>shift)&mask;
}
int main(){
    assert(fold(0x12345678u,32,0)==0u);
    assert(fold(0x89abcdefu,0,32)==0x89abcdefu);
    assert(fold(0xf0f0a5a5u,4,8)==0x5au);
    assert(fold(0xffffffffu,31,1)==1u);
}
"""
compilers=list(dict.fromkeys(p for n in ("clang++","g++") if (p:=shutil.which(n))))
assert compilers
with tempfile.TemporaryDirectory(prefix="bitfield-uextract-") as td:
    src=Path(td)/"t.cpp"; src.write_text(cpp)
    for cxx in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(td)/(Path(cxx).name+opt)
            cmd=[cxx,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                 "-fsanitize=undefined","-fno-sanitize-recover=undefined",
                 str(src),"-o",str(exe)]
            b=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
            assert b.returncode==0,(cxx,opt,b.stderr)
            x=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
            assert x.returncode==0,(cxx,opt,x.stdout,x.stderr)
            print("BITFIELD_UEXTRACT_UB_PASS",Path(cxx).name,opt)
