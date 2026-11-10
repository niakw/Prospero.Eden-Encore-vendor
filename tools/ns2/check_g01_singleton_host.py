#!/usr/bin/env python3
"""G01 negative: compile/execute actual maintained singleton descriptor guard.

The SPIR-V emitter previously discarded non-immediate singleton indices and
loaded texture element zero regardless of guest runtime value. Refuse that
unsupported form; do NOT change the valid dynamic array path (count > 1).
Host evidence only; this is not a live shader or a PS5 proof.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
src=ROOT/"encore-active/ns2-runtime/ampere/shader_recompiler/backend/spirv/emit_spirv_image.cpp"
s=src.read_text()
begin="void CheckSingletonTextureIndex("
# Test only the singleton owner. The following native range-proof
# helper has its own host and full real-SPIR-V validators.
end="[[nodiscard]] std::optional<u32> NativeBoundedImageIndexMaximum("
assert s.count(begin)==s.count(end)==1
code=begin+s.split(begin,1)[1].split(end,1)[0]
assert "Dynamic singleton texture descriptor index unsupported" in code
assert "if (!index.IsImmediate())" in code
assert "if (index.U32() != 0)" in code
assert s.count("CheckedTextureArrayIndex(ctx, index, def.count)") == 5
fixture=r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
using u32=std::uint32_t;
namespace Shader::Backend::SPIRV {
struct LogicError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
namespace IR {
enum class Type {U32,U64};
struct Value {
    bool immediate{},empty{};
    IR::Type type{IR::Type::U32};
    u32 word{};
    bool IsEmpty()const{return empty;}
    bool IsImmediate()const{return immediate;}
    auto Type()const->IR::Type{return type;}
    u32 U32()const{assert(immediate&&type==IR::Type::U32);return word;}
};
}
__OWNER__
}
int main(){
    using namespace Shader::Backend::SPIRV;
    auto imm=[](u32 n){return IR::Value{true,false,IR::Type::U32,n};};
    auto dyn=[](u32 n){return IR::Value{false,false,IR::Type::U32,n};};
    CheckSingletonTextureIndex(imm(0),1);
    CheckSingletonTextureIndex(IR::Value{false,true,IR::Type::U32,0},1);
    auto reject=[](const IR::Value& value,u32 count){
        bool refused=false;
        try{CheckSingletonTextureIndex(value,count);}
        catch(const LogicError&){refused=true;}
        assert(refused);
    };
    reject(imm(0),0);
    reject(imm(1),1);
    reject(dyn(0),1); // even unknown zero must not bind a fabricated element
    reject(dyn(33),1);
    reject(IR::Value{true,false,IR::Type::U64,0},1);
    reject(IR::Value{false,false,IR::Type::U64,0},1);
    std::puts("G01_VENDOR_SINGLETON_GUARD_PASS"
              " immediate_zero=accepted missing_descriptor=refused"
              " immediate_oob=refused dynamic_unknown=refused"
              " invalid_type=refused");
}
""".replace("__OWNER__",code)
compilers=list(dict.fromkeys(x for x in
    (shutil.which("clang++"),shutil.which("g++")) if x))
assert compilers
with tempfile.TemporaryDirectory(prefix="eden-g01-vendor-") as tmp:
    source=Path(tmp)/"owner.cpp"
    source.write_text(fixture)
    for cc in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(tmp)/(Path(cc).name.replace("+","p")+opt)
            argv=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                  "-fsanitize=undefined","-fno-sanitize-recover=all",
                  str(source),"-o",str(exe)]
            compiled=subprocess.run(argv,capture_output=True,text=True,
                                    timeout=60,env=os.environ.copy())
            assert compiled.returncode==0,(cc,compiled.stderr[-5000:])
            tested=subprocess.run([str(exe)],capture_output=True,text=True,
                                  timeout=15,env=os.environ.copy())
            assert tested.returncode==0,(cc,tested.stderr[-5000:])
            print(Path(cc).name,opt,tested.stdout.strip(),flush=True)
