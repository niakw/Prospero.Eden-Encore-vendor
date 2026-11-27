#!/usr/bin/env python3
"""R23 #72: 12 maintained shader/Vulkan owners + source-extracted SPIR-V rewrite."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
files={
  "encore-active/eden/src/shader_recompiler/CMakeLists.txt": "cc9ce95d9269d2dbd13dd6bbbf44c1afa0d87fb1",
  "encore-active/eden/src/shader_recompiler/backend/spirv/emit_spirv.cpp": "fd303bf04a3010916a190344cfb96bb46e64691b",
  "encore-active/eden/src/shader_recompiler/backend/spirv/emit_spirv_warp.cpp": {"8d835099614b9b185d36e2c76a2519ade6faa6f1", "69e1b7ab58b8a51cde0e11e28e62153be8e4822c"},
  "encore-active/eden/src/shader_recompiler/backend/spirv/spirv_emit_context.h": "844a118358c874b0666aebda116c1d6a0c9a8ceb",
  "encore-active/eden/src/shader_recompiler/frontend/maxwell/translate_program.cpp": "dd49db913a31d95a803aa6967f1b06bdba61777f",
  "encore-active/eden/src/shader_recompiler/host_translate_info.h": "af549e742edca3ce3e790ea5343136094a42b197",
  "encore-active/eden/src/shader_recompiler/ir_opt/constant_propagation_pass.cpp": {"6285fef16f2f500b36b1aa45d987f8774eb8131d", "ce074980d021e14c80b76b4acb1372791c106b5f", "9cbdcbffe4117f9bc7957a69d15e6a277fcfe12b"},
  "encore-active/eden/src/shader_recompiler/ir_opt/geometry_compaction_pass.cpp": "98cfa47401afcebbd122f4f8b9473a3142ddad62",
  "encore-active/eden/src/shader_recompiler/ir_opt/passes.h": "0c2b44e02249c0cfb06ccd3aa843adc0a12334e9",
  "encore-active/eden/src/shader_recompiler/profile.h": {"8e9c83f8885c18634bf02e68a167e6ce2ef33503", "0c1db4867e945b367bba01bd9de6fa42f47099a9"},
  "encore-active/eden/src/video_core/renderer_vulkan/vk_pipeline_cache_cost.cpp": "2e67b2a0d8dcba21ca28711c666dd352da624042",
  "encore-active/eden/src/video_core/vulkan_common/vulkan_device.h": "6c17ea15d036f2c4fb8e51f655e8a0cc33e4b1b8"
}
# The SPIR-V native owner was integrated after R23 #72 into the same
# shipping TU; its checked source SHA has advanced. Retain the actual
# RewriteOpcodes extraction and UBSan host oracle below; don't skip the file.
# No runtime shader source is changed by this test-only repin.
for path,expected in files.items():
 d=(root/path).read_bytes()
 actual=hashlib.sha1(b"blob "+str(len(d)).encode()+bytes([0])+d).hexdigest()
 if path.endswith("ir_opt/constant_propagation_pass.cpp") and actual=="9cbdcbffe4117f9bc7957a69d15e6a277fcfe12b":
  source=d.decode()
  new_hunk="            if (count == 0) {\n                return 0U;\n            }\n            const u32 mask{count == 32 ? ~0U : (1U << count) - 1};\n            return (base >> shift) & mask;\n"
  old_hunk="            return (base >> shift) & ((1U << count) - 1);\n"
  assert source.count(new_hunk)==1 and old_hunk not in source
  prior=source.replace(new_hunk,old_hunk,1).encode()
  assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()=="ce074980d021e14c80b76b4acb1372791c106b5f"
 if path.endswith("ir_opt/passes.h"):
  text=d.decode()
  assert "GeometryCompactionPass" in text and "HasBrokenPattern" in text,(path,"subgroup/AMD owners not combined")
 elif path.endswith("vk_pipeline_cache_cost.cpp"):
  text=d.decode()
  assert "single_lane_geometry_subgroups" in text and "HasBrokenPattern" in text,(path,"subgroup/AMD pipeline not combined")
 elif path.endswith("backend/spirv/emit_spirv.cpp"):
  old="Id TypeId(const EmitContext& ctx, IR::Type type) {\n    switch (type) {\n    case IR::Type::U1:\n        return ctx.U1;\n    case IR::Type::U32:\n        return ctx.U32[1];\n    default:\n        throw NotImplementedException(\"Phi node type {}\", type);\n    }\n}"
  new="Id TypeId(const EmitContext& ctx, IR::Type type) {\n    // Eglantine #492: use the already-declared scalar/vector type IDs.\n    // Optional widths may not have been declared for this shader/profile.\n    Id id{};\n    switch (type) {\n    case IR::Type::U1:\n        id = ctx.U1;\n        break;\n    case IR::Type::U8:\n        id = ctx.U8;\n        break;\n    case IR::Type::U16:\n        id = ctx.U16;\n        break;\n    case IR::Type::U32:\n        id = ctx.U32[1];\n        break;\n    case IR::Type::U64:\n        id = ctx.U64;\n        break;\n    case IR::Type::F16:\n        id = ctx.F16[1];\n        break;\n    case IR::Type::F32:\n        id = ctx.F32[1];\n        break;\n    case IR::Type::F64:\n        id = ctx.F64[1];\n        break;\n    case IR::Type::U32x2:\n        id = ctx.U32[2];\n        break;\n    case IR::Type::U32x3:\n        id = ctx.U32[3];\n        break;\n    case IR::Type::U32x4:\n        id = ctx.U32[4];\n        break;\n    case IR::Type::F16x2:\n        id = ctx.F16[2];\n        break;\n    case IR::Type::F16x3:\n        id = ctx.F16[3];\n        break;\n    case IR::Type::F16x4:\n        id = ctx.F16[4];\n        break;\n    case IR::Type::F32x2:\n        id = ctx.F32[2];\n        break;\n    case IR::Type::F32x3:\n        id = ctx.F32[3];\n        break;\n    case IR::Type::F32x4:\n        id = ctx.F32[4];\n        break;\n    case IR::Type::F64x2:\n        id = ctx.F64[2];\n        break;\n    case IR::Type::F64x3:\n        id = ctx.F64[3];\n        break;\n    case IR::Type::F64x4:\n        id = ctx.F64[4];\n        break;\n    default:\n        break;\n    }\n    if (!Sirit::ValidId(id)) {\n        throw NotImplementedException(\"Phi node type {}\", type);\n    }\n    return id;\n}"
  if new in d.decode():
   assert d.decode().count(new)==1
   prior=d.decode().replace(new,old,1).encode()
   assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()==expected
  else:
   assert actual==expected
 elif path.endswith("frontend/maxwell/translate_program.cpp"):
  # Preserve the exact historic subgroup source after removing only the
  # reviewed emulated geometry mask delta (Lichii #1466).
  s=d.decode()
  delta="                // Lichii #1466: lowering forwards Position and the generic vec4s.\n                // Do not declare unrelated built-in inputs in an emulated geometry stage.\n                for (size_t i = 0; i < program.info.passthrough.mask.size(); ++i) {\n                    if (i < static_cast<size_t>(IR::Attribute::PositionX) ||\n                        i > static_cast<size_t>(IR::Attribute::Generic31W)) {\n                        program.info.passthrough.mask[i] = false;\n                    }\n                }\n"
  if delta in s:
   assert s.count(delta)==1
   prior=s.replace(delta,"",1).encode()
   assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()==expected
  else:
   assert actual==expected
 elif path.endswith("spirv_emit_context.h"):
  if actual=="1d4046906b39e929b66af460c8a19e29805ed165":
   selected=d.decode()
   new="    Id input_sample_mask{};\n"
   assert selected.count(new)==1
   predecessor=selected.replace(new,"",1).encode()
   digest=hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()
   assert digest=="844a118358c874b0666aebda116c1d6a0c9a8ceb", ("SPIR-V mesh/sample-mask ABI reverse", digest)
  else:
   assert actual=="844a118358c874b0666aebda116c1d6a0c9a8ceb"
  source=d.decode()
  for symbol in ("MeshInfo mesh_info", "DefineMesh(const IR::Program& program)",
                 "input_tess_level_outer", "input_tess_level_inner",
                 "mesh_payload", "mesh_primitive_targets", "mesh_tasks_marker"):
   assert symbol in source,(path,"missing native mesh/tessellation ABI",symbol)
 elif path.endswith("vulkan_common/vulkan_device.h"):
  header=d.decode()
  assert "GetMaxSubgroupSize() const" in header and "IsExtDepthClipEnableSupported()" in header,(path,"missing subgroup or EDS3 API")
 else:
  assert (actual in expected) if isinstance(expected, set) else (actual == expected), (path, actual, expected)
shader=root/"encore-active/eden/src/shader_recompiler"
vk=root/"encore-active/eden/src/video_core/renderer_vulkan"
assert "GeometryCompactionPass(program, normalized_host_info);" in (shader/"frontend/maxwell/translate_program.cpp").read_text()
assert "TightenOutputVertices(program);" in (shader/"frontend/maxwell/translate_program.cpp").read_text()
assert "single_lane_geometry_subgroups" in (shader/"host_translate_info.h").read_text()
profile_source=(shader/"profile.h").read_text()
warp_source=(shader/"backend/spirv/emit_spirv_warp.cpp").read_text()
pipeline_source=(vk/"vk_pipeline_cache_cost.cpp").read_text()
assert "has_broken_spirv_subgroup_shuffle" in profile_source
if "has_broken_spirv_fragment_shuffle" in profile_source:
    assert "bool UseShuffleFallback(EmitContext& ctx)" in warp_source
    assert "fragment_shuffle_fallback[index] = true;" in pipeline_source
    assert "EmitSPIRV(emit_profile, runtime_info, program, binding)" in pipeline_source
assert "VK_DRIVER_ID_MESA_TURNIP" in pipeline_source
assert "device.GetMaxSubgroupSize()" in (vk/"vk_pipeline_cache_cost.cpp").read_text()
assert "GetMaxSubgroupSize() const" in (vk/"include/video_core/vulkan_common/vulkan_device.h").read_text()
emit=(shader/"backend/spirv/emit_spirv.cpp").read_text()
start="void RewriteOpcodes(std::vector<u32>& code, std::span<const std::pair<u32, spv::Op>> rewrites) {"
end="\n} // Anonymous namespace"
# RewriteOpcodes now precedes another native Task/Mesh helper. Use
# the immediate function closing brace, not the anonymous-namespace end.
boundary="\n}\n// Sirit 1.0.5 does not expose the SPV_EXT_mesh_shader"
assert emit.count(start)==1 and emit.count(boundary)==1
method=start+emit.split(start,1)[1].split(boundary,1)[0]+"\n}"
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

subprocess.run(["python3","-B",str(root/"tests/ps5_spirv_phi_types_host.py")],check=True,timeout=120)
