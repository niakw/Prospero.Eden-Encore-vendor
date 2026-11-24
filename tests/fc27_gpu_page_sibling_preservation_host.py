#!/usr/bin/env python3
"""Qualify upstream b13c4c1 on Encore's selected BufferCache owner.

A CPU write changes page-granular tracker ownership. GPU-written sibling bytes
on that same page must be downloaded before the page becomes CPU-owned, or a
later upload can overwrite current host data with stale guest bytes.

Host-only: exact source method plus a synthetic interval model; no gameplay or
PS5 runtime claim.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
BASE=(ROOT/"encore-active/eden-core/src/video_core/buffer_cache/buffer_cache_base.h").read_text()
CACHE=(ROOT/"encore-active/eden-core/src/video_core/buffer_cache/buffer_cache.h").read_text()
RANGESET=(ROOT/"sources/eden/src/common/range_sets.h").read_text()
# Preserve the immutable upstream snapshot and authenticate the native overlay
# by reversing only the exact reviewed additions.
HOT=ROOT/"encore-active/eden-core/overrides/buffer-hotpath"
assert "Intersects(" not in RANGESET
native_range=(HOT/"common/range_sets.h").read_text()
assert native_range.replace("    // Half-open overlap query: one lookup, without enumerating dirty spans.\n    bool Intersects(AddressType base_address, size_t size) const;\n", "", 1) == RANGESET
native_inc=(HOT/"common/range_sets.inc").read_text()
range_add="// Lichii 6b7185676d: the caller needs only a yes/no GPU ownership answer.\ntemplate <typename AddressType>\nbool RangeSet<AddressType>::Intersects(AddressType base_address, size_t size) const {\n    const auto& set = m_impl->m_ranges_set;\n    if (set.empty() || size == 0) {\n        return false;\n    }\n    // Invalid overflowing queries must not falsely advertise clean memory.\n    if (size > std::numeric_limits<AddressType>::max() - base_address) {\n        return true;\n    }\n    const AddressType end_address = base_address + static_cast<AddressType>(size);\n    const auto it = set.lower_bound(\n        typename RangeSetImpl::IntervalType{base_address, end_address});\n    return it != set.end() && it->lower() < end_address;\n}\n\n"
assert native_inc.count(range_add)==1
assert native_inc.replace(range_add,"",1)==(ROOT/"sources/eden/src/common/range_sets.inc").read_text()
native_word=(HOT/"video_core/buffer_cache/word_manager.h").read_text()
word_add="            // Lichii f0efe663a5: clean words can skip work only when no\n            // CPU tracking notification is owed. GPU masking stays intact.\n            if (word == 0 &&\n                (!clear || ((type != Type::CPU && type != Type::CachedCPU) ||\n                            (untracked_words[index] & mask) == 0))) {\n                return;\n            }\n"
assert native_word.count(word_add)==1
assert native_word.replace(word_add,"",1)==(ROOT/"sources/eden/src/video_core/buffer_cache/word_manager.h").read_text()
assert "#ifdef PS5_NATIVE\n    // Native RangeSet owner is selected together with this BufferCache.\n    // Avoid walking every fragmented GPU-written interval under the cache mutex.\n    return gpu_modified_ranges.Intersects(addr, size);\n#else\n    bool is_dirty = false;\n    gpu_modified_ranges.ForEachInRange(addr, size, [&](DAddr, DAddr) { is_dirty = true; });\n    return is_dirty;\n#endif" in CACHE
# Exhaust the clean-word decision against old state/notification behavior for
# CPU, CachedCPU, GPU and Preflushable, both clearing modes, all 4-bit masks.
for kind in range(4):
    for clear in (False, True):
        for state in range(16):
            for untracked in range(16):
                for cached in range(16):
                    for mask in range(16):
                        effective = mask & ~untracked if kind == 2 else mask
                        word = state & effective
                        old_state = state & ~effective if clear else state
                        old_untracked = untracked & ~effective if clear and kind in (0,1) else untracked
                        old_cached = cached & ~word if clear and kind == 0 else cached
                        notifications = untracked & effective if clear and kind in (0,1) else 0
                        skip = word == 0 and (not clear or kind not in (0,1) or not (untracked & effective))
                        if skip:
                            assert (old_state,old_untracked,old_cached,notifications)==(state,untracked,cached,0)
# Lichii 15dd980: Bananza binds storage slots 16/17. The selected shared
# BufferCache must expose all 32 bits without changing mask width or selecting
# the divergent research-only class layout.
assert "constexpr u32 NUM_STORAGE_BUFFERS = 32;" in BASE
assert 'static_assert(NUM_STORAGE_BUFFERS <= 32, "storage-buffer bitmasks must fit u32");' in BASE
# DMA acceleration may only use one physical buffer span when the full
# guest GPU address range is demonstrably contiguous.
copy_method=CACHE.split("bool BufferCache<P>::DMACopy(",1)[1].split(
    "bool BufferCache<P>::DMAClear(",1)[0]
clear_method=CACHE.split("bool BufferCache<P>::DMAClear(",1)[1].split(
    "std::pair<typename P::Buffer*",1)[0]
for sym in ("gpu_memory->IsGranularRange(src_address, amount)",
            "gpu_memory->IsContinuousRange(src_address, amount)",
            "gpu_memory->IsGranularRange(dest_address, amount)",
            "gpu_memory->IsContinuousRange(dest_address, amount)"):
    assert sym in copy_method, sym
assert copy_method.index("if (!source_contiguous || !dest_contiguous)") < copy_method.index(
    "ClearDownload(*cpu_dest_address, amount)")
for sym in ("gpu_memory->IsGranularRange(dst_address, size)",
            "gpu_memory->IsContinuousRange(dst_address, size)"):
    assert sym in clear_method, sym
assert clear_method.index("!gpu_memory->IsContinuousRange(dst_address, size)") < clear_method.index(
    "ClearDownload(*cpu_dst_address, size)")
assert clear_method.index("SynchronizeBuffer(dest_buffer") < clear_method.index(
    "runtime.ClearBuffer(dest_buffer")
assert "u32 enabled_compute_storage_buffers = 0;" in BASE
assert "u32 written_compute_storage_buffers = 0;" in BASE
assert "ssbo_index >= NUM_STORAGE_BUFFERS" in CACHE
assert "1U << ssbo_index" in CACHE
query_start=CACHE.index("bool BufferCache<P>::IsRegionGpuModified(DAddr addr, size_t size)")
query=CACHE[query_start:CACHE.index("\n}\n", query_start)]
assert "gpu_modified_ranges.ForEachInRange(addr, size," in query
RASTERS=[
    ROOT/"encore-active/eden-vulkan/vulkan_rasterizer.cpp",
    ROOT/"encore-active/eden-core/guest-bridge/vulkan_rasterizer.cpp",
    ROOT/"encore-active/eden-core/src/video_core/renderer_opengl/gl_rasterizer.cpp",
    ROOT/"encore-active/eden-core/src/video_core/renderer_opengl/gl_rasterizer_dev.cpp",
]

assert "void WriteMemory(DAddr device_addr, u64 size, bool keep_page_gpu_data = true);" in BASE
start="template <class P>\nvoid BufferCache<P>::WriteMemory(DAddr device_addr, u64 size, bool keep_page_gpu_data) {"
stop="\n}\n\ntemplate <class P>\nvoid BufferCache<P>::CachedWriteMemory"
assert CACHE.count(start)==CACHE.count(stop)==1
method=start+CACHE.split(start,1)[1].split(stop,1)[0]+"\n}"
for needle in (
    "Upstream Lichii/Eglantine b13c4c1",
    "Lichii 44bf47d",
    "const bool page_has_gpu_writes =",
    "if (keep_page_gpu_data && !page_has_gpu_writes) {",
    "const bool overlaps_cpu_write = IsRegionGpuModified(device_addr, size);",
    "(!overlaps_cpu_write || IsRegionGpuModified(page_begin, page_end - page_begin))",
    "gpu_modified_ranges.Subtract(device_addr, size);",
    "Common::AlignDown(device_addr, DEVICE_PAGESIZE)",
    "Common::AlignUp(device_addr + size, DEVICE_PAGESIZE)",
    "IsRegionGpuModified(page_begin, page_end - page_begin)",
    "DownloadMemory(page_begin, page_end - page_begin);",
    "memory_tracker.MarkRegionAsCpuModified(device_addr, size);",
):
    assert needle in method, needle
sibling_branch = method.split(
    "if (page_has_gpu_writes &&\n"
    "        (!overlaps_cpu_write || IsRegionGpuModified(page_begin, page_end - page_begin))) {", 1
)[1].split("\n    }", 1)[0]
assert sibling_branch.index("DownloadMemory(") < method.rindex("memory_tracker.MarkRegionAsCpuModified")
assert method.index("gpu_modified_ranges.Subtract") < method.index("DownloadMemory(")
assert method.index("const bool page_has_gpu_writes") < method.index("const bool overlaps_cpu_write")
assert method.count("IsRegionGpuModified(page_begin, page_end - page_begin)") == 2

def function(text, marker):
    begin=text.index(marker)
    end=text.find("\nvoid ",begin+len(marker))
    return text[begin:] if end<0 else text[begin:end]

for path in RASTERS:
    text=path.read_text()
    marker=("void RasterizerOpenGL::UnmapMemory(DAddr addr, u64 size)"
            if "opengl" in path.name or "renderer_opengl" in str(path)
            else "void RasterizerVulkan::UnmapMemory(DAddr addr, u64 size)")
    unmap=function(text,marker)
    assert "buffer_cache.WriteMemory(addr, size, false);" in unmap, path
    assert "buffer_cache.WriteMemory(addr, size);" not in unmap, path

iterator_start=BASE.index("    template <typename Func>\n    static void ForEachEnabledBit(")
iterator_end=BASE.index("\n\n    template <typename Func>\n    void ForEachBufferInRange(", iterator_start)
iterator_method=BASE[iterator_start:iterator_end]
assert "enabled_mask &= enabled_mask - 1;" in iterator_method
cpp=r"""
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>
using u32=std::uint32_t; using u64=std::uint64_t; using DAddr=std::uint64_t;
static constexpr DAddr DEVICE_PAGESIZE=4096;
static std::vector<int> events;
namespace Common {
template<class T> constexpr T AlignDown(T v,T a){return v & ~(a-1);}
template<class T> constexpr T AlignUp(T v,T a){return (v+a-1)&~(a-1);}
}
struct Ranges {
    std::vector<std::pair<DAddr,DAddr>> values;
    void Add(DAddr a,u64 n){if(n)values.emplace_back(a,a+n);}
    void Subtract(DAddr a,u64 n){
        const DAddr b=a+n; std::vector<std::pair<DAddr,DAddr>> out;
        for(auto [x,y]:values){
            if(y<=a||x>=b) out.emplace_back(x,y);
            else { if(x<a)out.emplace_back(x,a); if(y>b)out.emplace_back(b,y); }
        }
        values=std::move(out);
    }
    bool Intersects(DAddr a,u64 n) const {
        const DAddr b=a+n;
        for(auto [x,y]:values) if(x<b&&a<y) return true;
        return false;
    }
};
struct Tracker {
    void MarkRegionAsCpuModified(DAddr,u64){events.push_back(3);}
};
template<class P=int> class BufferCache {
public:
    Ranges gpu_modified_ranges;
    Tracker memory_tracker;
    std::vector<std::pair<DAddr,u64>> downloads;
    int exact_queries{};
    bool IsRegionGpuModified(DAddr a,std::size_t n){
        ++exact_queries;
        return gpu_modified_ranges.Intersects(a,n);
    }
    void ClearDownload(DAddr,u64){events.push_back(1);}
    void DownloadMemory(DAddr a,u64 n){events.push_back(2);downloads.emplace_back(a,n);}
    void WriteMemory(DAddr,u64,bool=true);
""" + iterator_method + r"""
};
""" + method + r"""
int main(){
    // Source-extracted binding iteration, including the high (31st) bit.
    const auto check_mask=[](u32 mask) {
        std::vector<u32> actual;
        BufferCache<>::ForEachEnabledBit(mask, [&](u32 i){actual.push_back(i);});
        u32 expected_count=0;
        for (u32 remaining=mask; remaining!=0; remaining &= remaining-1) {
            assert(expected_count<actual.size());
            assert(actual[expected_count++]==static_cast<u32>(std::countr_zero(remaining)));
        }
        assert(expected_count==actual.size());
    };
    for (u32 mask: std::array<u32,6>{0,1,0x80000000u,0xffffffffu,0x55555555u,0xaaaaaaaau}) {
        check_mask(mask);
    }
    for (u32 mask=0; mask<65536; ++mask) check_mask(mask*0x9e3779b9u);
    {
        BufferCache<> c;
        events.clear();
        c.WriteMemory(0x2100,0x20);
        assert((events==std::vector<int>{3}));
        assert(c.exact_queries==1);
        assert(c.downloads.empty());
    }
    {
        BufferCache<> c;
        c.gpu_modified_ranges.Add(0x1000,0x1000);
        events.clear();
        c.WriteMemory(0x1100,0x20);
        assert((events==std::vector<int>{1,2,3}));
        assert(c.downloads.size()==1);
        assert(c.exact_queries==3);
        assert((c.downloads[0]==std::pair<DAddr,u64>{0x1000,0x1000}));
        assert(!c.gpu_modified_ranges.Intersects(0x1100,0x20));
        assert(c.gpu_modified_ranges.Intersects(0x1200,0x20));
    }
    {
        // CPU and GPU dirty spans share a page but never overlap: the page
        // result remains valid, avoiding a third interval lookup.
        BufferCache<> c;
        c.gpu_modified_ranges.Add(0x1200,0x20);
        events.clear();
        c.WriteMemory(0x1100,0x20);
        assert((events==std::vector<int>{2,3}));
        assert(c.exact_queries==2);
        assert(c.downloads.size()==1);
    }
    {
        BufferCache<> c;
        c.gpu_modified_ranges.Add(0x1100,0x20);
        events.clear();
        c.WriteMemory(0x1100,0x20);
        assert((events==std::vector<int>{1,3}));
        assert(c.exact_queries==3);
        assert(c.downloads.empty());
    }
    {
        BufferCache<> c;
        c.gpu_modified_ranges.Add(0x1000,0x1000);
        events.clear();
        c.WriteMemory(0x1100,0x20,false);
        assert((events==std::vector<int>{1,3}));
        assert(c.downloads.empty());
    }
    std::cout<<"sibling_download=1 exact_only=0 unmap=0 bit_iteration=1\n";
}
"""
compilers=list(dict.fromkeys(x for x in (shutil.which("clang++"),shutil.which("g++"),shutil.which(os.getenv("CXX",""))) if x))
assert compilers
with tempfile.TemporaryDirectory(prefix="fc27-gpu-page-sibling-") as td:
    src=Path(td)/"test.cpp"; src.write_text(cpp)
    for cc in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(td)/(Path(cc).name.replace("+","p")+opt)
            cmd=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror","-pedantic",
                 "-fsanitize=undefined","-fno-sanitize-recover=all",
                 str(src),"-o",str(exe)]
            build=subprocess.run(cmd,text=True,capture_output=True,timeout=60)
            if build.returncode:
                raise RuntimeError(build.stdout+build.stderr)
            run=subprocess.run([str(exe)],text=True,capture_output=True,timeout=15)
            if run.returncode:
                raise RuntimeError(run.stdout+run.stderr)

print("FC27_GPU_PAGE_SIBLING_PRESERVATION_HOST_PASS upstream=b13c4c1,44bf47d clean_query=1 native_rangeset_intersects=1 clean_word=1 sibling=downloaded unmap=skip ps5=unverified")
