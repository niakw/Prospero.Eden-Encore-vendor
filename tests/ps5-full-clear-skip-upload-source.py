#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pin the conservative PS5/Lichii 0078 full-clear upload elision.

Source/parity proof only. It does not claim a GPU-time improvement on Encore.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
paths = [
    ROOT / "encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h",
    ROOT / "encore-active/eden-vulkan/vulkan-cache/video_core/texture_cache/texture_cache.h",
]
bases = [
    ROOT / "encore-active/ns2-astc/include/video_core/texture_cache/texture_cache_base.h",
    ROOT / "encore-active/eden-vulkan/vulkan-cache/video_core/texture_cache/texture_cache_base.h",
]
blocks = []
for path in paths:
    source = path.read_text()
    update = source.split("void TextureCache<P>::UpdateRenderTargets(bool is_clear) {", 1)[1].split(
        "\ntemplate <class P>\ntypename P::Framebuffer*", 1)[0]
    assert update.count("struct ClearingScope") == 1
    assert "clearing_scope{clearing_render_targets}" in update
    assert "clearing_render_targets = is_clear;" in update
    join = source.split("ImageId TextureCache<P>::JoinImages(", 1)[1].split(
        "\ntemplate <class P>", 1)[0]
    begin = join.index("    const auto overwritten_by_clear = [&] {")
    end = join.index("\n    if (can_rescale)", begin)
    block = join[begin:end]
    blocks.append(block)
    for needle in (
        "!clearing_render_targets", "!join_copies_to_do.empty()",
        "new_info.type != ImageType::e2D", "new_info.resources.levels != 1",
        "new_info.resources.layers != 1", "new_info.num_samples != 1",
        "surface.layer != 0", "regs.clear_control.use_scissor != 0",
        "scissor.min_x != 0", "scissor.max_x < width",
        "rt.width < width || rt.height < height",
        "regs.zeta_size.width < width || regs.zeta_size.height < height",
        "surface.R && surface.G && surface.B && surface.A",
        "surface.Z && full_stencil", "runtime.TransitionImageLayout(new_image)",
        "RefreshContents(new_image, new_image_id)",
    ):
        assert needle in block, (path, needle)
    assert block.index("if (overwritten_by_clear())") < block.index(
        "RefreshContents(new_image, new_image_id)")
assert blocks[0] == blocks[1], "active/shadow full-clear safety block diverged"
for path in bases:
    source = path.read_text()
    assert source.count("bool clearing_render_targets = false;") == 1

# Safety model for the non-negotiable rejection boundaries.
def eligible(*, clearing=True, copies=0, two_d=True, levels=1, layers=1, samples=1,
             layer=0, scissor_full=True, smaller_target=False, kind="color", rgba=True,
             z=True, stencil=True, same_target=True):
    if not clearing or copies or not two_d or levels != 1 or layers != 1 or samples != 1:
        return False
    if layer != 0 or not scissor_full or smaller_target:
        return False
    if kind == "color":
        return rgba and same_target
    if kind == "depth":
        return z and same_target
    if kind == "stencil":
        return stencil and same_target
    if kind == "depthstencil":
        return z and stencil and same_target
    return False

assert eligible()
assert eligible(kind="depthstencil")
for kwargs in (
    {"clearing": False}, {"copies": 1}, {"two_d": False}, {"levels": 2}, {"layers": 2},
    {"samples": 2}, {"layer": 1}, {"scissor_full": False}, {"smaller_target": True},
    {"rgba": False}, {"same_target": False}, {"kind": "depth", "z": False},
    {"kind": "depthstencil", "stencil": False},
):
    assert not eligible(**kwargs), kwargs

print("PS5_FULL_CLEAR_UPLOAD_ELISION_SOURCE_PASS active_shadow_parity=1 rejection_cases=13 console_unverified=1")
