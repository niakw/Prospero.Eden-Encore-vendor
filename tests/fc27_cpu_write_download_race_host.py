#!/usr/bin/env python3
"""Upstream #397 adaptation: newer guest CPU writes must beat stale GPU readback bytes."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
cache=(root/"encore-active/eden-core/src/video_core/buffer_cache/buffer_cache.h").read_text()
base=(root/"encore-active/eden-core/src/video_core/buffer_cache/buffer_cache_base.h").read_text()
dirty=(root/"encore-active/eden-core/overrides/core/gpu_dirty_memory_manager.h").read_text()
rast=(root/"encore-active/eden-vulkan/vulkan_rasterizer.cpp").read_text()
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
            dirty_exe=d/("dirty-"+Path(cxx).name+opt[2:])
            subprocess.run([cxx,"-std=c++20",opt,"-Wall","-Wextra","-Werror","-pthread",
                            "-I",str(d/"stub"),
                            "-I",str(root/"encore-active/eden-core/overrides"),
                            str(d/"dirty.cpp"),"-o",str(dirty_exe)],check=True,timeout=60)
            subprocess.run([str(dirty_exe)],check=True,timeout=20)
            print("FC27_CPU_WRITE_DOWNLOAD_RACE_PASS",Path(cxx).name,opt,
                  "async_protection=1 immediate_protection=1 pending_nonconsuming=1")
