#!/usr/bin/env python3
"""Compile exact maintained PS5 buffer usage tracker and compare to byte oracle.

Source-level P1 renderer correctness (Eglantine #462), not a PS5 GPU test.
The immutable Eden source must remain pinned and the new header lives in
a separate owned include overlay, selected by the paired engine PR.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
PINNED=ROOT/"sources/eden/src/video_core/buffer_cache/usage_tracker.h"
OWNED=ROOT/"encore-active/eden-core/overrides/usage-tracker/video_core/buffer_cache/usage_tracker.h"
VULKAN=ROOT/"encore-active/eden-vulkan/vk_buffer_cache_cost.cpp"
RENDERER_HEADER=ROOT/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_buffer_cache.h"

def gitblob(src: bytes) -> str:
    return hashlib.sha1(b"blob "+str(len(src)).encode()+bytes([0])+src).hexdigest()
assert gitblob(PINNED.read_bytes())=="6f84e463e32b3e839e200e79dfb93f14f2b14aef"
source=OWNED.read_text()
assert "ValidRange" in source and "PageMask" in source
assert "size <= std::numeric_limits<u64>::max() - offset" in source
assert "(offset_in_page + bytes - 1) >> BUFFER_BYTES_PER_BITSHIFT" in source
assert "if (first_page == last_page) return false;" in source
assert "count = last_bit - first_bit + 1;" in source
assert "if (size == 0) return 0;" in source
assert "buffer.IsRegionUsed(copy.dst_offset, copy.size)" in VULKAN.read_text()
assert "return tracker.IsUsed(offset, size);" in RENDERER_HEADER.read_text()

cpp=r"""
#include <cassert>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <limits>
#include "video_core/buffer_cache/usage_tracker.h"
using VideoCommon::UsageTracker;

static void known_cases() {
    UsageTracker tracker(1u<<20);
    tracker.Track(0,100);
    assert(tracker.IsUsed(0,1));
    assert(tracker.IsUsed(64,16));
    assert(tracker.IsUsed(96,4));
    assert(!tracker.IsUsed(128,64));
    tracker.Reset();
    tracker.Track(60,8);
    assert(tracker.IsUsed(0,4));
    assert(tracker.IsUsed(64,4));
    assert(!tracker.IsUsed(128,4));
    tracker.Reset();
    tracker.Track(4096+128,16);
    assert(tracker.IsUsed(4096+128,16));
    assert(!tracker.IsUsed(4096+256,64));
    assert(!tracker.IsUsed(4096,64));
    tracker.Reset();
    tracker.Track(4096-64,64);
    assert(tracker.IsUsed(4096-64,64));
    assert(!tracker.IsUsed(4096,64));
    assert(!tracker.IsUsed(4096+2048,64));
    tracker.Reset();
    tracker.Track(4096-32,4096+64);
    assert(tracker.IsUsed(4096-32,1));
    assert(tracker.IsUsed(4096+1024,1));
    assert(tracker.IsUsed(8192,16));
    assert(!tracker.IsUsed(8192+64,64));
    assert(!tracker.IsUsed(0,64));
    tracker.Reset();
    assert(!tracker.IsUsed(0,1) && !tracker.IsUsed(4096,4096));
    tracker.Track(0,0);
    assert(!tracker.IsUsed(0,0));
    tracker.Track(UINT64_MAX-5,100);
    assert(!tracker.IsUsed(UINT64_MAX-5,100));
    tracker.Track(UINT64_MAX,1);
    assert(!tracker.IsUsed(UINT64_MAX,1));
}

int main() {
    known_cases();
    // Independent byte-granularity 64-byte bucket oracle. Randomized
    // overlapping regions are deliberately not restricted to aligned spans.
    static constexpr uint32_t BYTES=1<<18;
    UsageTracker tracker(BYTES);
    std::vector<uint8_t> active(BYTES/64+1,0);
    uint64_t seed=0x5eeda19ULL;
    auto rnd=[&]() -> uint32_t {
        seed ^= seed<<13; seed ^= seed>>7; seed ^= seed<<17;
        return static_cast<uint32_t>(seed);
    };
    for (unsigned i=0; i<16000; ++i) {
        if (i%1013==0) {
            tracker.Reset();
            std::fill(active.begin(),active.end(),0u);
        }
        const uint32_t start=rnd()%BYTES;
        const uint32_t maxlen=std::min<uint32_t>(8192,BYTES-start);
        const uint32_t size=(rnd()%(maxlen+1));
        if ((rnd()%4)!=0) {
            tracker.Track(start,size);
            if (size) {
                for(uint32_t b=start/64;b<=(start+size-1)/64;++b) active[b]=1;
            }
        } else {
            bool expected=false;
            if (size)
                for(uint32_t b=start/64;b<=(start+size-1)/64;++b)
                    expected = expected || active[b];
            assert(tracker.IsUsed(start,size)==expected);
        }
    }
}
"""
compilers=[x for x in ("clang++","g++") if shutil.which(x)]
assert compilers
with tempfile.TemporaryDirectory(prefix="ps5-usage-tracker-") as tmp:
    root=Path(tmp)
    stub=root/"common"
    stub.mkdir()
    (stub/"common_types.h").write_text("#pragma once\n#include <cstdint>\nusing u64 = std::uint64_t;\n")
    src=root/"test.cpp"
    src.write_text(cpp)
    includes=["-I",str(root),"-I",str(ROOT/"encore-active/eden-core/overrides/usage-tracker")]
    for cc in compilers:
        exe=root/(cc.replace("+","p")+"-test")
        cmd=[cc,"-std=c++20","-O1","-Wall","-Wextra","-Werror",
             "-fsanitize=address,undefined","-fno-sanitize-recover=all",
             "-fno-omit-frame-pointer",*includes,str(src),"-o",str(exe)]
        proc=subprocess.run(cmd,capture_output=True,text=True,timeout=90)
        assert proc.returncode==0,(cc,proc.stderr[-5000:])
        proc=subprocess.run([str(exe)],capture_output=True,text=True,timeout=40)
        assert proc.returncode==0,(cc,proc.stderr[-3500:])
print("PS5_BUFFER_USAGE_GRANULARITY_HOST_PASS actual_header=1 "
      "partial_64=1 page_boundaries=1 multi_page=1 overflow_rejected=1 "
      "oracle_randomized=16000 original_source_immutable=1 hardware=unverified")
