#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pin the FC27 video-output -> guest invalidation -> texture publication chain.

R1 rendered the intro. Current PS5 VIC writes decoded output through SafeWrite;
the guest-memory scope invalidates that range. The rasterizer may elide that
invalidation only if the selected TextureCache has published image pages first.
Source proof only; PS5 picture output still requires hardware validation.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
vic = (ROOT / "encore-active/eden/src/video_core/host1x/vic.cpp").read_text()
guest = (ROOT / "encore-active/eden-core/overrides/core/guest_memory.h").read_text()
raster = (ROOT / "encore-active/eden-vulkan/vulkan_rasterizer.cpp").read_text()
texture = (ROOT / "encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h").read_text()

# Both VIC output families can write directly into guest GPU memory.
assert vic.count("Core::Memory::GuestMemoryFlags::SafeWrite") >= 3
assert "regs.output_surface.luma.Address()" in vic
assert "regs.output_surface.chroma_u.Address()" in vic

# A direct writable span is published to the GPU caches by invalidation.
scope = guest.split("class GuestMemoryScoped", 1)[1]
assert "this->m_memory->InvalidateRegion(this->m_addr, this->size_bytes());" in scope
assert "FLAGS & GuestMemoryFlags::Safe" in scope

# Current PS5 fast-path consumes the monotonic page witness.
assert raster.count("::Eden::ImagePages::NeverHeld") >= 5
cpu_write = raster.split("bool RasterizerVulkan::OnCPUWrite", 1)[1].split(
    "void RasterizerVulkan::OnCacheInvalidations", 1
)[0]
assert "::Eden::ImagePages::NeverHeld" in cpu_write
assert "texture_cache.WriteMemory" in cpu_write

# The selected owner, not only the shadow Vulkan cache, must publish before
# dense and sparse slot-map insertion.
dense = "::Eden::ImagePages::Mark(image.cpu_addr, image.guest_size_bytes);"
sparse = "::Eden::ImagePages::Mark(cpu_addr, size);"
register = texture.split("void TextureCache<P>::RegisterImage", 1)[1].split(
    "void TextureCache<P>::UnregisterImage", 1
)[0]
assert register.count(dense) == 1
assert register.count(sparse) == 1
assert register.index(dense) < register.index(
    "slot_map_views.insert(image.gpu_addr, image.cpu_addr, image.guest_size_bytes, image_id)"
)
assert register.index(sparse) < register.index(
    "slot_map_views.insert(gpu_addr, cpu_addr, size, image_id)"
)

print("FC27_VIC_TEXTURE_INVALIDATION_SOURCE_PASS safe_write=1 invalidate=1 dense=1 sparse=1")
