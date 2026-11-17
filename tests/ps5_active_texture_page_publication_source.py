#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The PS5-selected shared TextureCache must publish ImagePages before registration.

R1 invalidated texture writes unconditionally. Later PS5 lock-elision uses
ImagePages::NeverHeld(), so the active owner must publish every dense/sparse
CPU span before it can become visible in the texture page tables.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ACTIVE = ROOT / "encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h"
SHADOW = ROOT / "encore-active/eden-vulkan/vulkan-cache/video_core/texture_cache/texture_cache.h"

active = ACTIVE.read_text()
shadow = SHADOW.read_text()

assert '#ifdef PS5_NATIVE\n#include "image_pages.h"\n#endif' in active

start = active.index("void TextureCache<P>::RegisterImage")
end = active.index("\ntemplate <class P>\nvoid TextureCache<P>::UnregisterImage", start)
register = active[start:end]

dense_mark = "::Eden::ImagePages::Mark(image.cpu_addr, image.guest_size_bytes);"
dense_insert = "slot_map_views.insert(image.gpu_addr, image.cpu_addr, image.guest_size_bytes, image_id)"
sparse_mark = "::Eden::ImagePages::Mark(cpu_addr, size);"
sparse_insert = "slot_map_views.insert(gpu_addr, cpu_addr, size, image_id)"

assert register.count(dense_mark) == 1
assert register.count(sparse_mark) == 1
assert register.index(dense_mark) < register.index(dense_insert)
assert register.index(sparse_mark) < register.index(sparse_insert)

# The shadow owner already carried the intended publication contract.
assert shadow.count(dense_mark) == 1
assert shadow.count(sparse_mark) == 1

# Publication is monotonic. Unregister must never clear the witness.
unreg_start = active.index("void TextureCache<P>::UnregisterImage")
unreg_end = active.index("\ntemplate <class P>", unreg_start + 10)
unregister = active[unreg_start:unreg_end]
assert "ImagePages::" not in unregister

print("PS5_ACTIVE_TEXTURE_PAGE_PUBLICATION_SOURCE_PASS dense=1 sparse=1 monotonic=1")
