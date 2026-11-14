#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host proof for the focused scalar-constant cache port from Lichii #1320."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
h=ROOT/"encore-active/eden-shader/src/shader_recompiler/backend/spirv/spirv_emit_context.h"
src=h.read_text()
for needle,count in [
    ("#include <bit>",1),
    ('#include "common/container/unordered_map.h"',1),
    ("CachedConstant(u32_constants, U32[1], value)",1),
    ("CachedConstant(f32_constants, F32[1], value)",1),
    ("std::bit_cast<u32>(value)",1),
    ("::Common::unordered_map<u32, Id> u32_constants;",1),
    ("::Common::unordered_map<u32, Id> f32_constants;",1),
]:
    assert src.count(needle)==count,(needle,src.count(needle))
begin="    template <typename T>\n    Id CachedConstant("
start=src.index(begin)
end=src.index("\n\n    // Keep integer and float namespaces distinct",start)
method=src[start:end]
# The executable uses the exact method text with only the repository-specific
# map namespace/type aliases supplied by a tiny mock EmitContext.
cpp=r'''
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <unordered_map>
using u32=std::uint32_t;
struct Id { u32 value{}; friend bool operator==(Id,Id)=default; };
namespace Common { template<class K,class V> using unordered_map=std::unordered_map<K,V>; }
struct Mock {
    u32 calls{};
    template<class T> Id Constant(Id, T) { return Id{++calls}; }
'''+method.replace("    ","",1)+r'''
    Common::unordered_map<u32,Id> u32_constants;
    Common::unordered_map<u32,Id> f32_constants;
};
int main(){
    Mock m;
    Id t{7};
    auto a=m.CachedConstant(m.u32_constants,t,u32{5});
    auto b=m.CachedConstant(m.u32_constants,t,u32{5});
    assert(a==b && m.calls==1);
    float pz=std::bit_cast<float>(u32{0x00000000});
    float nz=std::bit_cast<float>(u32{0x80000000});
    float n1=std::bit_cast<float>(u32{0x7fc00000});
    float n2=std::bit_cast<float>(u32{0x7fc00001});
    auto fp=m.CachedConstant(m.f32_constants,t,pz);
    auto fn=m.CachedConstant(m.f32_constants,t,nz);
    auto f1=m.CachedConstant(m.f32_constants,t,n1);
    auto f2=m.CachedConstant(m.f32_constants,t,n2);
    assert(!(fp==fn) && !(f1==f2));
    auto ui=m.CachedConstant(m.u32_constants,t,u32{0x80000000});
    assert(!(ui==fn));
    auto again=m.CachedConstant(m.f32_constants,t,n2);
    assert(again==f2 && m.calls==6);
}
'''
with tempfile.TemporaryDirectory(prefix="scalar-constant-cache-") as td:
    p=Path(td)/"test.cpp"; p.write_text(cpp)
    out=Path(td)/"test"
    subprocess.run(["clang++","-std=c++20","-O2","-Wall","-Wextra","-Werror",
                    "-fsanitize=undefined","-fno-sanitize-recover=all",str(p),"-o",str(out)],
                   check=True,timeout=60)
    subprocess.run([str(out)],check=True,timeout=20)
print("SPIRV_SCALAR_CONSTANT_CACHE_HOST_PASS bit_patterns=1 separate_types=1 exact_method=1 full_emit_spirv=unverified")
