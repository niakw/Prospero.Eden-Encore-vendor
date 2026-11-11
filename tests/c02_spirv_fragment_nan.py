#!/usr/bin/env python3
"""C02 source-extracted NaN fragment-color guard contract.

Host/mock/ASan only; SPIR-V validator and PS5 device rendering remain open.
"""
from pathlib import Path
import hashlib
import subprocess
import os
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILE = ROOT / "encore-active/ns2-runtime/ampere/shader_recompiler/backend/spirv/emit_spirv_context_get_set.cpp"
source = FILE.read_text()
old = """    if (type == AttributeType::Float) {
        const Id pointer{ctx.OpAccessChain(ctx.output_f32, ctx.frag_color.at(index), component_id)};
        ctx.OpStore(pointer, value);
    } else if (type == AttributeType::UnsignedInt)"""
new = """    if (type == AttributeType::Float) {
        const Id pointer{ctx.OpAccessChain(ctx.output_f32, ctx.frag_color.at(index), component_id)};
        ctx.OpStore(pointer, ctx.OpSelect(ctx.F32[1], ctx.OpIsNan(ctx.U1, value), ctx.Const(0.0f), value));
    } else if (type == AttributeType::UnsignedInt)"""
assert source.count(new) == 1
recovered = source.replace(new, old, 1).encode()
sha = hashlib.sha1(b"blob " + str(len(recovered)).encode() + bytes([0]) + recovered).hexdigest()
assert sha == "f768235d3a6667f18ee5900311364c7d9a7853ba", sha
expr = "ctx.OpSelect(ctx.F32[1], ctx.OpIsNan(ctx.U1, value), ctx.Const(0.0f), value)"
assert source.count(expr) == 1
assert 'ctx.OpStore(pointer, ctx.OpBitcast(ctx.U32[1], value));' in source
assert 'ctx.OpStore(pointer, ctx.OpBitcast(ctx.S32[1], value));' in source
print("C02_EXACT_SHARED_SPIRV_OWNER_SOURCE_PASS")

fixture = """
#include <cassert>
#include <cmath>
#include <limits>
struct MockContext {
    int F32[2] = {0, 32};
    int U1 = 1;
    float stored = -1;
    bool OpIsNan(int, float x) { return std::isnan(x); }
    float Const(float x) { return x; }
    float OpSelect(int, bool cond, float yes, float no) { return cond ? yes : no; }
    void OpStore(float*, float x) { stored=x; }
};
int main() {
    MockContext ctx;
    float out = -1;
    float* pointer=&out;
    for (float value : {0.0f, 1.25f, -20.5f, std::numeric_limits<float>::quiet_NaN()}) {
        ctx.OpStore(pointer, """ + expr + """);
        assert(!std::isnan(ctx.stored));
        assert(ctx.stored == (std::isnan(value) ? 0.0f : value));
    }
}
"""
fixture=fixture.replace("#include <limits>", "#include <limits>\n#include <initializer_list>")
with tempfile.TemporaryDirectory(prefix="c02-spv-") as td:
    cpp=Path(td)/"test.cpp"
    exe=Path(td)/"test"
    cpp.write_text(fixture)
    subprocess.run([os.environ.get("CXX","clang++"), "-std=c++20", "-O1",
                    "-fsanitize=address,undefined", str(cpp), "-o", str(exe)],
                   check=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=30,
                   env={**os.environ, "ASAN_OPTIONS": "halt_on_error=1"})
print("C02_FRAGMENT_NAN_SOURCE_EXPRESSION_HOST_PASS finite_and_nan=4 ps5_gpu_not_tested=1")
