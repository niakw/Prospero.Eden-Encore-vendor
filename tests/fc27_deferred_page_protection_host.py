#!/usr/bin/env python3
"""FC27 #194 safety rollback: compile the exact synchronous rasterizer CPU-write owner.

This historical test path deliberately remains for the existing CI callers.
Do not re-enable deferred cached-page untracking without a PS5-safe scheduler
ownership gate matching upstream Lichii #1518 and console visual evidence.
"""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
R=ROOT/"encore-active/eden-vulkan/vulkan_rasterizer.cpp"
D=ROOT/"encore-active/eden-core/overrides/core/device_memory_manager.inc"
T=ROOT/"encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h"
def blob(path):
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\x00"+raw).hexdigest()
assert blob(R)=="49e7a97dfafffe8b5433b4c77bcc1b8ce0800cbb"
# The synchronous CPU write path is unchanged. Only InnerInvalidation now
# claims every CPU-written span before readback can replace a later span.
raster_text=R.read_text()
batch=raster_text.split("void RasterizerVulkan::InnerInvalidation(",1)[1].split(
    "\nbool RasterizerVulkan::OnCPUWrite(",1)[0]
assert batch.index("buffer_cache.ClaimCpuWrites(sequences);") < batch.index(
    "buffer_cache.WriteMemory(addr, size);")
assert blob(D)=="daf493ce6e863397ec44bfae467c4f7533570477"
assert blob(T)=="c7cb95ecaf388b925fc29582499bbd4b54a77f75"
# Selected NS1/NS2 TextureCache must retain the causal native protections.
texture_selected=T.read_text()
for needle in ("bool upload_completed = false;", "std::try_to_lock",
               "kMaxCompletedDecodesPerTick = 4",
               "return GetImageViewForFramebuffer(freshest_id);"):
    assert needle in texture_selected, ("selected texture protection lost",needle)
# The PS5-selected owner must also contain the previously shadow-only BCn 3D
# cancellation, allocation, and bounds protections, not just 2D decode fixes.
shadow=(ROOT/"encore-active/eden-vulkan/vulkan-cache/video_core/texture_cache/texture_cache.h").read_text()
method="void TextureCache<P>::TickAsyncUnswizzle() {"
def body(src):
    return src.split(method,1)[1].split("\n}\n",1)[0]
assert body(texture_selected)==body(shadow), "selected BCn 3D GPU unswizzle protections diverged"
for needle in ("invalid_slices", "staging.mapped_span.size() < task.total_size",
               "ImageFlagBits::CpuModified | ImageFlagBits::Remapped"):
    assert needle in body(texture_selected), needle
# Keep zero-fill elision scoped to the native async conversion input only.
alloc_start="void TextureCache<P>::QueueAsyncDecode(Image& image, ImageId image_id) {"
def native_allocation(src):
    block=src.split(alloc_start,1)[1]
    begin=block.index("    Common::ScratchBuffer<u8> local_unswizzle_data_buffer;")
    end=block.index("    Tegra::Memory::GpuGuestMemory<u8",begin)
    return block[begin:end]
assert native_allocation(texture_selected)==native_allocation(shadow)
assert "resize_destructive(image.unswizzled_size_bytes);" in native_allocation(texture_selected)
assert not (ROOT/"encore-active/eden-core/overrides/core/deferred_cached_updates.h").exists()
raster=R.read_text()
device=D.read_text()
texture=T.read_text()
assert "DeferredCachedUpdates" not in raster+device
assert "ImagePages::Mark(image.cpu_addr, image.guest_size_bytes);" in texture
assert "ImagePages::Mark(cpu_addr, size);" in texture
start=raster.index("bool RasterizerVulkan::OnCPUWrite(")
end=raster.index("\nvoid RasterizerVulkan::OnCacheInvalidations(",start)
method=raster[start:end]
assert "static thread_local" not in method
assert method.index("GuestCacheLock(buffer_cache.mutex, false)") < method.index("buffer_cache.OnCPUWrite(addr, size)")
assert method.index("buffer_cache.OnCPUWrite(addr, size)") < method.index("texture_cache.WriteMemory(addr, size)")
assert "if (buffer_cache.OnCPUWrite(addr, size)) {\n            return true;\n        }" in method
assert "ImagePages::NeverHeld" in method
assert "device_inter->InvalidateRegion(address, size);" in device
assert "WaitForPublished" not in device
assert "void DeviceMemoryManager<Traits>::UpdatePagesCachedBatch" in device
assert "TryAdd(" not in device

fixture=r"""
#include <cassert>
#include <atomic>
#include <cstdint>
#include <mutex>
using DAddr=std::uint64_t; using u64=std::uint64_t;
#define DEBUG_ASSERT(x) assert(x)
struct FakeMutex {
    bool held=false;
    void lock(){assert(!held);held=true;}
    void unlock(){assert(held);held=false;}
};
namespace Eden {
namespace Performance {
enum class CachePhase {GuestWriteBufferWait,GuestWriteBufferWork,
    GuestWriteTextureWait,GuestWriteTextureWork,GuestWriteShaderInvalidate};
struct BatchedGuestWriteTimer { ~BatchedGuestWriteTimer() {} };
bool ShouldSampleGuestWrite(){return false;}
struct CachePhaseTimer {
    CachePhaseTimer(CachePhase,bool){}
    void Stop(){}
};
void GuestCacheLock(FakeMutex& m,bool){m.lock();}
struct CpuAhead { bool load(std::memory_order) const{return false;} };
CpuAhead cpu_write_ahead;
}
namespace BufferPages {
bool skip=false;
bool NeverRegistered(DAddr,u64){return skip;}
}
namespace ImagePages {
bool skip=false;
bool NeverHeld(DAddr,u64){return skip;}
}
namespace BufferWriteAhead {
std::atomic<int> calls{};
template<class A,class B>
bool MarkFollowingPages(std::uint64_t,std::uint64_t,bool,A,B) {
    ++calls; return false;
}
}
}
struct BufferCache {
    FakeMutex mutex;
    int writes{};
    bool gpu_modified{};
    bool OnCPUWrite(DAddr,u64){assert(mutex.held);++writes;return gpu_modified;}
    bool IsRegionRegistered(DAddr,u64){assert(mutex.held);return true;}
};
struct TextureCache {
    FakeMutex mutex;
    int writes{};
    void WriteMemory(DAddr,u64){assert(mutex.held);++writes;}
};
struct PipelineCache {
    int invalidations{};
    void InvalidateRegion(DAddr,u64){++invalidations;}
};
struct RasterizerVulkan {
    BufferCache buffer_cache;
    TextureCache texture_cache;
    PipelineCache pipeline_cache;
    bool OnCPUWrite(DAddr,u64);
};
""" + method + r"""
int main(){
    RasterizerVulkan r;
    assert(!r.OnCPUWrite(0x4000,64));
    assert(r.buffer_cache.writes==1 && r.texture_cache.writes==1);
    assert(r.pipeline_cache.invalidations==1);
    assert(!r.buffer_cache.mutex.held && !r.texture_cache.mutex.held);
    r.buffer_cache.gpu_modified=true;
    assert(r.OnCPUWrite(0x4000,64));
    assert(r.buffer_cache.writes==2 && r.texture_cache.writes==1);
    assert(r.pipeline_cache.invalidations==1);
    r.buffer_cache.gpu_modified=false;
    Eden::BufferPages::skip=true;
    assert(!r.OnCPUWrite(0x5000,64));
    assert(r.buffer_cache.writes==2 && r.texture_cache.writes==2);
    assert(r.pipeline_cache.invalidations==2);
    Eden::ImagePages::skip=true;
    assert(!r.OnCPUWrite(0x6000,64));
    assert(r.texture_cache.writes==2 && r.pipeline_cache.invalidations==3);
    Eden::BufferPages::skip=false;
    assert(!r.OnCPUWrite(0x7000,64));
    assert(r.buffer_cache.writes==3 && r.texture_cache.writes==2);
    assert(r.pipeline_cache.invalidations==4);
    // First ordinary unmodified write and last registered write only.
    // GPU-modified early return and BufferPages::NeverRegistered shortcuts
    // correctly do not enter the write-ahead helper.
    assert(Eden::BufferWriteAhead::calls==2);
}
"""
compilers=list(dict.fromkeys(filter(None, (os.environ.get("CXX"),shutil.which("clang++"),shutil.which("g++")))))
assert compilers, "host C++ compiler required"
with tempfile.TemporaryDirectory(prefix="fc27-sync-page-") as temp:
    code=Path(temp)/"test.cpp"
    code.write_text(fixture)
    for compiler in compilers:
        for opt in ("-O0","-O2"):
            binary=Path(temp)/(Path(compiler).name+opt)
            subprocess.run([compiler,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                            "-fsanitize=undefined","-fno-sanitize-recover=all",
                            str(code),"-o",str(binary)],check=True,timeout=45)
            subprocess.run([str(binary)],check=True,timeout=10)
print("FC27_SYNCHRONOUS_PAGE_PROTECTION_HOST_PASS source=1 gpu_modified=1 texture_invalidate=1 ps5=unverified")
