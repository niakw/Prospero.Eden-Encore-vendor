#!/usr/bin/env python3
"""Qualify Lichii #2121 Switch 2 software-blitter native tiling.

The immutable Eden owner remains Maxwell/NS1. The maintained PS5 owner must
differ only by forwarding MemoryManager::UsesNativeTiling() to both block-linear
source and destination subrect transforms. The decoder API keeps false as its
default, preserving the NS1 layout when native tiling is not selected.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
pinned = (root / "sources/eden/src/video_core/engines/sw_blitter/blitter.cpp").read_text()
active = (root / "encore-active/eden-core/src/video_core/engines/sw_blitter/blitter.cpp").read_text()
header = (root / "encore-active/eden-core/overrides/native-tiling/video_core/textures/decoders.h").read_text()

src_old = """                         src.block_height, src.block_depth, src_extent_x * src_bytes_per_pixel);"""
src_new = """                         src.block_height, src.block_depth, src_extent_x * src_bytes_per_pixel,
                         memory_manager.UsesNativeTiling());"""
dst_old = """                       dst.block_height, dst.block_depth, dst_extent_x * dst_bytes_per_pixel);"""
dst_new = """                       dst.block_height, dst.block_depth, dst_extent_x * dst_bytes_per_pixel,
                       memory_manager.UsesNativeTiling());"""

assert pinned.count(src_old) == 1 and pinned.count(dst_old) == 1
assert "memory_manager.UsesNativeTiling()" not in pinned
expected = pinned.replace(src_old, src_new, 1).replace(dst_old, dst_new, 1)
assert active == expected, "software blitter drift beyond reviewed Lichii #2121 adaptation"
assert active.count("memory_manager.UsesNativeTiling()") == 2

blit = active.split("bool SoftwareBlitEngine::Blit(", 1)[1]
source_branch = blit.split("if (src.linear == Fermi2D::MemoryLayout::BlockLinear)", 1)[1].split(
    "} else {", 1)[0]
dest_branch = blit.split("if (dst.linear == Fermi2D::MemoryLayout::BlockLinear)", 1)[1].split(
    "} else {", 1)[0]
assert "UnswizzleSubrect(" in source_branch and "memory_manager.UsesNativeTiling()" in source_branch
assert "SwizzleSubrect(" in dest_branch and "memory_manager.UsesNativeTiling()" in dest_branch

# NS1/Maxwell compatibility: callers that do not opt into the Switch 2 layout
# continue to use the old layout.
assert header.count("bool native_tiling = false") == 4
print("NS2_SW_BLITTER_NATIVE_TILING_PASS upstream_pr=2121 source=1 destination=1 ns1_default=0")
