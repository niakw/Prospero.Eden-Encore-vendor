#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the selected exact cache helper with real pinned Sirit, not a mock module."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
cpm = ROOT / "sources/eden-cpm"
sirit = cpm / "sirit/v1.0.5"
shader = ROOT / "encore-active/eden-shader/src/shader_recompiler/backend/spirv/spirv_emit_context.h"
src = shader.read_text()
start = src.index("    template <typename T>\n    Id CachedConstant(")
end = src.index("\n\n    // Keep integer and float namespaces distinct", start)
helper = src[start:end]
assert src.count("CachedConstant(u32_constants, U32[1], value)") == 1
assert src.count("CachedConstant(f32_constants, F32[1], value)") == 1
code = r"""
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
#include <sirit/sirit.h>
#include "common/container/unordered_map.h"
using u32 = std::uint32_t;
using Sirit::Id;
struct Cached : Sirit::Module {
    using Sirit::Module::Module;
""" + helper + r"""
    Common::unordered_map<u32, Id> u32_constants, f32_constants;
};
template<class M> std::vector<u32> emit(M& m, u32 seed) {
    m.AddCapability(spv::Capability::Shader);
    m.SetMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
    const Id u = m.TypeInt(32, false), f = m.TypeFloat(32);
    const Id v = m.TypeVoid(), ft = m.TypeFunction(v);
    std::vector<u32> corpus{0, 0x80000000u, 0x7fc00000u, 0x7fc00001u,
                           0x7f800000u, 0xff800000u, 1, 0xffffffffu, 0x3f800000u};
    std::mt19937 rng(seed);
    for (unsigned i=0; i<256; ++i) corpus.push_back(static_cast<u32>(rng()));
    std::vector<std::array<Id,2>> ids;
    for (int repeat=0; repeat<8; ++repeat) {
        for (size_t i=0;i<corpus.size();++i) {
            const u32 bits=corpus[i];
            Id iu{}, iff{};
            if constexpr (requires { m.u32_constants; }) {
                iu=m.CachedConstant(m.u32_constants,u,bits);
                iff=m.CachedConstant(m.f32_constants,f,std::bit_cast<float>(bits));
            } else {
                iu=m.Constant(u,bits);
                iff=m.Constant(f,std::bit_cast<float>(bits));
            }
            assert(iu.value != iff.value);
            if (repeat==0) ids.push_back({iu,iff});
            else assert(iu.value==ids[i][0].value && iff.value==ids[i][1].value);
        }
    }
    // Independently test exact returned IDs, using the original creation order.
    for (size_t i=0;i<corpus.size();++i) {
        const u32 bits=corpus[i];
        if constexpr (requires { m.u32_constants; }) {
            assert(m.CachedConstant(m.u32_constants,u,bits).value==ids[i][0].value);
            assert(m.CachedConstant(m.f32_constants,f,std::bit_cast<float>(bits)).value==ids[i][1].value);
        }
    }
    assert(ids[0][1].value != ids[1][1].value && ids[2][1].value != ids[3][1].value);
    Id fn = m.OpFunction(v,spv::FunctionControlMask::MaskNone,ft);
    m.AddLabel();
    m.OpReturn();
    m.OpFunctionEnd();
    m.AddEntryPoint(spv::ExecutionModel::GLCompute,fn,"main");
    m.AddExecutionMode(fn,spv::ExecutionMode::LocalSize,u32{1},u32{1},u32{1});
    return m.Assemble();
}
int main() {
    for (u32 seed : {0u,1u,42u,65535u,0xdeadbeefu}) {
        Sirit::Module baseline(0x00010300);
        Cached cached(0x00010300);
        auto original=emit(baseline,seed);
        auto optimized=emit(cached,seed);
        assert(original==optimized);
    }
    std::puts("SPIRV_SCALAR_REAL_SIRIT_HOST_PASS modules=5 byte_identical=1 actual_sirit=1 actual_map=1 complete_guest_shader_corpus=unverified");
}
"""
# The exact dependency is compiled directly instead of enabling its upstream
# tests or relying on a system-installed Sirit with different behavior.
units = [sirit/"src"/name for name in ["sirit.cpp","instructions/type.cpp","instructions/constant.cpp","instructions/function.cpp","instructions/flow.cpp","instructions/debug.cpp","instructions/derivatives.cpp","instructions/memory.cpp","instructions/annotation.cpp","instructions/misc.cpp","instructions/logical.cpp","instructions/conversion.cpp","instructions/bit.cpp","instructions/arithmetic.cpp","instructions/extension.cpp","instructions/image.cpp","instructions/group.cpp","instructions/barrier.cpp","instructions/atomic.cpp"]]
assert all(p.is_file() for p in units)
boost = cpm/"boost/boost-1.90.0/libs"
includes=[ROOT/"sources/eden/src",sirit/"include",sirit/"src",
          cpm/"spirv-headers/vulkan-sdk-1.4.341.0/include", *sorted(boost.glob("*/include"))]
assert all(p.is_dir() for p in includes)
compiler=shutil.which("clang++")
assert compiler
with tempfile.TemporaryDirectory(prefix="real-sirit-") as td:
    srcfile=Path(td)/"check.cpp"; srcfile.write_text(code)
    out=Path(td)/"check"
    subprocess.run([compiler,"-std=c++20","-O1",*[f"-I{p}" for p in includes],str(srcfile),
                    *map(str,units),"-o",str(out)],check=True,timeout=240)
    subprocess.run([str(out)],check=True,timeout=30)
