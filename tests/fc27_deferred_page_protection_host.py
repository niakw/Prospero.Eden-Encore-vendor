#!/usr/bin/env python3
"""FC27 #194 safety rollback: compile the exact synchronous rasterizer CPU-write owner.

This historical test path deliberately remains for the existing CI callers.
Do not re-enable deferred cached-page untracking without a PS5-safe scheduler
ownership gate matching upstream Lichii #1518 and console visual evidence.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
R=ROOT/"encore-active/eden/src/video_core/renderer_vulkan/vulkan_rasterizer.cpp"
D=ROOT/"encore-active/eden/src/core/device_memory_manager.inc"
T=ROOT/"encore-active/eden/src/video_core/texture_cache/texture_cache.h"
H=ROOT/"encore-active/eden/src/core/device_memory_manager.h"
W=ROOT/"encore-active/eden/src/video_core/buffer_cache/word_manager.h"
raster=R.read_text()
device=D.read_text()
header=H.read_text()
word=W.read_text()
texture=T.read_text()
assert "DeferredPageCaching" not in word
assert "t_deferred_page_caching" not in word
assert "deferred_cached_updates" not in header+device
assert "BeginDeferredCachedUpdates" not in header+raster
assert "EndDeferredCachedUpdates" not in header+raster
assert "std::this_thread::yield()" not in device
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
batch_start=device.index("void DeviceMemoryManager<Traits>::UpdatePagesCachedBatch")
batch_end=device.index("\n}\n\n} // namespace Core",batch_start)
batch=device[batch_start:batch_end]
for marker in (
    "bool separated = true;",
    "previous.first + previous.second >= current.first",
    "if (separated) {",
    "Common::ScopedRangeLock lk(counter_guard, lock_begin",
    "UpdatePagesCachedCountNoLock(addr, size, delta);",
):
    assert marker in batch, marker
# Malformed ranges are rejected before either the allocation-free or fallback path.
assert batch.index("length > device_as_size - start") < batch.index("bool separated = true;")
# Adjacent/overlapping/unordered inputs still reach the historical sort/coalesce fallback.
assert batch.index("if (separated) {") < batch.index("std::vector<std::pair<DAddr, size_t>> tmp")
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
