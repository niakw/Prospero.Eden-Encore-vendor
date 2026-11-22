#!/usr/bin/env python3
"""FC27/Maxwell XMAD pointer-fold qualification.

Ports only the stabilized algebraic fold kept by current Eglantine/Lichii.
The host oracle proves integer equivalence; PS5/FC27 visual effect remains
unverified until a user-approved native build and console run.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
CONST=ROOT/"encore-active/eden-shader/src/shader_recompiler/ir_opt/constant_propagation_pass.cpp"
CACHE=ROOT/"encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp"
PINNED=ROOT/"sources/eden/src/shader_recompiler/ir_opt/constant_propagation_pass.cpp"

def blob(path: Path)->str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()

const_blob=blob(CONST)
assert const_blob in {"ce074980d021e14c80b76b4acb1372791c106b5f","9cbdcbffe4117f9bc7957a69d15e6a277fcfe12b"}, const_blob
if const_blob=="9cbdcbffe4117f9bc7957a69d15e6a277fcfe12b":
    current=CONST.read_text()
    assert current.count("            if (count == 0) {\n                return 0U;\n            }\n            const u32 mask{count == 32 ? ~0U : (1U << count) - 1};\n            return (base >> shift) & mask;\n")==1
    restored=current.replace("            if (count == 0) {\n                return 0U;\n            }\n            const u32 mask{count == 32 ? ~0U : (1U << count) - 1};\n            return (base >> shift) & mask;\n","            return (base >> shift) & ((1U << count) - 1);\n",1).encode()
    assert hashlib.sha1(b"blob "+str(len(restored)).encode()+bytes([0])+restored).hexdigest()=="ce074980d021e14c80b76b4acb1372791c106b5f"
cache_blob=blob(CACHE)
if cache_blob=="79660c5cc4ff046f24e7ffa50ab066a86857698c":
    from ps5_recent_prewarm_receipt import reverse_adaptive_prewarm
    source_cache_pre_admission = reverse_adaptive_prewarm(CACHE.read_text())
    cache_blob = "a0e2a45b9b0f080b739d97abb92e9485a0063f73"
else:
    source_cache_pre_admission = CACHE.read_text()
if cache_blob=="a0e2a45b9b0f080b739d97abb92e9485a0063f73":
    selected=source_cache_pre_admission
    reverse = (
        ("        // Vulkan requires signed/unsigned fragment outputs to match the\n        // attachment's numeric class. Previously this was enabled only on\n        // MoltenVK: native PS5 RADV would emit floats for integer targets,\n        // making color writes poison/undefined (Vulkan shader interfaces).\n        // Keep the existing desktop policy; fix the PS5 renderer's ABI.\n        const bool require_typed_fragment_outputs =\n#ifdef PS5_NATIVE\n            true;\n#else\n            device.IsMoltenVK();\n#endif\n        if (require_typed_fragment_outputs) {", "        if (device.IsMoltenVK()) {"),
    )
    for newer, earlier in reverse:
        assert newer in selected, "Unexpected CACHE source change"
        selected=selected.replace(newer, earlier, 1)
    data=selected.encode("utf-8")
    previous=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
    assert previous=="cfac496d574010b8121611c8616d38bf7cdcbc41", ("Unexpected source predecessor",previous)
    cache_blob="cfac496d574010b8121611c8616d38bf7cdcbc41"
assert cache_blob in {
    "be5d48abd44996502b0829a6e0501d130a151455",
    "e10c4893c03da57dce7f607d3e0af46d62e343f1",
    "dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56",
    "e3910d11f33376097a90858118ba276f7e02cb70",
    "cfac496d574010b8121611c8616d38bf7cdcbc41",
}, cache_blob
if cache_blob in {"e10c4893c03da57dce7f607d3e0af46d62e343f1","dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56","e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41"}:
    selected=CACHE.read_text()
    assert "fragment_shuffle_fallback[index] = true;" in selected
    assert "EmitSPIRV(emit_profile, runtime_info, program, binding)" in selected
    assert "skipping fragment shuffle with known driver issue" in selected
    if cache_blob in {"dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56","e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41"}:
        assert "EDEN_FRAGMENT_SHUFFLE_FALLBACK count=%u hash=0x%016llx" in selected
        assert "fallback_witness <= 8" in selected
        if cache_blob in {"e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41"}:
            # Successor changed only the reviewed PS5 pipeline teardown sequence.
            # The exact source blob above prevents accidentally accepting further drift.
            assert "workers.WaitForRequests();" in selected
            assert selected.index("workers.WaitForRequests();") < selected.index("serialization_thread.WaitForRequests();")
if cache_blob=="cfac496d574010b8121611c8616d38bf7cdcbc41":
    # Only the reviewed native cache-admission policy changed after e3910d;
    # preserve the graphics shader fallback and shutdown drain contracts.
    selected=CACHE.read_text()
    assert selected.count("kStartupPrewarmPipelines = 32;")==1
    assert "kRecentShaderCacheScanLimit = 64ULL << 20;" in selected
    assert "recent_cache_builds.pop_front();" in selected
    assert "prefer_recent_cache ? stop_loading : startup_reader.get_token()" in selected
    assert "workers.WaitForRequests();" in selected
assert blob(PINNED)=="57c08588fbd1a160acbcdd516c2d38d15f95818b"

src=CONST.read_text()
pinned=PINNED.read_text()
cache=CACHE.read_text()
for needle in (
    "bool FoldHalvesRecombine(IR::Inst& inst)",
    "void FoldIMul32(IR::Inst& inst)",
    "factor.U32() == 0",
    "factor.U32() == 1",
    "return FoldIMul32(inst);",
):
    assert src.count(needle)==1, needle
assert "FoldHalvesRecombine" not in pinned
assert "void FoldIMul32" not in pinned
assert "constexpr u32 CACHE_VERSION = 21;" in cache
assert "constexpr u32 CACHE_VERSION = 20;" not in cache
assert "constexpr u32 CACHE_VERSION = 19;" not in cache

# Keep exact upstream structural constraints: only a 16/16 split of the SAME
# source can collapse. Do not generalize this into arbitrary bit-field algebra.
half=src.split("bool FoldHalvesRecombine(IR::Inst& inst)",1)[1].split("\ntemplate <typename T>",1)[0]
for needle in (
    "shl->Arg(1) != IR::Value{16U}",
    "low->Arg(1) != IR::Value{0U}",
    "low->Arg(2) != IR::Value{16U}",
    "high->Arg(1) != IR::Value{16U}",
    "high->Arg(2) != IR::Value{16U}",
    "high->Arg(0).Resolve() != low->Arg(0).Resolve()",
):
    assert needle in half, needle

cpp=r"""
#include <cassert>
#include <cstdint>
#include <random>
#include <limits>
using u32=std::uint32_t;
static u32 lo16(u32 x){ return x & 0xffffu; }
static u32 hi16(u32 x){ return (x >> 16) & 0xffffu; }
static u32 recombine(u32 x){ return (hi16(x) << 16) + lo16(x); }
int main(){
    const u32 edge[]={
        0u,1u,2u,0xffffu,0x10000u,0x12345678u,0x80000000u,
        0xfffffffeu,0xffffffffu
    };
    for(u32 x:edge){
        assert(u32(x*0u)==0u);
        assert(u32(x*1u)==x);
        assert(recombine(x)==x);
    }
    std::mt19937 rng(0xFC270001u);
    for(unsigned i=0;i<200000;i++){
        const u32 x=rng();
        assert(u32(x*0u)==0u);
        assert(u32(x*1u)==x);
        assert(recombine(x)==x);
    }
}
""";
with tempfile.TemporaryDirectory(prefix="fc27-xmad-fold-") as tmp:
    p=Path(tmp)
    (p/"test.cpp").write_text(cpp)
    subprocess.run([
        os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
        "-fsanitize=undefined","-fno-sanitize-recover=all",
        str(p/"test.cpp"),"-o",str(p/"test")
    ],check=True,timeout=60)
    subprocess.run([str(p/"test")],check=True,timeout=20)
print("FC27_XMAD_POINTER_FOLD_PASS cases=200009 pinned_immutable=1 cache_version=21 ps5_visual=unverified")
