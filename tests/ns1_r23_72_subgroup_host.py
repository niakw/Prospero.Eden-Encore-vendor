#!/usr/bin/env python3
"""R23 #72: 12 maintained shader/Vulkan owners + source-extracted SPIR-V rewrite."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
files={
  "encore-active/eden-shader/src/shader_recompiler/CMakeLists.txt": "cc9ce95d9269d2dbd13dd6bbbf44c1afa0d87fb1",
  "encore-active/eden-shader/src/shader_recompiler/backend/spirv/emit_spirv.cpp": "a903480c782f577bede2480c58037619ecb5371a",
  "encore-active/eden-shader/src/shader_recompiler/backend/spirv/emit_spirv_warp.cpp": "8d835099614b9b185d36e2c76a2519ade6faa6f1",
  "encore-active/eden-shader/src/shader_recompiler/backend/spirv/spirv_emit_context.h": "7e43b58a4ad9da0f8efc800e3963a1a3978813ca",
  "encore-active/eden-shader/src/shader_recompiler/frontend/maxwell/translate_program.cpp": "dd49db913a31d95a803aa6967f1b06bdba61777f",
  "encore-active/eden-shader/src/shader_recompiler/host_translate_info.h": "44df85ccb2a98c64c98dab41f54783bd5970b19c",
  "encore-active/eden-shader/src/shader_recompiler/ir_opt/constant_propagation_pass.cpp": "6285fef16f2f500b36b1aa45d987f8774eb8131d",
  "encore-active/eden-shader/src/shader_recompiler/ir_opt/geometry_compaction_pass.cpp": "98cfa47401afcebbd122f4f8b9473a3142ddad62",
  "encore-active/eden-shader/src/shader_recompiler/ir_opt/passes.h": "0c2b44e02249c0cfb06ccd3aa843adc0a12334e9",
  "encore-active/eden-shader/src/shader_recompiler/profile.h": "8e9c83f8885c18634bf02e68a167e6ce2ef33503",
  "encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp": "2e67b2a0d8dcba21ca28711c666dd352da624042",
  "encore-active/eden-vulkan/include/video_core/vulkan_common/vulkan_device.h": "6c17ea15d036f2c4fb8e51f655e8a0cc33e4b1b8"
}
for path,expected in files.items():
 d=(root/path).read_bytes()
 actual=hashlib.sha1(b"blob "+str(len(d)).encode()+bytes([0])+d).hexdigest()
 if path.endswith("ir_opt/passes.h"):
  text=d.decode()
  assert "GeometryCompactionPass" in text and "HasBrokenPattern" in text,(path,"subgroup/AMD owners not combined")
 elif path.endswith("vk_pipeline_cache_cost.cpp"):
  text=d.decode()
  assert "single_lane_geometry_subgroups" in text and "HasBrokenPattern" in text,(path,"subgroup/AMD pipeline not combined")
 else:
  assert actual==expected,(path,actual)
shader=root/"encore-active/eden-shader/src/shader_recompiler"
vk=root/"encore-active/eden-vulkan"
assert "GeometryCompactionPass(program, normalized_host_info);" in (shader/"frontend/maxwell/translate_program.cpp").read_text()
assert "TightenOutputVertices(program);" in (shader/"frontend/maxwell/translate_program.cpp").read_text()
assert "single_lane_geometry_subgroups" in (shader/"host_translate_info.h").read_text()
assert "has_broken_spirv_subgroup_shuffle" in (shader/"profile.h").read_text()
assert "VK_DRIVER_ID_MESA_TURNIP" in (vk/"vk_pipeline_cache_cost.cpp").read_text()
assert "device.GetMaxSubgroupSize()" in (vk/"vk_pipeline_cache_cost.cpp").read_text()
assert "GetMaxSubgroupSize() const" in (vk/"include/video_core/vulkan_common/vulkan_device.h").read_text()
emit=(shader/"backend/spirv/emit_spirv.cpp").read_text()
start="void RewriteOpcodes(std::vector<u32>& code, std::span<const std::pair<u32, spv::Op>> rewrites) {"
end="\n} // Anonymous namespace"
assert emit.count(start)==1 and emit.count(end)==1
method=start+emit.split(start,1)[1].split(end,1)[0]
test=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <ranges>
#include <span>
#include <utility>
#include <vector>
using u32=std::uint32_t;
namespace spv {
enum class Op:u32 {
 OpGroupNonUniformShuffleXor=1,
 OpGroupNonUniformQuadBroadcast=2,
 OpGroupNonUniformShuffleUp=3,
 OpGroupNonUniformShuffleDown=4,
 OpGroupNonUniformQuadSwap=5
};
}
""" + method + r"""
int main(){
 using namespace spv;
 std::vector<u32> shuffle={0,0,0,0,0,(4u<<16)|1u,0u,42u,0u};
 std::array<std::pair<u32,Op>,1> replacements{{{42u,Op::OpGroupNonUniformShuffleUp}}};
 RewriteOpcodes(shuffle,replacements);
 assert((shuffle[5]&0xFFFFu)==static_cast<u32>(Op::OpGroupNonUniformShuffleUp));
 std::vector<u32> quad={0,0,0,0,0,(4u<<16)|2u,0u,77u,0u};
 std::array<std::pair<u32,Op>,1> qreplacements{{{77u,Op::OpGroupNonUniformQuadSwap}}};
 RewriteOpcodes(quad,qreplacements);
 assert((quad[5]&0xFFFFu)==static_cast<u32>(Op::OpGroupNonUniformQuadSwap));
 std::vector<u32> untouched={0,0,0,0,0,(4u<<16)|1u,0u,99u,0u};
 RewriteOpcodes(untouched,replacements);
 assert((untouched[5]&0xFFFFu)==1u);
 std::vector<u32> zero={0,0,0,0,0,0u,0u,42u,0u};
 RewriteOpcodes(zero,replacements);
 assert(zero[5]==0u);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-subgroup-") as tmp:
 p=Path(tmp)
 (p/"test.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
    "-fsanitize=address,undefined","-fno-omit-frame-pointer",
    str(p/"test.cpp"),"-o",str(p/"test")],check=True,timeout=65)
 subprocess.run([str(p/"test")],check=True,timeout=20,
    env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_72_SUBGROUP_12_SOURCE_HOST_PASS geometry=1 spirv_opcode_rewrite=4")
