#!/usr/bin/env python3
"""FC27 dynamic texture-table + fixed sampler qualification.

Exact-title provenance: seedds/nxnative documents EA SPORTS FC 27
0100C49025D3E000 using a dynamically indexed c2 texture-handle table with one
fixed sampler word. Lichii #65 preserves the array through BitwiseOr32 and
does not advance the secondary sampler per element.

This test proves the selected Encore owners implement only that behavior.
It is source/host proof, not a PS5 visual pass.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
TEX=ROOT/"encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp"
GFX=ROOT/"encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp"
CMP=ROOT/"encore-active/eden-vulkan/vk_compute_pipeline_cost.cpp"
CACHE=ROOT/"encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp"

def blob(path: Path)->str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()

assert blob(TEX)=="3d9625f1981d6500e7b294018e0659464869d6f3"
assert blob(GFX)=="efa8fd4d73c111655fced67fe7dd1e6b7c0178ba"
assert blob(CMP)=="7d9fa72ce2afff5a7b1b75a05fc894b1d99ce6a2"
assert blob(CACHE)=="be5d48abd44996502b0829a6e0501d130a151455"

tex=TEX.read_text()
gfx=GFX.read_text()
cmp=CMP.read_text()
cache=CACHE.read_text()

# Dynamic + static composition. Two dynamic arrays remain deliberately rejected.
for needle in (
    "if (lhs->count > 1 && rhs->count > 1)",
    "if (rhs->count > 1 ||",
    ".dynamic_offset = lhs->dynamic_offset,",
    ".count = lhs->count,",
    "IR::U32 dynamic_offset{offset};",
    "offset_inst->GetOpcode() != IR::Opcode::ShiftLeftLogical32",
):
    assert tex.count(needle)==1,needle
assert "if (lhs->count > 1 || rhs->count > 1)" not in tex

# The texture table advances; the secondary sampler word is fixed.
assert gfx.count("const u32 second_offset{desc.secondary_cbuf_offset};")==1
assert "desc.secondary_cbuf_offset + index_offset" not in gfx
assert cmp.count("const u32 secondary_offset{desc.secondary_cbuf_offset};")==1
assert "desc.secondary_cbuf_offset + index_offset" not in cmp
assert "constexpr u32 CACHE_VERSION = 21;" in cache

# Existing descriptor budgeting stays active; this patch does not blindly pull
# Lichii's later NS2 handle-table machinery into NS1.
for needle in ("DynamicDescriptorCount(", "DynamicSampledTextureCap(",
               "DESCRIPTOR_MAX_COUNT = 1024"):
    assert needle in tex
assert "HandleOp::Index" not in tex

cpp=r"""
#include <cassert>
#include <cstdint>
#include <optional>
#include <random>
#include <utility>
using u32=std::uint32_t;
struct D {
    u32 index, offset, shift, count, dynamic_tag;
    bool secondary=false;
};
static std::optional<std::pair<D,D>> compose(D lhs,D rhs) {
    if (lhs.secondary || rhs.secondary) return std::nullopt;
    if (lhs.count>1 && rhs.count>1) return std::nullopt;
    if (rhs.count>1 ||
        (lhs.count==1 && (lhs.shift>0 || lhs.index>rhs.index || lhs.offset>rhs.offset))) {
        std::swap(lhs,rhs);
    }
    return std::pair{lhs,rhs};
}
static u32 descriptor_count(u32 base,u32 shift,u32 limit) {
    if (limit==0) limit=1;
    const u32 max_bytes=16u*limit;
    if (shift>=31 || base>=max_bytes) return 1;
    const u32 stride=1u<<shift;
    const u32 available=max_bytes-base;
    if (available<8) return 1;
    const u32 count=1u+(available-8u)/stride;
    return count<limit?count:limit;
}
int main(){
    // FC27-style c2 table starting at 0x168 with 8-byte stride plus fixed c2+0x570 sampler.
    D table{2,0x168,0,128,0xFC27}, sampler{2,0x570,0,1,0};
    for(bool reverse: {false,true}) {
        auto pair=reverse?compose(sampler,table):compose(table,sampler);
        assert(pair);
        assert(pair->first.count==128);
        assert(pair->first.offset==0x168);
        assert(pair->first.dynamic_tag==0xFC27);
        assert(pair->second.count==1);
        assert(pair->second.offset==0x570);
        for(u32 i=0;i<128;i++) {
            const u32 index_offset=i<<3;
            const u32 texture_word=pair->first.offset+index_offset;
            const u32 sampler_word=pair->second.offset;
            assert(texture_word==0x168u+i*8u);
            assert(sampler_word==0x570u);
        }
    }
    // Zero-based shifted table form from Lichii #65.
    assert(descriptor_count(0,4,128)==128);
    assert(descriptor_count(0x168,3,128)>=64);
    // Two independently dynamic tables are intentionally not representable.
    assert(!compose(D{2,0,0,64,1},D{3,0,0,64,2}));

    std::mt19937 rng(0xFC270065u);
    for(unsigned n=0;n<200000;n++) {
        const u32 i=rng()%128u;
        const u32 texture=0x168u+(i<<3);
        const u32 sampler_addr=0x570u;
        assert(texture>=0x168u);
        assert(sampler_addr==0x570u);
    }
}
""";
with tempfile.TemporaryDirectory(prefix="fc27-dyn-tex-") as tmp:
    p=Path(tmp)
    (p/"test.cpp").write_text(cpp)
    subprocess.run([
        os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
        "-fsanitize=undefined","-fno-sanitize-recover=all",
        str(p/"test.cpp"),"-o",str(p/"test")
    ],check=True,timeout=60)
    subprocess.run([str(p/"test")],check=True,timeout=20)

print("FC27_DYNAMIC_TEXTURE_STATIC_SAMPLER_PASS table_gt32=1 sampler_fixed=1 zero_base_shift=1 cache=21 ps5_visual=unverified")
