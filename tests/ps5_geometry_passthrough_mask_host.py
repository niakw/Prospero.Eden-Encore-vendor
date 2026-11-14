#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Source-extracted emulated geometry mask regression; no GPU execution."""
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "encore-active/eden-shader/src/shader_recompiler/frontend/maxwell/translate_program.cpp").read_text()
anchor = "            if (!normalized_host_info.support_geometry_shader_passthrough) {\n"
delta = "                // Lichii #1466: lowering forwards Position and the generic vec4s.\n                // Do not declare unrelated built-in inputs in an emulated geometry stage.\n                for (size_t i = 0; i < program.info.passthrough.mask.size(); ++i) {\n                    if (i < static_cast<size_t>(IR::Attribute::PositionX) ||\n                        i > static_cast<size_t>(IR::Attribute::Generic31W)) {\n                        program.info.passthrough.mask[i] = false;\n                    }\n                }\n"
assert source.count(anchor + delta) == 1
old = source.replace(anchor + delta, anchor, 1).encode()
assert hashlib.sha1(b"blob " + str(len(old)).encode() + bytes([0]) + old).hexdigest() == "dd49db913a31d95a803aa6967f1b06bdba61777f"
# Compile exactly the selected mask initialization + emulated lowering branch.
start = "            const auto& mask{env.GpPassthroughMask()};"
end = "                LowerGeometryPassthrough(program, normalized_host_info);\n            }"
assert source.count(start) == source.count(end) == 1
body = source[source.index(start):source.index(end) + len(end)]
for header in ("sources/eden/src/shader_recompiler/frontend/ir/attribute.h",
               "encore-active/ns2-runtime/shader_resource/include/shader_recompiler/frontend/ir/attribute.h"):
    enum_entries = re.findall(r"SRIR_ATTRIBUTE_ELEM\((\w+),\s*(\d+)\)", (root / header).read_text())
    assert len(enum_entries) > 128
    enum = ",".join(name+"="+n for name,n in enum_entries)
    cpp = r"""
#include <array>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <random>
#include <cstddef>
using std::size_t;
namespace IR { enum class Attribute : uint64_t { """+enum+r""" }; }
struct Program { struct Info { struct Pass { std::bitset<512> mask; } passthrough; } info;
    unsigned output_vertices=99, output_topology=3, lower_count=0; };
struct Host { bool support_geometry_shader_passthrough; };
struct Env { std::array<uint32_t,8> mask; const auto& GpPassthroughMask() const { return mask; } };
unsigned GetOutputTopologyVertices(unsigned x){return x;}
void LowerGeometryPassthrough(Program& p,const Host&){++p.lower_count;}
Program translate(Env env, bool hardware) {
    Program program;
    Host normalized_host_info{hardware};
""" + body + r"""
    return program;
}
int main(){
    static_assert(static_cast<size_t>(IR::Attribute::PositionX)==28);
    static_assert(static_cast<size_t>(IR::Attribute::Generic31W)==159);
    std::mt19937 rng(1446);
    for(int k=0;k<1026;++k){
        Env env{};
        for(auto& word:env.mask)word=k==0?0u:k==1?~0u:rng();
        auto native=translate(env,true), lower=translate(env,false);
        assert(native.lower_count==0 && native.output_vertices==99);
        assert(lower.lower_count==1 && lower.output_vertices==3);
        for(size_t i=0;i<512;++i){
            bool original=i<256 && ((env.mask[i/32]>>(i%32))&1u)==0;
            assert(native.info.passthrough.mask[i]==original);
            assert(lower.info.passthrough.mask[i]==(original&&i>=28&&i<=159));
        }
    }
}
"""
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "mask.cpp"
        src.write_text(cpp)
        for opt in ("-O0", "-O2"):
            exe = Path(d) / "mask"
            subprocess.run(["c++", "-std=c++20", opt, "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-fno-sanitize-recover=all",
                            str(src), "-o", str(exe)], check=True, timeout=60)
            subprocess.run([str(exe)], check=True, timeout=10)
    print("GEOMETRY_PASSTHROUGH_MASK_PASS", header, "cases=1026 native_unchanged=1")
print("GEOMETRY_PASSTHROUGH_MASK_HOST_ONLY no SPIR-V module or PS5 GPU execution")
