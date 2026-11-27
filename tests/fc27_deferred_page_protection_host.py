#!/usr/bin/env python3
"""FC27 BufferCache page-protection ownership gate (host only).

This validates the PS5 adaptation of Lichii #1518: guest CPU writes update
BufferCache word state under the cache mutex, but page UNTRACK protection
changes are applied after that mutex is released. GPU-worker writes remain
synchronous, +1 tracking is never deferred, and DeviceMemoryManager::Unmap
waits for every deferred update before recycling backing.

This is source/host validation only. It is not PS5 FC27 gameplay evidence.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RASTER = ROOT / "encore-active/eden-vulkan/vulkan_rasterizer.cpp"
DEVICE_H = ROOT / "encore-active/eden-core/overrides/core/device_memory_manager.h"
DEVICE_INC = ROOT / "encore-active/eden-core/overrides/core/device_memory_manager.inc"
WORD = ROOT / "encore-active/eden-core/overrides/buffer-hotpath/video_core/buffer_cache/word_manager.h"
GPU_THREAD_H = ROOT / "encore-active/eden-gpu/include/video_core/gpu_thread.h"
GPU_THREAD_QUIET = ROOT / "encore-active/eden-gpu/gpu_thread_quiet.cpp"
GPU_THREAD_DEV = ROOT / "encore-active/eden-gpu/gpu_thread_dev.cpp"

for p in (RASTER, DEVICE_H, DEVICE_INC, WORD, GPU_THREAD_H,
          GPU_THREAD_QUIET, GPU_THREAD_DEV):
    assert p.exists(), p

raster = RASTER.read_text()
device_h = DEVICE_H.read_text()
device_inc = DEVICE_INC.read_text()
word = WORD.read_text()
gpu_thread_h = GPU_THREAD_H.read_text()
gpu_thread_quiet = GPU_THREAD_QUIET.read_text()
gpu_thread_dev = GPU_THREAD_DEV.read_text()

# The maintained PS5 header owns the epoch; no dormant side header/flag.
for marker in (
    "void BeginDeferredCachedUpdates() noexcept",
    "deferred_cached_updates.fetch_add(1, std::memory_order_relaxed)",
    "void EndDeferredCachedUpdates() noexcept",
    "deferred_cached_updates.fetch_sub(1, std::memory_order_release)",
    "std::atomic<u32> deferred_cached_updates{};",
):
    assert marker in device_h, marker
assert not (ROOT / "encore-active/eden-core/overrides/core/deferred_cached_updates.h").exists()

# Unmap invalidates cache ownership first, then waits for page-count publication,
# then takes mapping_guard and recycles the backing.
unmap = device_inc.split(
    "void DeviceMemoryManager<Traits>::Unmap(DAddr address, size_t size) {", 1
)[1].split("\ntemplate <typename Traits>", 1)[0]
for marker in (
    "device_inter->InvalidateRegion(address, size);",
    "deferred_cached_updates.load(std::memory_order_acquire)",
    "std::this_thread::yield();",
    "std::scoped_lock lk(mapping_guard);",
):
    assert marker in unmap, marker
assert unmap.index("device_inter->InvalidateRegion(address, size);") < unmap.index(
    "deferred_cached_updates.load(std::memory_order_acquire)"
) < unmap.index("std::scoped_lock lk(mapping_guard);")

# Word state changes stay under BufferCache ownership; only -1 page untracks can
# be collected. A +1 request is an assertion and still falls through to the
# immediate UpdatePagesCachedBatch path.
for marker in (
    "class DeferredPageCaching",
    "inline thread_local DeferredPageCaching* t_deferred_page_caching{};",
    'ASSERT_MSG(delta < 0,',
    "t_deferred_page_caching->Add(",
    "tracker->UpdatePagesCachedBatch(coalesced, delta);",
):
    assert marker in word, marker
apply = word.split(
    "void ApplyCollectedRanges(std::vector<std::pair<VAddr, u64>>& ranges, int delta) const {",
    1,
)[1].split("\n    /// @brief Notify tracker", 1)[0]
assert apply.index("ASSERT_MSG(delta < 0") < apply.index(
    "t_deferred_page_caching->Add("
) < apply.rindex("tracker->UpdatePagesCachedBatch(coalesced, delta);")

# Encore has one GPU scheduler worker. It marks itself explicitly so only guest
# CPU threads may defer the protection syscall.
for marker in (
    "inline thread_local bool t_is_worker_thread{};",
    "bool IsWorkerThread() noexcept",
):
    assert marker in gpu_thread_h, marker
for owner in (gpu_thread_quiet, gpu_thread_dev):
    run = owner.split("void ThreadManager::RunWorker(", 1)[1].split(
        "void ThreadManager::SubmitList(", 1
    )[0]
    assert "t_is_worker_thread = true;" in run
    assert "SCOPE_EXIT { t_is_worker_thread = false; };" in run

start = raster.index("bool RasterizerVulkan::OnCPUWrite(DAddr addr, u64 size) {")
end = raster.index("\nvoid RasterizerVulkan::OnCacheInvalidations(", start)
method = raster[start:end]
for marker in (
    "static thread_local VideoCommon::DeferredPageCaching deferred;",
    "!VideoCommon::GPUThread::IsWorkerThread()",
    "VideoCommon::t_deferred_page_caching = &deferred;",
    "gpu_modified = buffer_cache.OnCPUWrite(addr, size);",
    "device_memory.BeginDeferredCachedUpdates();",
    "deferred.ApplyAll();",
    "device_memory.EndDeferredCachedUpdates();",
    "texture_cache.WriteMemory(addr, size);",
    "pipeline_cache.InvalidateRegion(addr, size);",
):
    assert marker in method, marker
assert "::Eden::GpuDeferredWrite" not in method
assert "EDEN_" not in method  # no runtime flag / title-specific activation
assert method.index("GuestCacheLock(buffer_cache.mutex, false)") < method.index(
    "VideoCommon::t_deferred_page_caching = &deferred;"
) < method.index("buffer_cache.OnCPUWrite(addr, size)")
assert method.index("device_memory.BeginDeferredCachedUpdates();") < method.index(
    "deferred.ApplyAll();"
) < method.index("device_memory.EndDeferredCachedUpdates();")
assert method.index("device_memory.EndDeferredCachedUpdates();") < method.index(
    "texture_cache.WriteMemory(addr, size);"
) < method.index("pipeline_cache.InvalidateRegion(addr, size);")

# Host model: the page-untrack callback must observe the BufferCache mutex
# released, +1 is never queued, and an unmap cannot pass the epoch while a
# deferred update is outstanding.
fixture = r"""
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

using VAddr = std::uint64_t;
using s32 = std::int32_t;

struct FakeTracker {
    bool* cache_locked{};
    int applied{};
    void UpdatePagesCachedBatch(
        std::span<const std::pair<VAddr, std::size_t>> ranges, s32 delta) {
        assert(cache_locked && !*cache_locked);
        assert(delta == -1);
        assert(!ranges.empty());
        ++applied;
    }
};

class DeferredPageCaching {
public:
    using Ranges = std::vector<std::pair<VAddr, std::size_t>>;
    using Apply = void (*)(void*, std::span<const std::pair<VAddr, std::size_t>>, s32);
    void Add(void* tracker, Apply apply, const Ranges& ranges, s32 delta) {
        assert(delta < 0);
        batches.push_back({tracker, apply, delta, collected.size(), ranges.size()});
        collected.insert(collected.end(), ranges.begin(), ranges.end());
    }
    void ApplyAll() {
        for (const auto& batch : batches) {
            batch.apply(batch.tracker,
                std::span<const std::pair<VAddr, std::size_t>>(collected)
                    .subspan(batch.begin, batch.count),
                batch.delta);
        }
        batches.clear();
        collected.clear();
    }
    bool Empty() const { return batches.empty(); }
private:
    struct Batch {
        void* tracker;
        Apply apply;
        s32 delta;
        std::size_t begin;
        std::size_t count;
    };
    std::vector<Batch> batches;
    Ranges collected;
};

int main() {
    bool cache_locked = true;
    FakeTracker tracker{&cache_locked};
    DeferredPageCaching deferred;
    DeferredPageCaching::Ranges ranges{{0x1000, 0x2000}};
    deferred.Add(
        &tracker,
        [](void* p, std::span<const std::pair<VAddr, std::size_t>> r, s32 d) {
            static_cast<FakeTracker*>(p)->UpdatePagesCachedBatch(r, d);
        },
        ranges, -1);
    assert(!deferred.Empty());

    std::atomic<unsigned> epoch{1};
    std::atomic<bool> unmapped{};
    std::thread unmapper([&] {
        while (epoch.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        unmapped.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(!unmapped.load(std::memory_order_acquire));
    cache_locked = false;
    deferred.ApplyAll();
    assert(tracker.applied == 1);
    epoch.fetch_sub(1, std::memory_order_release);
    unmapper.join();
    assert(unmapped.load(std::memory_order_acquire));
    assert(deferred.Empty());
}
"""

compilers = list(dict.fromkeys(filter(
    None, (os.environ.get("CXX"), shutil.which("clang++"), shutil.which("g++"))
)))
assert compilers, "host C++ compiler required"
with tempfile.TemporaryDirectory(prefix="fc27-page-untrack-") as temp:
    code = Path(temp) / "test.cpp"
    code.write_text(fixture)
    for compiler in compilers:
        for opt in ("-O0", "-O2"):
            binary = Path(temp) / (Path(compiler).name + opt)
            subprocess.run([
                compiler, "-std=c++20", opt, "-Wall", "-Wextra", "-Werror",
                "-fsanitize=undefined", "-fno-sanitize-recover=all",
                str(code), "-pthread", "-o", str(binary)
            ], check=True, timeout=45)
            subprocess.run([str(binary)], check=True, timeout=10)

print(
    "FC27_DEFERRED_PAGE_PROTECTION_HOST_PASS "
    "owner_gate=1 untrack_only=1 unmap_epoch=1 ps5=unverified"
)
