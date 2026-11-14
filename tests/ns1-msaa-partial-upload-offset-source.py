#!/usr/bin/env python3
"""Static PS5 Vulkan MSAA partial upload address contract. No native build."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "encore-active/eden-vulkan/vk_texture_cache_cost.cpp").read_text()
shader = (root / "sources/eden/src/video_core/host_shaders/"
          "convert_non_msaa_to_msaa.frag").read_text()
depth_shader = (root / "sources/eden/src/video_core/host_shaders/"
                "convert_non_msaa_to_msaa_depth.frag").read_text()

upload = cpp.split("void Image::UploadMemory(VkBuffer buffer,", 1)[1].split(
    "void Image::UploadMemory(const StagingBufferRef& map,", 1)[0]
assert "TransformBufferImageCopies(copies, offset, upload_aspect_mask)" in upload
assert "image_copy.src_offset = copy.image_offset;" in upload
assert "image_copy.dst_offset = {copy.image_offset.x >> samples_x," in upload
assert "image_copy.src_subresource = copy.image_subresource;" in upload
assert "image_copy.dst_subresource = copy.image_subresource;" in upload
assert "CopyMSAA(" in upload and "CopyMSAADepth(" in upload
assert "image_copy.src_offset = {0, 0, 0};" not in upload
for program in (shader, depth_shader):
    assert "msaa_coord * scale + sample_offset + src_offset" in program
print("NS1_MSAA_PARTIAL_UPLOAD_SOURCE_CONTRACT_PASS offsets=1 color=1 depth=1 ps5_runtime=unverified")
