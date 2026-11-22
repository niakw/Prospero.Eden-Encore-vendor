#!/usr/bin/env python3
"""R23 #2: exact 7 active Vulkan source owners, native PS5 depth clip truth table."""
from pathlib import Path
from native_msaa_handle_receipt import restore_pre_rebind, restore_integrated_rasterizer, INTEGRATED_RASTERIZER
import hashlib
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]/"encore-active/eden-vulkan"
files={
"fixed_pipeline_state.cpp":"fe9189f92163455127adf5f74a4a181f320a01f4",
"include/video_core/renderer_vulkan/fixed_pipeline_state.h":"21d47b3ab5472e514e28b8ab28233f6d60b8aa59",
"vulkan_rasterizer.cpp":"2530e3754435175810cbf24cb20446e177d1f19f",
"vulkan_device.cpp":"50578df547b769bd45cf4964ffa3f793c1749273",
"include/video_core/vulkan_common/vulkan_device.h":"92fde878aefc4a48a6e84264c1b914faecc3c477",
"vk_graphics_pipeline_cost.cpp":"d7563d41b74d780f4512899e2fda1b2f362f5523",
"vk_pipeline_cache_cost.cpp":"9d963a5f5c48b9b7f673f72a18552408a9a38807",
}
for name,expected in files.items():
    v=(root/name).read_bytes()
    actual=hashlib.sha1(b"blob "+str(len(v)).encode()+bytes([0])+v).hexdigest()
    if name=="include/video_core/vulkan_common/vulkan_device.h":
        text=v.decode()
        assert "IsExtDepthClipEnableSupported()" in text and "GetMaxSubgroupSize() const" in text,(name,"lost EDS3/subgroup API")
    elif name=="vk_graphics_pipeline_cost.cpp" and actual=="efa8fd4d73c111655fced67fe7dd1e6b7c0178ba":
        text=v.decode()
        fixed="const u32 second_offset{desc.secondary_cbuf_offset};"
        indexed="const u32 second_offset{desc.secondary_cbuf_offset + index_offset};"
        assert text.count(fixed)==1 and indexed not in text
        normalized=text.replace(fixed,indexed,1).encode()
        normalized_sha=hashlib.sha1(b"blob "+str(len(normalized)).encode()+bytes([0])+normalized).hexdigest()
        assert normalized_sha==expected,(name,actual,normalized_sha,expected)
    elif name=="vk_graphics_pipeline_cost.cpp" and actual in {"bae8f3c36fc4e595e9b630ca55cc50496468e173","f3c82781675b6979a8be96a333184e048ef57f81"}:
        # The live FC27 owner extends the earlier exact-attribute binding
        # with an optional bounded graphics transition hint graph. Its exact
        # full-source SHA is pinned, and the separate FC27 transition harness
        # compiles Next/AddTransition with the fallback cache still complete.
        text=v.decode()
        assert text.count("const u32 second_offset{desc.secondary_cbuf_offset};")==1
        assert "desc.secondary_cbuf_offset + index_offset" not in text
        assert text.count("kNativeTransitionHintLimit")==3 or (
            "kNativeTransitionHintLimit" in text and "next_transition_replacement" in text)
    elif name=="vk_pipeline_cache_cost.cpp":
        text=v.decode()
        assert "dynamic_features.has_depth_clip_enable = device.IsExtDepthClipEnableSupported();" in text,(name,"lost EDS3 policy")
        assert "single_lane_geometry_subgroups" in text and "Shader::Optimization::HasBrokenPattern" in text,(name,"lost shader integration")
    elif name=="vulkan_device.cpp" and actual=="c1126bd34a026ccc5c46acea49c9b4a3deeb5d26":
        # Exact current Vulkan device owner differs from frozen R23 for
        # other reviewed PS5/NS2 capabilities. Check depth-clip negotiation
        # without weakening its opt-in/feature guard.
        text=v.decode()
        assert "extensions.depth_clip_enable = features.depth_clip_enable.depthClipEnable;" in text
        assert "RemoveExtensionFeatureIfUnsuitable(extensions.depth_clip_enable" in text
    elif name=="fixed_pipeline_state.cpp":
        text=v.decode()
        assert "MaxwellDepthClampClip" in text and "regs.tir_mode == Maxwell::TIRMode::RasterNTargetM" in text,(name,"lost EDS3/TIR")
    elif name=="vulkan_rasterizer.cpp":
        # Nov13 QueryCache lifetime fix is added after the R23 renderer receipt.
        # Prove its exact selected destructor, then reverse ONLY that hunk
        # before checking the historical source blob.
        post_teardown = "RasterizerVulkan::~RasterizerVulkan() {\n    // QueryCache has a shorter lifetime than BufferCache. BufferCache::~\n    // triggers a final scheduler.Finish() AFTER QueryCache is destroyed.\n    // Drain outstanding queries while alive, then revoke the borrowed pointer\n    // to prevent the shutdown path from reading freed QueryCache storage.\n    scheduler.WaitWorker();\n    scheduler.Finish();\n    scheduler.ClearQueryCache();\n}"
        pre_teardown = "RasterizerVulkan::~RasterizerVulkan() {\n    scheduler.WaitWorker();\n    scheduler.Finish();\n}"
        if actual in {"71d097b9a83028dfad9370792c905bb3bfccfd5a", INTEGRATED_RASTERIZER}:
            # This byte-sealed runtime successor includes reviewed frame/CPU
            # invalidation optimizations and the anti-regression rollback of
            # deferred page protection (#194). Do not attempt to reverse it
            # through a two-commit-only MSAA helper. The engine Vulkan oracle
            # reconstructs the exact historic R23 predecessor separately.
            source=restore_integrated_rasterizer(v.decode("utf-8"))
            assert source.count(post_teardown)==1
            assert "MaxwellDepthClampClip(regs, device.IsExtDepthClipEnableSupported()).clamp" in source
            assert "buffer_cache.ClaimCpuWrites(sequences);" in source
            assert "buffer_cache.ClaimCpuWrites(ranges);" in source
            assert "deferred_cached_updates" not in source
            assert "GetDeviceMemoryUsage()" in source
        else:
            source = restore_pre_rebind(v.decode("utf-8"))
            assert source.count(post_teardown)==1, "FC27 teardown revocation lost"
            normalized = source.replace(post_teardown, pre_teardown, 1).encode("utf-8")
            normalized_sha = hashlib.sha1(b"blob "+str(len(normalized)).encode()+bytes([0])+normalized).hexdigest()
            assert normalized_sha==expected,(name,actual,normalized_sha,expected)
    else:
        assert actual==expected,(name,actual)
source=(root/"fixed_pipeline_state.cpp").read_text()
start="DepthClampClip MaxwellDepthClampClip(const Maxwell& regs, bool has_depth_clip_enable) {"
end="\nvoid FixedPipelineState::DynamicState::Refresh3("
assert source.count(start)==1
method=start+source.split(start,1)[1].split(end,1)[0]
header=(root/"include/video_core/renderer_vulkan/fixed_pipeline_state.h").read_text()
assert "BitField<13, 1, u32> depth_clip_disabled;" in header
assert "bool has_depth_clip_enable;" in header
assert "IsExtDepthClipEnableSupported()" in (root/"include/video_core/vulkan_common/vulkan_device.h").read_text()
assert "features.depth_clip_enable.depthClipEnable" in (root/"vulkan_device.cpp").read_text()
assert "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT" in (root/"vk_graphics_pipeline_cost.cpp").read_text()
assert "dynamic_features.has_depth_clip_enable = device.IsExtDepthClipEnableSupported();" in (root/"vk_pipeline_cache_cost.cpp").read_text()
assert "return pipeline;" in (root/"vk_pipeline_cache_cost.cpp").read_text() # native PS5 never skips incomplete async side-effect draw
assert "MaxwellDepthClampClip(regs, device.IsExtDepthClipEnableSupported()).clamp" in (root/"vulkan_rasterizer.cpp").read_text()

fixture=r"""
#include <cassert>
namespace Test {
struct Maxwell {
 struct ViewportClipControl {
  enum class GeometryClip {Passthrough,FrustumXYZ,FrustumZ,FrustumXY,Other};
  GeometryClip geometry_clip=GeometryClip::Passthrough;
  int pixel_min_z=0;
  int pixel_max_z=0;
 };
 ViewportClipControl viewport_clip_control;
};
struct DepthClampClip {bool clamp;bool clip;};
""" + method + r"""
}
int main(){
 using namespace Test;
 Maxwell r{};
 for(auto gc: {Maxwell::ViewportClipControl::GeometryClip::Passthrough,
               Maxwell::ViewportClipControl::GeometryClip::FrustumXYZ,
               Maxwell::ViewportClipControl::GeometryClip::FrustumZ}){
   r.viewport_clip_control.geometry_clip=gc;
   auto noext=MaxwellDepthClampClip(r,false);
   assert(!noext.clamp && noext.clip);
   auto ext=MaxwellDepthClampClip(r,true);
   assert(!ext.clamp && ext.clip);
 }
 r.viewport_clip_control.geometry_clip=Maxwell::ViewportClipControl::GeometryClip::FrustumXY;
 assert(MaxwellDepthClampClip(r,false).clamp);
 assert(!MaxwellDepthClampClip(r,false).clip);
 assert(!MaxwellDepthClampClip(r,true).clamp);
 assert(!MaxwellDepthClampClip(r,true).clip);
 r.viewport_clip_control.pixel_min_z=1;
 assert(MaxwellDepthClampClip(r,true).clamp);
 assert(!MaxwellDepthClampClip(r,true).clip);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-depth-") as td:
 d=Path(td)
 (d/"t.cpp").write_text(fixture.replace("#include <cassert>","#include <cassert>\n#include <initializer_list>"))
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g","-fsanitize=address,undefined",
                 str(d/"t.cpp"),"-o",str(d/"test")],check=True,timeout=60)
 subprocess.run([str(d/"test")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("NS1_R23_EDS3_DEPTHCLIP_SEVEN_SOURCE_ASAN_UBSAN_PASS ext_on_off=1 z_clip=1 minz=1")
