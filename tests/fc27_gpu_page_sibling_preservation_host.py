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
    "gpu_modified_ranges.Subtract(device_addr, size);",
    "Common::AlignDown(device_addr, DEVICE_PAGESIZE)",
    "Common::AlignUp(device_addr + size, DEVICE_PAGESIZE)",
    "IsRegionGpuModified(page_begin, page_end - page_begin)",
    "DownloadMemory(page_begin, page_end - page_begin);",
    "memory_tracker.MarkRegionAsCpuModified(device_addr, size);",
):
    assert needle in method, needle
assert method.index("gpu_modified_ranges.Subtract") < method.index("DownloadMemory(")
assert method.index("DownloadMemory(") < method.index("MarkRegionAsCpuModified")

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

cpp=r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>
using u64=std::uint64_t; using DAddr=std::uint64_t;
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
    bool IsRegionGpuModified(DAddr a,std::size_t n){return gpu_modified_ranges.Intersects(a,n);}
    void ClearDownload(DAddr,u64){events.push_back(1);}
    void DownloadMemory(DAddr a,u64 n){events.push_back(2);downloads.emplace_back(a,n);}
    void WriteMemory(DAddr,u64,bool=true);
};
""" + method + r"""
int main(){
    {
        BufferCache<> c;
        c.gpu_modified_ranges.Add(0x1000,0x1000);
        events.clear();
        c.WriteMemory(0x1100,0x20);
        assert((events==std::vector<int>{1,2,3}));
        assert(c.downloads.size()==1);
        assert(c.downloads[0]==std::pair<DAddr,u64>{0x1000,0x1000});
        assert(!c.gpu_modified_ranges.Intersects(0x1100,0x20));
        assert(c.gpu_modified_ranges.Intersects(0x1200,0x20));
    }
    {
        BufferCache<> c;
        c.gpu_modified_ranges.Add(0x1100,0x20);
        events.clear();
        c.WriteMemory(0x1100,0x20);
        assert((events==std::vector<int>{1,3}));
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
    std::cout<<"sibling_download=1 exact_only=0 unmap=0\n";
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

print("FC27_GPU_PAGE_SIBLING_PRESERVATION_HOST_PASS upstream=b13c4c1 sibling=downloaded unmap=skip ps5=unverified")
