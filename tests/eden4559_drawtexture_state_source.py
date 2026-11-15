#!/usr/bin/env python3
from pathlib import Path
import hashlib

root=Path(__file__).resolve().parents[1]
active=root/"encore-active/eden-vulkan"
source=root/"sources/eden/src/video_core/renderer_vulkan"

def blob(path):
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

# Pinned source archive stays immutable; the maintained PS5 fork owns the adaptation.
assert blob(source/"blit_image.cpp")=="ddd52e94268b8147a255566123e369e297b772f9"
assert blob(source/"blit_image.h")=="62e6743a23e02e03dc3e05663123a4592ab2572a"
cpp=(active/"blit_image.cpp").read_text()
hdr=(active/"include/video_core/renderer_vulkan/blit_image.h").read_text()
rast=(active/"vulkan_rasterizer.cpp").read_text()
fsr=(active/"fsr31_native_bridge.cpp").read_text()

assert "#include <optional>" in hdr
assert "const std::optional<VkRect2D>& scissor = std::nullopt" in hdr
assert "TransitionImageLayout(cmdbuf, src_image, VK_IMAGE_LAYOUT_GENERAL);" in cpp
assert "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL" not in cpp.split(
    "void BlitImageHelper::BlitColor(const Framebuffer* dst_framebuffer, VkImageView",1)[1].split(
    "void BlitImageHelper::BlitImpl(",1)[0]
assert "const VkFramebuffer framebuffer_handle = dst_framebuffer->Handle();" in cpp
assert "BeginRenderPass(cmdbuf, render_pass, framebuffer_handle, render_area);" in cpp
assert "cmdbuf.SetScissor(0, *clip);" in cpp
assert "scheduler.InvalidateState();" in cpp
assert "std::optional<VkRect2D> scissor;" in rast
assert "src_is_target" in rast
assert "maxwell3d->regs.blend.enable[0] == 0" in rast
assert "src_size, scissor);" in rast
assert "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL" not in fsr.split(
    "const auto temporal_output = TryRecord(frame);",1)[1].split("return true;",1)[0]
print("EDEN4559_DRAWTEXTURE_STATE_SOURCE_PASS scissor=1 general=1 framebuffer_capture=1 selfcopy=1 fsr_general=1")
