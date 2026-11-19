#!/usr/bin/env python3
"""FC27: source + concurrency witness for post-lock cached-page publication.

Ports the safety invariant from Lichii #1518 without GpuSplit: logical cache
word state stays under buffer_cache.mutex; only negative page-cache count
publication may leave the lock. A positive/zero update flushes older negatives
synchronously first. Device Unmap invalidates the cache, then waits for any
already-published deferred update before removing backing.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "encore-active/eden-core/overrides/core/deferred_cached_updates.h"
DEVICE = ROOT / "encore-active/eden-core/overrides/core/device_memory_manager.inc"
RASTER = ROOT / "encore-active/eden-vulkan/vulkan_rasterizer.cpp"

helper = HELPER.read_text(encoding="utf-8")
device = DEVICE.read_text(encoding="utf-8")
raster = RASTER.read_text(encoding="utf-8")

assert "inline thread_local Collector* active_collector{};" in helper
assert "inline std::atomic<u32> published_updates{0};" in helper
assert "if (flushing || delta >= 0 || ranges.empty())" in helper
assert "Flush();\n            return false;" in helper

batch = device.split("void DeviceMemoryManager<Traits>::UpdatePagesCachedBatch(", 1)[1].split(
    "\n}\n\n} // namespace Core", 1
)[0]
assert batch.index("if (delta < 0)") < batch.index("deferred->TryAdd(")
try_add = batch.index("deferred->TryAdd(")\nassert try_add < batch.index("return;", try_add)
assert batch.index("} else if (!deferred->Empty())") < batch.index("deferred->Flush();")
assert batch.index("deferred->Flush();") < batch.index(
    "std::shared_lock registry_lk(process_registry_guard);"
)

unmap = device.split("void DeviceMemoryManager<Traits>::Unmap(", 1)[1].split(
    "\ntemplate <typename Traits>\nvoid DeviceMemoryManager<Traits>::TrackContinuityImpl", 1
)[0]
assert unmap.index("device_inter->InvalidateRegion(address, size);") < unmap.index(
    "::Eden::DeferredCachedUpdates::WaitForPublished();"
)
assert unmap.index("::Eden::DeferredCachedUpdates::WaitForPublished();") < unmap.index(
    "std::scoped_lock lk(mapping_guard);"
)

cpu = raster.split("bool RasterizerVulkan::OnCPUWrite(", 1)[1].split(
    "\nvoid RasterizerVulkan::OnCacheInvalidations", 1
)[0]
for needle in (
    "::Eden::DeferredCachedUpdates::Collector deferred;",
    "::Eden::DeferredCachedUpdates::active_collector = &deferred;",
    "::Eden::DeferredCachedUpdates::BeginPublish();",
    "::Eden::DeferredCachedUpdates::EndPublish();",
):
    assert needle in cpu
assert cpu.index("GuestCacheLock(buffer_cache.mutex, false)") < cpu.index(
    "::Eden::DeferredCachedUpdates::BeginPublish();"
)
assert cpu.index("buffer_cache.OnCPUWrite(addr, size)") < cpu.index(
    "::Eden::DeferredCachedUpdates::BeginPublish();"
)
assert cpu.index("::Eden::DeferredCachedUpdates::BeginPublish();") < cpu.index(
    "deferred.Flush();", cpu.index("if (publish_deferred)")
)
# The old all-write DEV queue remains a separate, unselected implementation.
assert "GpuDeferredWrite::TryEnqueue" not in cpu

fixture = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include "core/deferred_cached_updates.h"

using Eden::DeferredCachedUpdates::Collector;
using Eden::DeferredCachedUpdates::Range;

struct Tracker {
    std::vector<int> deltas;
    std::vector<DAddr> starts;
};

static void Apply(void* raw, std::span<const Range> ranges, s32 delta) {
    auto& tracker = *static_cast<Tracker*>(raw);
    tracker.deltas.push_back(delta);
    tracker.starts.push_back(ranges.front().first);
}

int main() {
    Tracker tracker;
    Collector collector;
    const Range first[]{{0x1000, 0x1000}};
    const Range second[]{{0x3000, 0x2000}};

    // Negative updates are held and preserve call order.
    assert(collector.TryAdd(&tracker, Apply, first, -1));
    assert(collector.TryAdd(&tracker, Apply, second, -1));
    assert(tracker.deltas.empty());
    collector.Flush();
    assert((tracker.deltas == std::vector<int>{-1, -1}));
    assert((tracker.starts == std::vector<DAddr>{0x1000, 0x3000}));
    assert(collector.Empty());

    // Tracking must never be deferred.
    assert(!collector.TryAdd(&tracker, Apply, first, +1));
    assert(collector.Empty());

    // Published updates block an Unmap-style waiter until EndPublish.
    using namespace Eden::DeferredCachedUpdates;
    BeginPublish();
    std::atomic<bool> entered{false};
    std::atomic<bool> released{false};
    std::thread waiter([&] {
        entered.store(true, std::memory_order_release);
        WaitForPublished();
        released.store(true, std::memory_order_release);
    });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(!released.load(std::memory_order_acquire));
    EndPublish();
    waiter.join();
    assert(released.load(std::memory_order_acquire));
    assert(published_updates.load(std::memory_order_acquire) == 0);
}
'''

compiler = shutil.which("clang++") or shutil.which("g++")
assert compiler, "C++20 compiler required"
with tempfile.TemporaryDirectory(prefix="fc27-deferred-pages-") as td:
    source = Path(td) / "test.cpp"
    exe = Path(td) / "test"
    source.write_text(fixture, encoding="utf-8")
    cmd = [
        compiler, "-std=c++20", "-O2", "-pthread",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-I", str(ROOT / "encore-active/eden-core/overrides"),
        "-I", str(ROOT / "sources/eden/src"),
        str(source), "-o", str(exe),
    ]
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stderr[-5000:]
    ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
    assert ran.returncode == 0, ran.stderr[-3000:]

print("FC27_DEFERRED_PAGE_PROTECTION_HOST_PASS negative_only=1 order=1 unmap_wait=1 fail_safe_positive=1")
print("Host/source proof only; PS5 FC27 frame-time improvement remains unverified")
