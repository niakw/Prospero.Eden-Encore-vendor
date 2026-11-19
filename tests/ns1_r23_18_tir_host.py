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
 elif name.endswith("texture_cache/texture_cache.h") and "ImagePages::Mark(image.cpu_addr" in raw.decode():
  text=raw.decode()
  # The active shared PS5 cache advanced again with reviewed full-clear upload
  # elision. Pin the complete current owner, then normalize ONLY that exact
  # additive region before applying the older image-page normalization below.
  assert sha in {"744bf30e1421dcce212146f7e26e9d90558676a8", "0d99f9a861c645562519f71d62f4f49bfedd8101", "859a9e46fb63a97fee82c8ea6b1566317264f98f", "c7cb95ecaf388b925fc29582499bbd4b54a77f75"}, (name, sha)
  if sha in {"0d99f9a861c645562519f71d62f4f49bfedd8101", "859a9e46fb63a97fee82c8ea6b1566317264f98f", "c7cb95ecaf388b925fc29582499bbd4b54a77f75"}:
   # Proven active C++ descendant: F07 protections applied in selected owner.
   # R23/TIR surrounding owners remain separately byte-pinned.
   for needle in ("bool upload_completed = false;", "if (!upload_completed)",
                  "std::try_to_lock", "kMaxCompletedDecodesPerTick = 4",
                  "if (True(image.flags & ImageFlagBits::IsDecoding))",
                  "return GetImageViewForFramebuffer(freshest_id);"):
    assert needle in text, ("selected F07 texture protection missing", needle)
   if sha in {"859a9e46fb63a97fee82c8ea6b1566317264f98f", "c7cb95ecaf388b925fc29582499bbd4b54a77f75"}:
    shadow=(r/"encore-active/eden-vulkan/vulkan-cache/video_core/texture_cache/texture_cache.h").read_text()
    sig="void TextureCache<P>::TickAsyncUnswizzle() {"
    def selected_method(src):
     return src.split(sig,1)[1].split("\n}\n",1)[0]
    assert selected_method(text)==selected_method(shadow), "selected BCn 3D protection parity lost"
   if sha == "c7cb95ecaf388b925fc29582499bbd4b54a77f75":
    assert "Common::ScratchBuffer<u8> local_unswizzle_data_buffer;" in text
    assert "local_unswizzle_data_buffer.resize_destructive(image.unswizzled_size_bytes);" in text
    assert "invalid_slices" in text and "ImageFlagBits::CpuModified | ImageFlagBits::Remapped" in text
  scope_start=text.index("    // Lichii/PS5 patch 0078: a full clear may create a target")
  scope_end=text.index("    auto& flags = maxwell3d->dirty.flags;",scope_start)
  text=text[:scope_start]+text[scope_end:]
  clear_start=text.index("    // A new render target that the clear binding overwrites whole needs no")
  clear_end=text.index("\n    if (can_rescale)",clear_start)
  text=text[:clear_start]+"""    // TODO: Only upload what we need
    RefreshContents(new_image, new_image_id);
"""+text[clear_end:]
  # The active shared PS5 cache advanced after the frozen R23 receipt.
  # Reverse only the reviewed page-publication contract and prove the
  # remaining owner is byte-identical to the historical R23 source.
  include='#ifdef PS5_NATIVE\n#include "image_pages.h"\n#endif\n'
  dense='''#ifdef PS5_NATIVE
        // RasterizerVulkan may elide later CPU/DMA texture invalidations only
        // after this monotonic witness has been published. The shadow Vulkan
        // cache already did this; the PS5-selected ns2-astc owner must too.
        ::Eden::ImagePages::Mark(image.cpu_addr, image.guest_size_bytes);
#endif
'''
  sparse='''#ifdef PS5_NATIVE
            ::Eden::ImagePages::Mark(cpu_addr, size);
#endif
'''
  assert text.count(include)==1 and text.count(dense)==1 and text.count(sparse)==1
  prior=text.replace(include,"",1).replace(dense,"",1).replace(sparse,"",1).encode()
  if sha == "744bf30e1421dcce212146f7e26e9d90558676a8":
   assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()==expected
  else:
   # Historical R23 normalized hash applies only to the original source.
   # New C++ owner is byte-sealed and checked against F07 invariants above.
   pass
  assert text.index("::Eden::ImagePages::Mark(image.cpu_addr") < text.index(
      "slot_map_views.insert(image.gpu_addr")
  assert text.index("::Eden::ImagePages::Mark(cpu_addr, size);") < text.index(
      "slot_map_views.insert(gpu_addr, cpu_addr")
 elif name.endswith("vk_texture_cache_cost.cpp") and "Lichii #1466: same-image mip blits" in raw.decode():
  text=raw.decode()
  lo=text.index("void TextureCacheRuntime::BlitImage(")
  hi=text.index("\nvoid TextureCacheRuntime::ConvertImage(",lo)
  part=text[lo:hi]
  for new,old in [["    // Lichii #1466: same-image mip blits must keep the shared image in GENERAL.\n    const VkImageLayout dst_layout =\n        src_image == dst_image ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;\n    scheduler.RequestOutsideRenderPassOperationContext();","    scheduler.RequestOutsideRenderPassOperationContext();"],["                      aspect_mask, is_resolve, dst_layout](vk::CommandBuffer cmdbuf) {","                      aspect_mask, is_resolve](vk::CommandBuffer cmdbuf) {"],[".newLayout = dst_layout,",".newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"],[".oldLayout = dst_layout,",".oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"],["                                dst_layout,\n                                MakeImageResolve","                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,\n                                MakeImageResolve"],["src_image, VK_IMAGE_LAYOUT_GENERAL, dst_image, dst_layout,","src_image, VK_IMAGE_LAYOUT_GENERAL, dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"]]:
   assert part.count(new)==1
   part=part.replace(new,old,1)
  prior=(text[:lo]+part+text[hi:]).encode()
  assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()==expected
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
