#!/usr/bin/env python3
"""Exact production TypeId function host witness; no console or GPU execution."""
from pathlib import Path
import hashlib
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"encore-active/eden-shader/src/shader_recompiler/backend/spirv/emit_spirv.cpp").read_text()
old="Id TypeId(const EmitContext& ctx, IR::Type type) {\n    switch (type) {\n    case IR::Type::U1:\n        return ctx.U1;\n    case IR::Type::U32:\n        return ctx.U32[1];\n    default:\n        throw NotImplementedException(\"Phi node type {}\", type);\n    }\n}"
new="Id TypeId(const EmitContext& ctx, IR::Type type) {\n    // Eglantine #492: use the already-declared scalar/vector type IDs.\n    // Optional widths may not have been declared for this shader/profile.\n    Id id{};\n    switch (type) {\n    case IR::Type::U1:\n        id = ctx.U1;\n        break;\n    case IR::Type::U8:\n        id = ctx.U8;\n        break;\n    case IR::Type::U16:\n        id = ctx.U16;\n        break;\n    case IR::Type::U32:\n        id = ctx.U32[1];\n        break;\n    case IR::Type::U64:\n        id = ctx.U64;\n        break;\n    case IR::Type::F16:\n        id = ctx.F16[1];\n        break;\n    case IR::Type::F32:\n        id = ctx.F32[1];\n        break;\n    case IR::Type::F64:\n        id = ctx.F64[1];\n        break;\n    case IR::Type::U32x2:\n        id = ctx.U32[2];\n        break;\n    case IR::Type::U32x3:\n        id = ctx.U32[3];\n        break;\n    case IR::Type::U32x4:\n        id = ctx.U32[4];\n        break;\n    case IR::Type::F16x2:\n        id = ctx.F16[2];\n        break;\n    case IR::Type::F16x3:\n        id = ctx.F16[3];\n        break;\n    case IR::Type::F16x4:\n        id = ctx.F16[4];\n        break;\n    case IR::Type::F32x2:\n        id = ctx.F32[2];\n        break;\n    case IR::Type::F32x3:\n        id = ctx.F32[3];\n        break;\n    case IR::Type::F32x4:\n        id = ctx.F32[4];\n        break;\n    case IR::Type::F64x2:\n        id = ctx.F64[2];\n        break;\n    case IR::Type::F64x3:\n        id = ctx.F64[3];\n        break;\n    case IR::Type::F64x4:\n        id = ctx.F64[4];\n        break;\n    default:\n        break;\n    }\n    if (!Sirit::ValidId(id)) {\n        throw NotImplementedException(\"Phi node type {}\", type);\n    }\n    return id;\n}"
assert source.count(new)==1
prior=source.replace(new,old,1).encode()
assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()=="fd303bf04a3010916a190344cfb96bb46e64691b"
header=(root/"sources/eden/src/shader_recompiler/frontend/ir/type.h").read_text()
enum=re.search(r"enum class Type \{(.*?)\};",header,re.S).group(1)
cases=[["U1","U1"],["U8","U8"],["U16","U16"],["U32","U32[1]"],["U64","U64"],["F16","F16[1]"],["F32","F32[1]"],["F64","F64[1]"],["U32x2","U32[2]"],["U32x3","U32[3]"],["U32x4","U32[4]"],["F16x2","F16[2]"],["F16x3","F16[3]"],["F16x4","F16[4]"],["F32x2","F32[2]"],["F32x3","F32[3]"],["F32x4","F32[4]"],["F64x2","F64[2]"],["F64x3","F64[3]"],["F64x4","F64[4]"]]
cpp=r"""
#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>
namespace IR {enum class Type {""" +enum+r""" };}
using Id=unsigned;
namespace Sirit {bool ValidId(Id id){return id!=0;}}
struct NotImplementedException:std::runtime_error {
    NotImplementedException(const char* s,IR::Type):std::runtime_error(s){}
};
struct VectorTypes {
    std::array<Id,4> defs{};
    Id operator[](size_t n) const {return defs.at(n-1);}
};
struct EmitContext {
    Id U1=1,U8=2,U16=3,U64=4;
    VectorTypes U32{{10,11,12,13}},F32{{20,21,22,23}},F16{{30,31,32,33}},F64{{40,41,42,43}};
};
""" +new+r"""
bool reject(const EmitContext& ctx,IR::Type type){
    try{(void)TypeId(ctx,type);return false;}catch(const NotImplementedException&){return true;}
}
int main(){
    EmitContext ctx;
"""+"\n".join("    assert(TypeId(ctx, IR::Type::"+k+")==ctx."+v+");" for k,v in cases)+r"""
    assert(reject(ctx,IR::Type::Void)&&reject(ctx,IR::Type::Opaque)&&reject(ctx,IR::Type::Reg));
    assert(reject(ctx,static_cast<IR::Type>(0x7fffffff)));
    ctx.U8=ctx.U16=ctx.U64=0;ctx.F16={};ctx.F64={};
    for(auto k:{IR::Type::U8,IR::Type::U16,IR::Type::U64,IR::Type::F16,IR::Type::F16x2,
                IR::Type::F16x3,IR::Type::F16x4,IR::Type::F64,IR::Type::F64x2,
                IR::Type::F64x3,IR::Type::F64x4})assert(reject(ctx,k));
    assert(TypeId(ctx,IR::Type::F32x4)==23&&TypeId(ctx,IR::Type::U32)==10);
}
"""
with tempfile.TemporaryDirectory() as d:
    d=Path(d); (d/"phi.cpp").write_text(cpp)
    for opt in ("-O0","-O2"):
        subprocess.run(["c++","-std=c++20",opt,"-Wall","-Wextra","-Werror",
                        "-fsanitize=undefined","-fno-sanitize-recover=all",
                        str(d/"phi.cpp"),"-o",str(d/"test")],check=True,timeout=60)
        subprocess.run([str(d/"test")],check=True,timeout=10)
print("PS5_SPIRV_PHI_TYPES_HOST_PASS supported=20 missing_optional=11 invalid=4 real_TypeId=1 GPU_unverified")
