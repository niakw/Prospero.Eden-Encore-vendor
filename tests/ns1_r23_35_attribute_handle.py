#!/usr/bin/env python3
"""NS1 R23 #35: 8 maintained source owner receipts and real draw-run method ASan."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
EXPECTED={
  "encore-active/eden-shader/src/shader_recompiler/host_translate_info.h": "af549e742edca3ce3e790ea5343136094a42b197",
  "encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp": "85c523e435ee8803ef3d2087567aa1a4b5d09369",
  "encore-active/eden-shader/src/shader_recompiler/shader_info.h": "d847e7485a9657a6a016354fd70fb423112857ff",
  "encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp": "d7563d41b74d780f4512899e2fda1b2f362f5523",
  "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_graphics_pipeline.h": "d49090a476f6a9bf61026331f59c9fbb1d918729",
  "encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp": "417b9b953bae2dcae511e4166821881f5c443aa1",
  "encore-active/eden-vulkan/vulkan_rasterizer.cpp": "2530e3754435175810cbf24cb20446e177d1f19f",
  "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_rasterizer.h": "cb35d183c8a9c2ea05f5eb8949a167ea295f8ce7"
}
def gitblob(p):
 b=p.read_bytes()
 return hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
for path,sha in EXPECTED.items():
 assert gitblob(ROOT/path)==sha,(path,gitblob(ROOT/path),sha)
shader=(ROOT/"encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp").read_text()
profile=(ROOT/"encore-active/eden-shader/src/shader_recompiler/host_translate_info.h").read_text()
pipeline=(ROOT/"encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp").read_text()
rast=(ROOT/"encore-active/eden-vulkan/vulkan_rasterizer.cpp").read_text()
header=(ROOT/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_rasterizer.h").read_text()
assert shader.count("IsAttributeSourcedHandle(")>=2
assert shader.count("ATTRIBUTE_HANDLE_CBUF_INDEX")>=2
assert "support_attribute_texture_handle{}" in profile
assert "single_lane_geometry_subgroups{}" in profile, "lost subgroup support"
assert "ResolveConditionalPhi(operand, block)" in shader, "lost conditional Phi shader fix"
assert "MaxwellDepthClampClip(" in rast, "lost EDS3 depth clamp"
assert "desc.cbuf_index == Shader::ATTRIBUTE_HANDLE_CBUF_INDEX" in pipeline
assert "attribute_handle_runs.size() > 1" in rast
assert "CanExecuteRecordedDraw()" in rast and "DrawMeshTasksEXT" in rast
assert "struct AttributeHandleRun" in header
start="void RasterizerVulkan::CollectAttributeHandleRuns(bool is_indexed) {"
end="\nvoid RasterizerVulkan::Draw(bool is_indexed, u32 instance_count) {"
assert rast.count(start)==1 and rast.count(end)==1
method=start+rast.split(start,1)[1].split(end,1)[0]
test=r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
using u32=std::uint32_t;
using GPUVAddr=std::uint64_t;
namespace Tegra::Engines {
struct Maxwell3D {
 struct Regs {
  enum class PrimitiveTopology {Triangles, Lines};
  struct VertexAttribute {
   enum class Type { UInt, SInt, Float};
   enum class Size {Invalid, X32};
   struct Buf {u32 i=0;u32 Value() const{return i;}};
   bool constant=true;
   Size size=Size::Invalid;
   Type type=Type::Float;
   Buf buffer{};
   u32 offset=0;
  };
  struct VertexStream {
   bool enabled=false;
   GPUVAddr address=1000;
   u32 stride=4;
   bool IsEnabled() const{return enabled;}
   GPUVAddr Address() const{return address;}
  };
  std::array<VertexAttribute,16> vertex_attrib_format{};
  std::array<VertexStream,16> vertex_streams{};
 } regs;
 struct DrawManager {
  struct DrawState {
   struct Index {
    u32 count=0;
    GPUVAddr IndexStart() const{return 3000;}
    size_t FormatSizeInBytes() const{return 4;}
   } index_buffer;
   struct Vert {u32 count=0, first=0;} vertex_buffer;
   Regs::PrimitiveTopology topology=Regs::PrimitiveTopology::Triangles;
   u32 base_index=0;
  } draw_state;
 } draw_manager;
};
}
namespace Vulkan {
using Maxwell=Tegra::Engines::Maxwell3D::Regs;
struct Memory {
 std::map<GPUVAddr,u32> values;
 template<typename T> T Read(GPUVAddr address) const {
  const auto it=values.find(address);
  return it==values.end()? T{}:static_cast<T>(it->second);
 }
 void ReadBlockUnsafe(GPUVAddr address,void* dest,size_t n) const {
  const u32 value=Read<u32>(address);
  std::memcpy(dest,&value,n);
 }
};
struct RasterizerVulkan {
 struct AttributeHandleRun {u32 first{},count{},handle{};};
 std::vector<AttributeHandleRun> attribute_handle_runs;
 Tegra::Engines::Maxwell3D* maxwell3d{};
 Memory* gpu_memory{};
 void CollectAttributeHandleRuns(bool);
};
""" + method + r"""
}
int main(){
 Tegra::Engines::Maxwell3D engine{};
 Vulkan::Memory mem{};
 Vulkan::RasterizerVulkan raster{};
 raster.maxwell3d=&engine;
 raster.gpu_memory=&mem;
 auto& attr=engine.regs.vertex_attrib_format[0];
 attr.constant=false;attr.size=Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size::X32;attr.type=Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type::UInt;
 auto& stream=engine.regs.vertex_streams[0];stream.enabled=true;
 engine.draw_manager.draw_state.vertex_buffer.count=9;
 for(u32 i=0;i<9;i++) mem.values[1000+i*4]=i<3?11:(i<6?22:11);
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.size()==3);
 assert(raster.attribute_handle_runs[0].first==0 && raster.attribute_handle_runs[0].count==3 &&
        raster.attribute_handle_runs[0].handle==11);
 assert(raster.attribute_handle_runs[1].first==3 && raster.attribute_handle_runs[1].count==3 &&
        raster.attribute_handle_runs[1].handle==22);
 assert(raster.attribute_handle_runs[2].first==6 && raster.attribute_handle_runs[2].count==3 &&
        raster.attribute_handle_runs[2].handle==11);
 raster.attribute_handle_runs.clear();
 engine.draw_manager.draw_state.index_buffer.count=9;
 for(u32 i=0;i<9;i++)mem.values[3000+i*4]=8-i;
 raster.CollectAttributeHandleRuns(true);
 assert(raster.attribute_handle_runs.size()==3);
 assert(raster.attribute_handle_runs[0].handle==11);
 assert(raster.attribute_handle_runs[1].handle==22);
 assert(raster.attribute_handle_runs[2].handle==11);
 raster.attribute_handle_runs.clear();
 engine.draw_manager.draw_state.vertex_buffer.count=0;
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.empty());
 raster.attribute_handle_runs.clear();
 engine.draw_manager.draw_state.vertex_buffer.count=9;
 attr.constant=true;
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.size()==1);
 assert(raster.attribute_handle_runs[0].count==9);
 raster.attribute_handle_runs.clear();
 attr.constant=false;
 engine.draw_manager.draw_state.topology=Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology::Lines;
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.size()==1);
 assert(raster.attribute_handle_runs[0].count==9);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-attribute-handle-") as td:
 d=Path(td)
 (d/"run.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
                 "-fsanitize=address,undefined","-fno-omit-frame-pointer",
                 str(d/"run.cpp"),"-o",str(d/"run")],check=True,timeout=70)
 subprocess.run([str(d/"run")],check=True,timeout=25,
                env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1:detect_leaks=1"})
print("R23_35_VERTEX_ATTRIBUTE_TEXTURE_RUNS_PASS sources=8 paths=6")
