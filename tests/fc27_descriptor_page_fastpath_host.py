#!/usr/bin/env python3
"""Exact Eglantine #374 one-page descriptor-read source/model proof."""
from pathlib import Path
import hashlib
import re

ROOT=Path(__file__).resolve().parents[1]
PINNED=ROOT/"sources/eden/src/video_core/texture_cache/descriptor_table.h"
ACTIVE=ROOT/"encore-active/eden-core/src/video_core/texture_cache/descriptor_table.h"
MEM=ROOT/"sources/eden/src/video_core/memory_manager.h"
COREMEM=ROOT/"sources/eden/src/core/memory.h"

def blob_bytes(raw: bytes)->str:
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()
def blob(p:Path)->str:
    return blob_bytes(p.read_bytes())

assert blob(PINNED)=="e40c128ab5798d0e5c1ee99ce2513b0246ae961f"
assert blob(ACTIVE)=="be14d3396ff3871ceed46cb8499e9cfb546719de"

src=ACTIVE.read_text()
orig=PINNED.read_text()
for needle in (
    "#include <cstring>",
    '#include "core/memory.h"',
    "const bool one_page =",
    "(gpu_addr & Core::Memory::YUZU_PAGEMASK) + sizeof(T) <= Core::Memory::YUZU_PAGESIZE",
    "one_page ? gpu_memory.GetPointer(gpu_addr) : nullptr",
    "std::memcpy(std::addressof(result.first), host, sizeof(T));",
    "gpu_memory.ReadBlockUnsafe(gpu_addr, std::addressof(result.first), sizeof(T));",
):
    assert src.count(needle)==1,needle

# Exact reversible delta: no logging/texture-cache policy changes are bundled.
rev=src
rev=rev.replace("#include <algorithm>\n#include <cstring>\n","#include <algorithm>\n",1)
rev=rev.replace('#include "common/assert.h"\n#include "core/memory.h"\n',
                '#include "common/assert.h"\n',1)
new='''        // Eglantine #374: descriptors that fit in one guest page are
        // contiguous in host memory. Avoid the generic block-read path for
        // the per-texture/per-sampler hot path; keep the old fallback for
        // page crossings or unresolved mappings.
        const bool one_page =
            (gpu_addr & Core::Memory::YUZU_PAGEMASK) + sizeof(T) <= Core::Memory::YUZU_PAGESIZE;
        if (const u8* const host = one_page ? gpu_memory.GetPointer(gpu_addr) : nullptr) {
            std::memcpy(std::addressof(result.first), host, sizeof(T));
        } else {
            gpu_memory.ReadBlockUnsafe(gpu_addr, std::addressof(result.first), sizeof(T));
        }
'''
old='''        gpu_memory.ReadBlockUnsafe(gpu_addr, std::addressof(result.first), sizeof(T));
'''
assert rev.count(new)==1
rev=rev.replace(new,old,1)
assert rev==orig
assert blob_bytes(rev.encode())=="e40c128ab5798d0e5c1ee99ce2513b0246ae961f"

mem=MEM.read_text()
assert "[[nodiscard]] const u8* GetPointer(GPUVAddr addr) const;" in mem

# Model exact page-boundary decision. Use the actual core constants when
# available; this guards <= at the last fitting byte and fallback on crossing.
cm=COREMEM.read_text()
m=re.search(r"YUZU_PAGEBITS\s*=\s*(\d+)",cm)
assert m,m
bits=int(m.group(1))
page=1<<bits
mask=page-1
def one_page(addr:int,size:int)->bool:
    return (addr & mask)+size <= page
for size in (4,8,16,32,64):
    assert one_page(page-size,size)
    assert one_page(page*3+page-size,size)
    assert not one_page(page-size+1,size)
    assert not one_page(page*7+page-1,size)
assert one_page(0,64)

print(f"FC27_DESCRIPTOR_PAGE_FASTPATH_PASS page={page} reversible=1 in_page=direct crossing=fallback ps5_perf=unverified")
