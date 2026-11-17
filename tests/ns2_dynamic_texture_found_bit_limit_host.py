#!/usr/bin/env python3
"""Host regression for Lichii 5e33a765 / #1914 adapted to Encore.

Compiles the exact DynamicDescriptorIndexLimit helper extracted from the selected
texture pass against a minimal IR model. This proves the accepted IR shapes and
keeps unrelated dynamic indices unbounded. No guest/game data is used.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
OWNER=ROOT/"encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp"
source=OWNER.read_text()
start="u32 DynamicDescriptorIndexLimit(const IR::U32& dynamic_offset, u32 size_shift) {"
stop="\n}\n\nu32 SaturatingSub("
assert source.count(start)==1 and source.count(stop)==1
helper=start+source.split(start,1)[1].split(stop,1)[0]+"\n}"
assert "Upstream Lichii 5e33a765 / #1914" in source
assert "DynamicDescriptorIndexLimit(dynamic_offset, size_shift)" in source
assert ".count = (std::min)(DynamicDescriptorCount" in source

fixture=r"""
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
using u32=std::uint32_t;

namespace IR {
enum class Opcode {
    ShiftLeftLogical32,
    BitwiseXor32,
    FindUMsb32,
    FindSMsb32,
    SelectU32,
    Other,
};
struct Inst;
struct Value {
    bool immediate{};
    u32 value{};
    const Inst* inst{};
    bool IsImmediate() const { return immediate; }
    u32 U32() const { assert(immediate); return value; }
    const Inst* InstRecursive() const { return inst; }
};
using U32=Value;
struct Inst {
    Opcode opcode{Opcode::Other};
    std::array<Value,3> args{};
    Opcode GetOpcode() const { return opcode; }
    const Value& Arg(std::size_t index) const { return args.at(index); }
};
static Value Imm(u32 v){ return Value{true,v,nullptr}; }
static Value Ref(const Inst& i){ return Value{false,0,&i}; }
}
__HELPER__

int main(){
    using namespace IR;
    Inst input{Opcode::Other,{}};
    Inst ums{Opcode::FindUMsb32,{Ref(input),Imm(0),Imm(0)}};
    Inst sms{Opcode::FindSMsb32,{Ref(input),Imm(0),Imm(0)}};
    Inst xor31{Opcode::BitwiseXor32,{Ref(ums),Imm(31),Imm(0)}};
    Inst xor30{Opcode::BitwiseXor32,{Ref(ums),Imm(30),Imm(0)}};
    Inst shift_ums{Opcode::ShiftLeftLogical32,{Ref(ums),Imm(3),Imm(0)}};
    Inst shift_sms{Opcode::ShiftLeftLogical32,{Ref(sms),Imm(3),Imm(0)}};
    Inst shift_xor{Opcode::ShiftLeftLogical32,{Ref(xor31),Imm(3),Imm(0)}};
    Inst shift_bad_xor{Opcode::ShiftLeftLogical32,{Ref(xor30),Imm(3),Imm(0)}};
    Inst select_both{Opcode::SelectU32,{Imm(1),Ref(ums),Ref(xor31)}};
    Inst select_one{Opcode::SelectU32,{Imm(1),Ref(ums),Ref(input)}};
    Inst shift_select{Opcode::ShiftLeftLogical32,{Ref(select_both),Imm(3),Imm(0)}};
    Inst shift_select_bad{Opcode::ShiftLeftLogical32,{Ref(select_one),Imm(3),Imm(0)}};
    Inst wrong_shift{Opcode::ShiftLeftLogical32,{Ref(ums),Imm(4),Imm(0)}};

    assert(DynamicDescriptorIndexLimit(Ref(shift_ums),3)==32U);
    assert(DynamicDescriptorIndexLimit(Ref(shift_sms),3)==32U);
    assert(DynamicDescriptorIndexLimit(Ref(shift_xor),3)==32U);
    assert(DynamicDescriptorIndexLimit(Ref(shift_select),3)==32U);
    assert(DynamicDescriptorIndexLimit(Ref(shift_bad_xor),3)==~0U);
    assert(DynamicDescriptorIndexLimit(Ref(shift_select_bad),3)==~0U);
    assert(DynamicDescriptorIndexLimit(Ref(wrong_shift),3)==~0U);
    assert(DynamicDescriptorIndexLimit(Ref(input),3)==~0U);
    assert(DynamicDescriptorIndexLimit(Imm(0),3)==~0U);
    std::cout<<"found_bit=32 unknown="<<~0U<<"\n";
}
""".replace("__HELPER__",helper)

compilers=list(dict.fromkeys(x for x in (shutil.which("clang++"),shutil.which("g++")) if x))
assert compilers
with tempfile.TemporaryDirectory(prefix="ns2-dynamic-texture-limit-") as td:
    src=Path(td)/"test.cpp";src.write_text(fixture)
    for cc in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(td)/(Path(cc).name.replace("+","p")+opt)
            cmd=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror","-pedantic",
                 "-fsanitize=undefined","-fno-sanitize-recover=all",
                 str(src),"-o",str(exe)]
            build=subprocess.run(cmd,text=True,capture_output=True,timeout=60,env=os.environ.copy())
            if build.returncode:
                raise RuntimeError(build.stdout+build.stderr)
            run=subprocess.run([str(exe)],text=True,capture_output=True,timeout=15)
            if run.returncode:
                raise RuntimeError(run.stdout+run.stderr)

print("NS2_DYNAMIC_TEXTURE_FOUND_BIT_LIMIT_HOST_PASS upstream=5e33a765 limit=32 select=1 xor31=1 unknown=unbounded ps5=unverified")
