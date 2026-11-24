#!/usr/bin/env python3
"""FC27 #194 safety rollback: compile the exact synchronous rasterizer CPU-write owner.

This historical test path deliberately remains for the existing CI callers.
Do not re-enable deferred cached-page untracking without a PS5-safe scheduler
ownership gate matching upstream Lichii #1518 and console visual evidence.
"""
from pathlib import Path
from native_msaa_handle_receipt import restore_integrated_rasterizer, INTEGRATED_RASTERIZER, GUEST_CACHE_LOCK_SUCCESSOR
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
# Current maintained runtime owners. The CPU-write rollback still requires
# synchronous protection even though DMA clean-image invalidation was added.
# NS2 adds only the strict Ampere syncpoint-fence wrapper to this TU; strip
# that exact method before replaying the historical FC27 rasterizer receipt.
NATIVE_SYNC_BLOCK="""#ifdef PS5_NATIVE
bool RasterizerVulkan::SignalNativeSyncPoint(u32 value) {
    fence_manager.SignalNativeSyncPoint(value);
    return true;
}
#endif

"""
raster_text=R.read_text()
def text_blob(text):
    raw=text.encode()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\x00"+raw).hexdigest()
# FC27 24/11 batching moves ClaimCpuWrites into the one buffer-cache lock.
# Reverse only that reviewed delta before replaying this older safety receipt.
if text_blob(raster_text)=="e4948333ed15fba8024aeb4ffaa143a2e010f991":
    newer="""    if (may_claim_buffer) {
        // Lichii 5e428570 / f5edeab: claim and apply the complete buffer batch
        // under ONE acquisition. Texture state was handled above and is independent.
        // Claim must precede BufferCache::WriteMemory so a sibling GPU download
        // cannot overwrite a later CPU write from this same collected batch.
        std::scoped_lock lock{buffer_cache.mutex};
        buffer_cache.ClaimCpuWrites(ranges);
        for (const auto& [addr, size] : ranges) {
"""
    older="""    if (may_claim_buffer) {
        std::scoped_lock lock{buffer_cache.mutex};
        for (const auto& [addr, size] : ranges) {
"""
    marker="    // The single-range invalidation callback takes the texture and buffer\n"
    first="""    if (may_claim_buffer) {
        std::scoped_lock lock{buffer_cache.mutex};
        buffer_cache.ClaimCpuWrites(ranges);
    }

"""
    assert raster_text.count(newer)==raster_text.count(marker)==1
    raster_text=raster_text.replace(newer,older,1).replace(marker,first+marker,1)
    assert text_blob(raster_text)=="e5baa7f438d8caf11327989087db38ed1ca053a4"
assert raster_text.count(NATIVE_SYNC_BLOCK)==1
fc27_raster=raster_text.replace(NATIVE_SYNC_BLOCK,"")
assert text_blob(fc27_raster)==GUEST_CACHE_LOCK_SUCCESSOR
restored=restore_integrated_rasterizer(fc27_raster)
assert text_blob(restored)=="71d097b9a83028dfad9370792c905bb3bfccfd5a"
assert blob(D)=="daf493ce6e863397ec44bfae467c4f7533570477"
# The selected NS1/NS2 TextureCache now includes 10 independently reviewed
# safety and DMA source changes; this full file SHA remains a fail-closed
# inventory, not a silent acceptance of a changed historic contract.
assert blob(T) in {
    "948960fad4d829ba874746acb6e353a8a2ed19f7",  # original integrated DMA owner
    "5c6d015cf207d63de31b577411972606145d9f2e",  # empty render-target guard
    "27ea4e0e665d3f94e173c1335cb19211efc03646",  # selected HUD/cache parity
}
if blob(T) == "27ea4e0e665d3f94e173c1335cb19211efc03646":
    for guard in ("if (it == channel_state->sampler_ids.end())",
                  "if (pair->second != NULL_IMAGE_VIEW_ID || remapped_cached_view)",
                  "if (removed_views.empty())"):
        assert guard in T.read_text(), guard
for selected_guard in ("SplitLayeredImageCopies(copies)", "index > table.current_limit || index >= table.descriptors.size()", "constexpr u32 MaxDescriptorTableLimit = (1U << 21) - 1;", "TextureCache<P>::WriteDmaCopy(", "gpu_written_dest", "copy_ns1_volume_slice", "if (!gpu_memory || gpu_memory->UsesNativeTiling()", "alias_conversion_format", "PixelFormat::X8_D24_UNORM", "bool upload_completed = false;", "kMaxCompletedDecodesPerTick = 4"):
    assert selected_guard in T.read_text(), ("FC27/NS2 active texture guard lost",selected_guard)
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
# The DMA fix may invalidate only guest-current destination images.
dma=raster.split("bool AccelerateDMA::BufferCopy(",1)[1].split(
    "\ntemplate <bool IS_IMAGE_UPLOAD>",1)[0]
assert "std::scoped_lock lock{buffer_cache.mutex, texture_cache.mutex};" in dma
assert dma.index("buffer_cache.DMACopy(") < dma.index("texture_cache.WriteGpuMemory(")
assert "if (!copied_gpu_bytes &&" in dma
assert "!texture_cache.IsGpuMemoryGpuModified(src_address, amount)" in dma
assert "!texture_cache.IsGpuMemoryGpuModified(dest_address, amount)" in dma
assert "bool may_claim_buffer = false;" in raster
# The GMMU may return mapped fragments while omitting holes. Re-uploading
# those spans must fail closed when any byte remains untranslatable.
assert "piece_addr - gpu_addr != covered" in texture
assert "piece_size > size - covered" in texture
assert "return covered != size || size == 0;" in texture
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
