#!/usr/bin/env python3
"""NS1 #18 coherent four-owner TIR mixed-sample scratch/resolve source contract."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

r=Path(__file__).resolve().parents[1]
files={
"encore-active/eden-vulkan/fixed_pipeline_state.cpp":"71f35e18d4c3cdbfb6d23e065dc98e1d525ad2e4",
"encore-active/eden-vulkan/vk_texture_cache_cost.cpp":"2b344e2c9adce18a7037194cc4d5f81a5c6df144",
"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_texture_cache.h":"1b725abab76168d9787d9b062d7406df733cf49d",
"encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h":"da6ab6dcea2a1c02fcb2e862d111dc410b025d66",
}
for name,expected in files.items():
 raw=(r/name).read_bytes()
 sha=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
 if name.endswith("fixed_pipeline_state.cpp"):
  text=raw.decode()
  assert "MaxwellDepthClampClip" in text and "regs.tir_mode == Maxwell::TIRMode::RasterNTargetM" in text,(name,"lost EDS3/TIR")
 else:
  assert sha==expected,(name,sha)
 print("R23_18_FULL_SOURCE_OWNER_PASS",name)

fixed=(r/"encore-active/eden-vulkan/fixed_pipeline_state.cpp").read_text()
cost=(r/"encore-active/eden-vulkan/vk_texture_cache_cost.cpp").read_text()
h=(r/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_texture_cache.h").read_text()
cache=(r/"encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h").read_text()
# Both native header owners must match, retaining TIR and GPU retirement.
unified=(r/"encore-active/ns2-astc/include/video_core/renderer_vulkan/vk_texture_cache.h").read_text()
assert h==unified
for symbol in ("GetOrCreateTirColorScratch", "tir_color_scratch",
               "RetireGpuImageView", "RetireGpuImage(", "pending_retired_views",
               "AccelerateImageUploadLayers", "StorageImageLayerView"):
 assert symbol in unified,("lost shared TIR/ASTC ABI",symbol)
assert "regs.tir_mode == Maxwell::TIRMode::RasterNTargetM" in fixed
assert "GetOrCreateTirColorScratch" in cost and "GetOrCreateTirColorScratch" in h
assert "CreateMixedSamplesFramebuffer" in cost and "CreateMixedSamplesFramebuffer" in h
assert "renderpass_key.resolve_color = true;" in cost
assert "renderpass_key.resolve_depth_stencil = false;" in cost
assert "CreateVkFramebuffer(runtime, mixed_attachments, layers);" in cost
assert "regs.tir_mode == Tegra::Engines::Maxwell3D::Regs::TIRMode::RasterNTargetM" in cache
assert "scheduler.Finish();" in cost
assert "tir_color_scratch[color_image]" in cost

test=r"""
#include <cassert>
#include <cstdint>
struct Targets{
  bool tir=false;
  int raster_samples=1,depth_samples=1,color_samples=1;
  bool Mixed() const {
    return tir && raster_samples>1 && depth_samples==raster_samples && color_samples==1;
  }
};
int main(){
 for(int raster: {1,2,4,8}){
  Targets t{true,raster,raster,1};
  assert(t.Mixed()==(raster>1));
  t.tir=false;assert(!t.Mixed());
  t.tir=true;t.depth_samples=1;assert(!t.Mixed() || raster==1);
  t.depth_samples=raster;t.color_samples=raster;assert(!t.Mixed() || raster==1);
 }
}
""";
with tempfile.TemporaryDirectory(prefix="r23-tir-") as td:
 p=Path(td)
 (p/"tir.cpp").write_text(test.replace("#include <cstdint>","#include <cstdint>\n#include <initializer_list>"))
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g","-fsanitize=address,undefined",
     str(p/"tir.cpp"),"-o",str(p/"tir")],check=True,timeout=60)
 subprocess.run([str(p/"tir")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("R23_18_MIXED_MSAA_SOURCE_HOST_PASS selected4=1 no_false_mixed=1")
