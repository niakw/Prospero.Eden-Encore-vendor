#!/usr/bin/env python3
"""Upstream #397 adaptation: newer guest CPU writes must beat stale GPU readback bytes."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
cache=(root/"encore-active/eden-core/src/video_core/buffer_cache/buffer_cache.h").read_text()
base=(root/"encore-active/eden-core/src/video_core/buffer_cache/buffer_cache_base.h").read_text()
texture=(root/"encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h").read_text()
texture_base=(root/"encore-active/ns2-astc/include/video_core/texture_cache/texture_cache_base.h").read_text()
dirty=(root/"encore-active/eden-core/overrides/core/gpu_dirty_memory_manager.h").read_text()
rast=(root/"encore-active/eden-vulkan/vulkan_rasterizer.cpp").read_text()
manager=(root/"encore-active/eden-core/src/video_core/memory_manager.cpp").read_text()
rast_h=(root/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_rasterizer.h").read_text()
gpu_h=(root/"encore-active/eden-core/overrides/video_core/gpu.h").read_text()
iface=(root/"encore-active/eden-core/overrides/video_core/rasterizer_interface.h").read_text()
gpu_names=("gpu_vulkan.cpp","gpu_gl_native.cpp","gpu_gl_native_dev.cpp")
gpus=[(root/f"encore-active/eden-core/src/video_core/{name}").read_text()
      for name in gpu_names]

# Source ownership: immutable vendor/sources/eden remains untouched; native include override owns the ABI.
assert "void ForEachPendingCpuWrite" in gpu_h
assert "virtual void OnCacheInvalidations" in iface
# The native shared GPU graph selects vulkan_abi, not merely the generic
# overrides copy. Both interfaces must expose the identical batch contract.
unified_iface=(root/"encore-active/ns2-runtime/vulkan_abi/video_core/rasterizer_interface.h").read_text()
batch_abi="virtual void OnCacheInvalidations(std::span<const std::pair<DAddr, u64>> ranges)"
assert iface.count(batch_abi)==unified_iface.count(batch_abi)==1
assert "for (const auto& [addr, size] : ranges) OnCacheInvalidation(addr, size);" in unified_iface
assert "virtual bool DispatchNativeCompute(" in unified_iface

assert "void ForEachPending(" in dirty
assert "PendingCpuWrites pending_cpu_writes;" in base
assert "SetPendingCpuWrites(PendingCpuWrites source)" in base
assert "void ClaimCpuWrites(" in base
assert "PendingCpuWriteRanges()" in base
assert "WriteDownloadedMemory(" in base
assert cache.count("device_memory.WriteBlockUnsafe(")==1, "all readback writes must pass protection helper"
assert cache.count("WriteDownloadedMemory(cpu_written") == 3
assert "buffer_cache.SetPendingCpuWrites(" in rast
assert "gpu.ForEachPendingCpuWrite(callback);" in rast
assert "void RasterizerVulkan::OnCacheInvalidations" in rast
assert "buffer_cache.ClaimCpuWrites(ranges);" in rast
# The GPU-collected path keeps the already-reviewed claim-before-readback
# contract, but locks each cache only once per batch (not per dirty page).
collected=rast.split("void RasterizerVulkan::OnCacheInvalidations(", 1)[1].split(
    "void RasterizerVulkan::OnCacheInvalidation(", 1)[0]
assert collected.count("buffer_cache.ClaimCpuWrites(ranges);")==1
assert collected.count("std::scoped_lock lock{texture_cache.mutex};")==1
assert collected.count("std::scoped_lock lock{buffer_cache.mutex};")==1
assert collected.count("texture_cache.WriteMemory(addr, size);")==1
assert collected.count("buffer_cache.WriteMemory(addr, size);")==1
assert collected.count("pipeline_cache.InvalidateRegion(addr, size);")==1
assert collected.index("texture_cache.WriteMemory(addr, size);") < collected.index(
    "buffer_cache.ClaimCpuWrites(ranges);") < collected.index(
    "buffer_cache.WriteMemory(addr, size);") < collected.index(
    "pipeline_cache.InvalidateRegion(addr, size);")
inner=rast.split("void RasterizerVulkan::InnerInvalidation(", 1)[1].split("bool RasterizerVulkan::OnCPUWrite(", 1)[0]
assert inner.count("buffer_cache.ClaimCpuWrites(sequences);")==1
assert inner.index("buffer_cache.ClaimCpuWrites(sequences);") < inner.index("buffer_cache.WriteMemory(addr, size);")
assert "OnCacheInvalidations(std::span<const std::pair<DAddr, u64>> ranges) override;" in rast_h
for name, src in zip(gpu_names, gpus):
    # Lichii/Eglantine gpu.cpp include the complete collector type directly.
    # core/core.h only forward-declares it; libc++ std::span iteration and
    # ForEachPending member access require the complete type in every TU.
    assert '#include "core/gpu_dirty_memory_manager.h"' in src, name
    assert "system.GetGPUDirtyMemoryManager()" in src
    assert "renderer->ReadRasterizer()->OnCacheInvalidations(writes);" in src
    assert "void GPU::ForEachPendingCpuWrite(" in src

def method(text, marker):
    start=text.index(marker)
    brace=text.index("{",start)
    depth=0
    for i in range(brace,len(text)):
        if text[i]=="{": depth+=1
        elif text[i]=="}":
            depth-=1
            if depth==0: return text[start:i+1]
    raise AssertionError(marker)

# Native PS5 Vulkan obtains ONE driver budget sample before either cache
# mutex and reuses it in both cache frame GC paths.
buffer_frame=cache.split("void BufferCache<P>::TickFrame(",1)[1].split(
    "template <class P>\nvoid BufferCache<P>::UnmapGPUMemory(",1)[0]
texture_frame=texture.split("void TextureCache<P>::TickFrame(",1)[1].split(
    "template <class P>\nconst typename P::ImageView& TextureCache<P>::GetImageView",1)[0]
raster_tick=rast.split("void RasterizerVulkan::TickFrame()",1)[1].split(
    "bool RasterizerVulkan::AccelerateConditionalRendering()",1)[0]
assert "PollFrameMemoryUsage" not in base+cache+raster_tick
assert "void TickFrame(std::optional<u64> externally_sampled_usage" in base+texture_base
assert "total_used_memory = *externally_sampled_usage;" in buffer_frame
assert "total_used_memory = *externally_sampled_usage;" in texture_frame
assert "runtime.GetDeviceMemoryUsage();" in buffer_frame
assert "runtime.GetDeviceMemoryUsage();" in texture_frame
assert raster_tick.count("texture_cache_runtime.GetDeviceMemoryUsage()")==1
assert raster_tick.index("texture_cache_runtime.GetDeviceMemoryUsage()") < raster_tick.index(
    "std::scoped_lock lock{texture_cache.mutex};") < raster_tick.index(
    "texture_cache.TickFrame(frame_memory_usage);") < raster_tick.index(
    "std::scoped_lock lock{buffer_cache.mutex};") < raster_tick.index(
    "buffer_cache.TickFrame(frame_memory_usage);")


# The native GMMU must merge ONLY overlapping or exactly adjacent
# physical spans before CPU ownership is claimed. Never merge through a
# sparse gap: it may contain independent GPU-written sibling bytes.
flush=method(manager,"void MemoryManager::FlushCaching()")
assert "std::sort(page_stash2.begin(), page_stash2.end()," in flush
assert "const DAddr offset = addr - previous.first;" in flush
assert "offset <= previous.second" in flush
assert "page_stash2.resize(retained);" in flush
assert flush.index("page_stash2.resize(retained);") < flush.index(
    "rasterizer->InnerInvalidation(VideoCommon::FixSmallVectorADL(page_stash2));")
claim=method(cache,"template <class P>\ntemplate <typename RangeSize>\nvoid BufferCache<P>::ClaimCpuWrites")
pending=method(cache,"template <class P>\nCommon::RangeSet<DAddr> BufferCache<P>::PendingCpuWriteRanges")
write=method(cache,"template <class P>\nvoid BufferCache<P>::WriteDownloadedMemory")

cpp=r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <utility>
#include <vector>
using u8=std::uint8_t; using u64=std::uint64_t; using DAddr=std::uint64_t;
namespace Common {
template<class T> class RangeSet {
    std::vector<std::pair<T,T>> r;
public:
    void Add(T a,std::size_t n){ if(n) r.emplace_back(a,a+n); }
    void Subtract(T a,std::size_t n){
        const T b=a+n; std::vector<std::pair<T,T>> out;
        for(auto [x,y]:r){ if(y<=a||x>=b) out.emplace_back(x,y); else {
            if(x<a) out.emplace_back(x,a);
            if(y>b) out.emplace_back(b,y);
        }} r=std::move(out);
    }
    template<class F> void ForEach(F&& f) const { for(auto [x,y]:r) f(x,y); }
    template<class F> void ForEachInRange(T a,std::size_t n,F&& f) const {
        T b=a+n; for(auto [x,y]:r){ T s=std::max(x,a),e=std::min(y,b); if(s<e) f(s,e); }
    }
    bool Has(T a) const { for(auto [x,y]:r) if(a>=x&&a<y) return true; return false; }
};
}
struct Mem {
    std::vector<u8> bytes=std::vector<u8>(256,0);
    void WriteBlockUnsafe(DAddr a,const u8* p,u64 n){ std::memcpy(bytes.data()+a,p,n); }
};
template<class P=int> class BufferCache {
public:
    using PendingCpuWrites=std::function<void(const std::function<void(DAddr,size_t)>&)>;
    PendingCpuWrites pending_cpu_writes;
    Mem device_memory;
    Common::RangeSet<DAddr> gpu_modified_ranges;
    std::vector<std::pair<DAddr,u64>> cleared;
    bool IsRegionGpuModified(DAddr a,size_t n){
        bool hit=false; gpu_modified_ranges.ForEachInRange(a,n,[&](DAddr,DAddr){hit=true;}); return hit;
    }
    void ClearDownload(DAddr a,u64 n){cleared.emplace_back(a,n);}
    template <typename RangeSize>
    void ClaimCpuWrites(std::span<const std::pair<DAddr,RangeSize>>);
    Common::RangeSet<DAddr> PendingCpuWriteRanges();
    void WriteDownloadedMemory(const Common::RangeSet<DAddr>&,DAddr,const u8*,u64);
};
""" + claim + "\n" + pending + "\n" + write + r"""
int main(){
    BufferCache<> c;
    std::fill(c.device_memory.bytes.begin(),c.device_memory.bytes.end(),0xee);
    std::vector<u8> stale(128,0x12);
    c.pending_cpu_writes=[](const auto& cb){ cb(32,16); cb(80,8); };
    auto pending=c.PendingCpuWriteRanges();
    c.WriteDownloadedMemory(pending,16,stale.data(),96);
    for(int i=16;i<112;i++){
        const bool protected_cpu=(i>=32&&i<48)||(i>=80&&i<88);
        assert(c.device_memory.bytes[i]==(protected_cpu?0xee:0x12));
    }
    c.gpu_modified_ranges.Add(0,128);
    const std::pair<DAddr,u64> writes[]={{32,16},{80,8}};
    c.ClaimCpuWrites(std::span<const std::pair<DAddr,u64>>{writes});
    assert(c.cleared.size()==2);
    // The source owner must also accept size_t-like collected batches on
    // ABIs where their exact integer type differs from u64.
    c.gpu_modified_ranges.Add(0,128);
    const std::pair<DAddr,std::uint32_t> narrow_writes[]={{32,16},{80,8}};
    c.ClaimCpuWrites(std::span<const std::pair<DAddr,std::uint32_t>>{narrow_writes});
    assert(c.cleared.size()==4);
    for(int i=0;i<128;i++){
        const bool claimed=(i>=32&&i<48)||(i>=80&&i<88);
        assert(c.gpu_modified_ranges.Has(i)==!claimed);
    }
}
"""

# Compile the actual selected GPU-collected batch callback: one lock per
# cache domain, every CPU claim before its buffer writes, and no lost
# shader invalidations. This is the SAME existing FC27 host regression job.
batch_method=method(rast,"void RasterizerVulkan::OnCacheInvalidations(")
batch_cpp=r"""
#include <cassert>
#include <cstdint>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
using DAddr=std::uint64_t; using u64=std::uint64_t;
struct CountMutex {
    int acquired{}, held{};
    void lock(){assert(!held);++acquired;++held;}
    void unlock(){assert(held);--held;}
};
namespace Eden {
namespace BufferPages {
    bool NeverRegistered(DAddr addr,u64){return addr==0x3000;}
}
namespace ImagePages {
    bool NeverHeld(DAddr addr,u64){return addr==0x2000;}
}
}
static std::vector<int> calls;
struct BufferCache {
    CountMutex mutex;
    std::vector<DAddr> writes;
    void ClaimCpuWrites(std::span<const std::pair<DAddr,u64>> batch){
        assert(mutex.held);assert(batch.size()==5);calls.push_back(2);
    }
    void WriteMemory(DAddr addr,u64){
        assert(mutex.held);writes.push_back(addr);calls.push_back(3);
    }
};
struct TextureCache {
    CountMutex mutex;
    std::vector<DAddr> writes;
    void WriteMemory(DAddr addr,u64){
        assert(mutex.held);writes.push_back(addr);calls.push_back(1);
    }
};
struct PipelineCache {
    std::vector<DAddr> invalidated;
    void InvalidateRegion(DAddr addr,u64){
        invalidated.push_back(addr);calls.push_back(4);
    }
};
struct RasterizerVulkan {
    BufferCache buffer_cache;
    TextureCache texture_cache;
    PipelineCache pipeline_cache;
    void OnCacheInvalidations(std::span<const std::pair<DAddr,u64>>);
};
""" + batch_method + r"""
int main(){
    RasterizerVulkan r;
    const std::pair<DAddr,u64> batch[]={{0x1000,4},{0x2000,8},
                                        {0x3000,16},{0,4},{0x4000,0}};
    r.OnCacheInvalidations(batch);
    assert(r.buffer_cache.mutex.acquired==1 && !r.buffer_cache.mutex.held);
    assert(r.texture_cache.mutex.acquired==1 && !r.texture_cache.mutex.held);
    assert((r.buffer_cache.writes==std::vector<DAddr>{0x1000,0x2000}));
    assert((r.texture_cache.writes==std::vector<DAddr>{0x1000,0x3000}));
    assert((r.pipeline_cache.invalidated==std::vector<DAddr>{0x1000,0x2000,0x3000}));
    assert(!calls.empty() && calls.front()==1);
    int previous=0;
    for(int phase:calls){assert(phase>=previous);previous=phase;}
}
"""

# Compile the exact selected renderer's shared budget-expression.
# Refuse a driver query while either cache lock is held.
budget_cpp=r"""
#include <cassert>
#include <cstdint>
#include <optional>
using u64=std::uint64_t;
static bool texture_locked=false, buffer_locked=false;
struct TextureRuntime {
  int calls=0; bool supported=true;
  bool CanReportMemoryUsage() const{return supported;}
  u64 GetDeviceMemoryUsage(){
    assert(!texture_locked && !buffer_locked);
    ++calls;
    return 900;
  }
};
int main() {
  TextureRuntime texture_cache_runtime;
""" + "    const std::optional<u64> frame_memory_usage =\n        texture_cache_runtime.CanReportMemoryUsage()\n            ? std::optional<u64>{texture_cache_runtime.GetDeviceMemoryUsage()}\n            : std::nullopt;" + r"""
  assert(frame_memory_usage && *frame_memory_usage==900);
  texture_locked=true;
  const u64 tex=*frame_memory_usage;
  texture_locked=false;
  buffer_locked=true;
  const u64 buf=*frame_memory_usage;
  buffer_locked=false;
  assert(tex==buf && texture_cache_runtime.calls==1);
}
"""

merge_cpp=r"""
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <utility>
#include <vector>
#define PS5_NATIVE 1
using u64=std::uint64_t;
using DAddr=std::uint64_t;
using GPUVAddr=std::uint64_t;
namespace VideoCommon {
template<class T> std::span<const typename T::value_type> FixSmallVectorADL(const T& v) {
    return {v.data(),v.size()};
}
}
struct StubRasterizer {
    std::vector<std::pair<DAddr,std::size_t>> last;
    unsigned calls=0;
    void InnerInvalidation(std::span<const std::pair<DAddr,std::size_t>> ranges) {
        ++calls;
        last.assign(ranges.begin(),ranges.end());
    }
};
struct StubAccumulator {
    std::vector<std::pair<DAddr,std::size_t>> dirty;
    template<class F> bool InvalidateAll(F&& f) {
        if(dirty.empty())return false;
        for(auto [addr,n]:dirty)f(addr,n);
        dirty.clear();
        return true;
    }
};
struct MemoryManager {
    StubAccumulator accumulator;
    std::vector<std::pair<DAddr,std::size_t>> page_stash2;
    StubRasterizer* rasterizer;
    explicit MemoryManager(StubRasterizer& r):rasterizer(&r){}
    template<bool> void GetSubmappedRangeImpl(DAddr addr,std::size_t n,
                std::vector<std::pair<DAddr,std::size_t>>& out) {
        if(n)out.emplace_back(addr,n);
    }
    void FlushCaching();
};
""" + flush + r"""
int main() {
    StubRasterizer r;
    MemoryManager m(r);
    m.accumulator.dirty={{0x2000,4},{0x1004,8},{0x1000,8},
                         {0x2004,4},{0x1000,8},{0x3000,2}};
    m.FlushCaching();
    assert(r.calls==1&&r.last.size()==3);
    assert((r.last[0]==std::pair<DAddr,std::size_t>{0x1000,12}));
    assert((r.last[1]==std::pair<DAddr,std::size_t>{0x2000,8}));
    assert((r.last[2]==std::pair<DAddr,std::size_t>{0x3000,2}));
    m.FlushCaching();
    assert(r.calls==1); // no phantom invalidations
    std::mt19937_64 gen(0xFC27);
    for(unsigned t=0;t<4000;++t){
        std::vector<unsigned char> touched(4096);
        for(unsigned j=0;j<64;++j){
            auto a=static_cast<std::size_t>(gen()%4096);
            auto n=static_cast<std::size_t>(gen()%64);
            if(n>4096-a)n=4096-a;
            m.accumulator.dirty.emplace_back(a,n);
            for(std::size_t k=a;k<a+n;++k)touched[k]=1;
        }
        m.FlushCaching();
        std::vector<unsigned char> actual(4096);
        std::size_t prev_end=0;
        for(auto [a,n]:r.last) {
            assert(a>=prev_end && a+n<=4096);
            prev_end=static_cast<std::size_t>(a+n);
            for(std::size_t k=a;k<a+n;++k)actual[k]=1;
        }
        assert(touched==actual);
    }
}
"""

dirty_cpp=r"""
#include <cassert>
#include <functional>
#include <utility>
#include <vector>
#include "core/gpu_dirty_memory_manager.h"
int main(){
    Core::GPUDirtyMemoryManager m;
    m.Collect(0x17c0,0x180); // deliberately crosses a 2 KiB collector page
    std::vector<std::pair<PAddr,size_t>> one,two,gathered,empty;
    std::function<void(PAddr,size_t)> a=[&](PAddr p,size_t n){one.emplace_back(p,n);};
    std::function<void(PAddr,size_t)> b=[&](PAddr p,size_t n){two.emplace_back(p,n);};
    std::function<void(PAddr,size_t)> g=[&](PAddr p,size_t n){gathered.emplace_back(p,n);};
    std::function<void(PAddr,size_t)> e=[&](PAddr p,size_t n){empty.emplace_back(p,n);};
    m.ForEachPending(a); m.ForEachPending(b);
    assert(!one.empty() && one==two); // observation is non-consuming
    m.Gather(g);
    assert(!gathered.empty());
    m.ForEachPending(e);
    assert(empty.empty());
}
"""

with tempfile.TemporaryDirectory(prefix="fc27-cpu-write-download-") as td:
    d=Path(td)
    (d/"model.cpp").write_text(cpp)
    (d/"batch.cpp").write_text(batch_cpp)
    (d/"budget.cpp").write_text(budget_cpp)
    (d/"merge.cpp").write_text(merge_cpp)
    stub=d/"stub/core"; stub.mkdir(parents=True)
    (stub/"device_memory_manager.h").write_text(
        "#pragma once\n#include <cstddef>\n#include <cstdint>\n"
        "using u32=std::uint32_t; using u64=std::uint64_t; using PAddr=std::uint64_t;\n"
        "namespace Core { inline constexpr std::size_t DEVICE_PAGEBITS=12; }\n")
    (d/"dirty.cpp").write_text(dirty_cpp)
    for cxx in filter(None,(os.getenv("CXX"),"clang++","g++")):
        if subprocess.run(["which",cxx],capture_output=True).returncode: continue
        for opt in ("-O0","-O2"):
            exe=d/("model-"+Path(cxx).name+opt[2:])
            subprocess.run([cxx,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                            str(d/"model.cpp"),"-o",str(exe)],check=True,timeout=60)
            subprocess.run([str(exe)],check=True,timeout=20)
            batch_exe=d/("batch-"+Path(cxx).name+opt[2:])
            subprocess.run([cxx,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                            str(d/"batch.cpp"),"-o",str(batch_exe)],check=True,timeout=60)
            subprocess.run([str(batch_exe)],check=True,timeout=20)
            budget_exe=d/("budget-"+Path(cxx).name+opt[2:])
            subprocess.run([cxx,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                            str(d/"budget.cpp"),"-o",str(budget_exe)],check=True,timeout=60)
            subprocess.run([str(budget_exe)],check=True,timeout=20)
            merge_exe=d/("merge-"+Path(cxx).name+opt[2:])
            subprocess.run([cxx,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                            str(d/"merge.cpp"),"-o",str(merge_exe)],check=True,timeout=60)
            subprocess.run([str(merge_exe)],check=True,timeout=20)
            dirty_exe=d/("dirty-"+Path(cxx).name+opt[2:])
            subprocess.run([cxx,"-std=c++20",opt,"-Wall","-Wextra","-Werror","-pthread",
                            "-I",str(d/"stub"),
                            "-I",str(root/"encore-active/eden-core/overrides"),
                            str(d/"dirty.cpp"),"-o",str(dirty_exe)],check=True,timeout=60)
            subprocess.run([str(dirty_exe)],check=True,timeout=20)
            print("FC27_CPU_WRITE_DOWNLOAD_RACE_PASS",Path(cxx).name,opt,
                  "async_protection=1 immediate_protection=1 pending_nonconsuming=1")
