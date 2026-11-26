#!/usr/bin/env python3
"""Qualify Lichii #2121 Switch 2 software-blitter native tiling.

The immutable Eden owner remains Maxwell/NS1. The maintained PS5 owner must
differ only by forwarding MemoryManager::UsesNativeTiling() to both block-linear
source and destination subrect transforms. The decoder API keeps false as its
default, preserving the NS1 layout when native tiling is not selected.
"""
from pathlib import Path
import hashlib

root = Path(__file__).resolve().parents[1]
pinned = (root / "sources/eden/src/video_core/engines/sw_blitter/blitter.cpp").read_text()
active = (root / "encore-active/eden-core/src/video_core/engines/sw_blitter/blitter.cpp").read_text()
header = (root / "encore-active/eden-core/overrides/native-tiling/video_core/textures/decoders.h").read_text()

src_old = """                         src.block_height, src.block_depth, src_extent_x * src_bytes_per_pixel);"""
src_new = """                         src.block_height, src.block_depth, src_extent_x * src_bytes_per_pixel,
                         memory_manager.UsesNativeTiling());"""
dst_old = """                       dst.block_height, dst.block_depth, dst_extent_x * dst_bytes_per_pixel,
                       false, dst.layer);"""
dst_new = """                       dst.block_height, dst.block_depth, dst_extent_x * dst_bytes_per_pixel,
                       memory_manager.UsesNativeTiling(), dst.layer);"""

assert "memory_manager.UsesNativeTiling()" not in pinned
assert active.count(src_new) == 1 and active.count(dst_new) == 1
assert active.count("memory_manager.UsesNativeTiling()") == 2

# The maintained blitter already carried reviewed PS5 changes before #2121.
# Reverse ONLY the two native-tiling arguments and require its exact previous
# Git blob, instead of incorrectly comparing the maintained owner to pristine Eden.
previous = active.replace(src_new, src_old, 1).replace(dst_new, dst_old, 1)
previous_blob = hashlib.sha1(
    b"blob " + str(len(previous.encode())).encode() + b"\0" + previous.encode()
).hexdigest()
# Since the original #2121 receipt, the selected PS5 owner also gained
# independent fail-closed geometry/allocation hardening, exact nearest-neighbor sampling,
# and destination-only 3D DST_LAYER support. Reversing only the native flag preserves
# the explicit destination origin_z while restoring Maxwell GOB ordering.
assert hashlib.sha1(
    b"blob " + str(len(active.encode())).encode() + b"\0" + active.encode()
).hexdigest() == "d5a5436cdc2414c53834a886cdaa4578b4dc8a94"
assert previous_blob == "712095522ebd7d6bd469651114e27079c8365f22", previous_blob
for safety in (
    "MAX_SOFTWARE_BLIT_BYTES = 256ULL << 20",
    "checked_surface_size",
    "std::optional<size_t>",
    "surface.block_height > 5",
    "src.Address() > ~u64{0} - *src_size",
    "dst.Address() > ~u64{0} - *dst_size",
):
    assert safety in previous, ("software blitter safety predecessor drift", safety)

blit = active.split("bool SoftwareBlitEngine::Blit(", 1)[1]
source_branch = blit.split("if (src.linear == Fermi2D::MemoryLayout::BlockLinear)", 1)[1].split(
    "} else {", 1)[0]
dest_branch = blit.split("if (dst.linear == Fermi2D::MemoryLayout::BlockLinear)", 1)[1].split(
    "} else {", 1)[0]
assert "UnswizzleSubrect(" in source_branch and "memory_manager.UsesNativeTiling()" in source_branch
assert "SwizzleSubrect(" in dest_branch and "memory_manager.UsesNativeTiling()" in dest_branch
assert "dst.layer" in dest_branch

# NS1/Maxwell compatibility: callers that do not opt into the Switch 2 layout
# continue to use the old layout.
assert header.count("bool native_tiling = false") == 4
assert header.count("u32 origin_z") == 2
print("NS2_SW_BLITTER_NATIVE_TILING_PASS upstream_pr=2121 source=1 destination=1 ns1_default=0")
