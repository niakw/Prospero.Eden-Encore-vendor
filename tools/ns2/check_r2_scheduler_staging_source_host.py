#!/usr/bin/env python3
"""Compile exact R2 scheduler, staging Request, and typed-copy source bodies.

Exact source-derived methods in bounded host doubles (plus real helper header).
NOT a full native Vulkan include graph, GPU submission or PS5 validation.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCHED = ROOT / "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_scheduler.h"
SCHED_IMPL = ROOT / "encore-active/eden-vulkan/vulkan_scheduler.cpp"
STAGE = ROOT / "encore-active/eden-vulkan/vk_staging_buffer_pool.cpp"
CORE = ROOT / "encore-active/eden-core/src/video_core/buffer_cache/buffer_cache.h"
RUNTIME = ROOT / "encore-active/eden-vulkan/vk_buffer_cache_cost.cpp"
BOUNDS = ROOT / "encore-active/eden-vulkan/include/video_core/renderer_vulkan/ns2_staging_upload_bounds.h"
SOURCE_BLOBS = {
    SCHED: "d2d7e8b6cef6ea6ba912d5c7ad41b646527f6f0d",
    SCHED_IMPL: "db83d90460d93c8c50212753e51c54924f4f2e61",
    STAGE: "992d44db45352aa511cbddd0514ecb09a7806727",
    CORE: "4ae68723eb2bb2881ca53b7b5c1c1a60c7780d27",
    RUNTIME: "0b6e1e567cecf387c058f9f3301f876a1ecf31ca",
    BOUNDS: "65456cc3836c48ed569eb5d8aee1028dac1c6ca1",
}
TEMPLATE = r"""
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <utility>
#include <vector>
#include "ns2_staging_upload_bounds.h"
// The extracted method normally lives in namespace Vulkan.
namespace NS2Research = Vulkan::NS2Research;
using u64 = std::uint64_t;
using u8 = std::uint8_t;
using VkRenderPass = std::uintptr_t;
using VkBuffer = u64;
constexpr VkBuffer VK_NULL_HANDLE = 0;
namespace boost::container { template <class T, std::size_t N> using small_vector = std::vector<T>; }
namespace VideoCommon { struct BufferCopy { u64 src_offset{}, dst_offset{}, size{}; }; }
struct MockMemoryLocation { u64 memory{}, offset{}; };
struct Framebuffer {};
enum class MemoryUsage { Upload, Download };
struct StagingBufferRef {
    u64 request_generation{};
    u64 request_submission_tick{};
    VkBuffer buffer{};
    u64 offset{};
    std::span<u8> mapped_span{};
    MockMemoryLocation backing_location{};
    u64 backing_capacity{};
    int kind{};
};
struct MockCommandChunk {
    bool empty{};
    bool Empty() const noexcept { return empty; }
};
struct MockEventCV {
    int notifications{};
    void notify_all() { ++notifications; }
};
class Scheduler {
public:
    [[nodiscard]] u64 CurrentTick() const noexcept { return tick; }
@@SCHEDULER_METHODS@@
    u64 tick{5};
    u64 recorded_command_serial{30};
    u64 r2_dispatched_chunks{};
    std::unique_ptr<MockCommandChunk> chunk = std::make_unique<MockCommandChunk>();
    std::queue<std::unique_ptr<MockCommandChunk>> work_queue;
    bool worker_in_flight{};
    std::mutex queue_mutex;
    MockEventCV event_cv;
    void AcquireNewChunk() { chunk = std::make_unique<MockCommandChunk>(); }
    void DispatchWork();
    struct State { VkRenderPass renderpass{}; } state{};
    struct DeferredClear { const Framebuffer* framebuffer{}; } deferred_clear{};
};
class StagingBufferPool {
public:
    Scheduler scheduler{};
    std::size_t region_size{64};
    u64 request_generation_counter{};
    StagingBufferRef GetStreamBuffer(std::size_t) { return {.kind=1}; }
    StagingBufferRef GetStagingBuffer(std::size_t, MemoryUsage, bool) {
        return {.kind=2};
    }
    StagingBufferRef Request(std::size_t, MemoryUsage, bool deferred = false);
};
@@STAGING_REQUEST@@
@@DISPATCH_WORK@@
struct Buffer {
    struct NativeBackingSnapshot {
        VkBuffer handle{};
        u64 memory{}, binding_offset{}, capacity{};
    };
    NativeBackingSnapshot backing{22, 2002, 0, 512};
    [[nodiscard]] NativeBackingSnapshot GetNativeBackingSnapshot() const noexcept {
        return backing;
    }
};
class BufferCacheRuntime {
public:
    Scheduler scheduler{};
    std::vector<VideoCommon::BufferCopy> actual_copies{};
    std::size_t copy_calls{};
    bool actual_barrier{}, actual_reorder{};
    void CopyBuffer(VkBuffer destination, VkBuffer source,
                    std::span<const VideoCommon::BufferCopy> copies,
                    bool barrier, bool can_reorder_upload) {
        (void)destination;
        (void)source;
        ++copy_calls;
        actual_copies.assign(copies.begin(), copies.end());
        actual_barrier = barrier;
        actual_reorder = can_reorder_upload;
    }
    void CopyStagingUpload(const Buffer& destination, const StagingBufferRef& staging,
                           std::span<const VideoCommon::BufferCopy> copies, bool barrier,
                           bool can_reorder_upload);
};
@@RUNTIME_METHOD@@
int main() {
    int checks = 0;
    auto check = [&](bool ok) {
        ++checks;
        if (!ok) throw std::runtime_error("R2 real-owner test " + std::to_string(checks));
    };
    Scheduler scheduler;
    auto rec = scheduler.SnapshotR2Recording();
    check(scheduler.R2RecordingStillCurrent(rec));
    ++scheduler.recorded_command_serial;
    check(!scheduler.R2RecordingStillCurrent(rec));
    --scheduler.recorded_command_serial;
    check(scheduler.R2RecordingStillCurrent(rec));
    ++scheduler.tick;
    check(!scheduler.R2RecordingStillCurrent(rec));
    --scheduler.tick;
    scheduler.state.renderpass = 11;
    check(!scheduler.R2RecordingStillCurrent(rec));
    scheduler.state.renderpass = 0;
    Framebuffer fb;
    scheduler.deferred_clear.framebuffer = &fb;
    check(!scheduler.R2RecordingStillCurrent(rec));
    scheduler.deferred_clear.framebuffer = nullptr;
    check(scheduler.R2RecordingStillCurrent(rec));
    const auto before_empty_dispatch = scheduler.SnapshotR2Recording();
    scheduler.chunk->empty = true;
    scheduler.DispatchWork();
    check(scheduler.R2RecordingStillCurrent(before_empty_dispatch));
    check(scheduler.r2_dispatched_chunks == 0 && scheduler.work_queue.empty());
    scheduler.chunk->empty = false;
    scheduler.DispatchWork();
    check(!scheduler.R2RecordingStillCurrent(before_empty_dispatch));
    check(scheduler.r2_dispatched_chunks == 1 && scheduler.work_queue.size() == 1);
    check(scheduler.event_cv.notifications == 1);
    const auto before_inflight_dispatch = scheduler.SnapshotR2Recording();
    scheduler.worker_in_flight = true;
    scheduler.DispatchWork();
    check(!scheduler.R2RecordingStillCurrent(before_inflight_dispatch));
    check(scheduler.r2_dispatched_chunks == 2 && scheduler.work_queue.size() == 2);
    check(scheduler.event_cv.notifications == 1);

    StagingBufferPool pool;
    auto a = pool.Request(32, MemoryUsage::Upload);
    check(a.kind == 1 && a.request_generation == 1 && a.request_submission_tick == 5);
    auto b = pool.Request(65, MemoryUsage::Upload);
    check(b.kind == 2 && b.request_generation == 2 && b.request_submission_tick == 5);
    auto c = pool.Request(16, MemoryUsage::Download);
    check(c.kind == 2 && c.request_generation == 3);
    auto d = pool.Request(16, MemoryUsage::Upload, true);
    check(d.kind == 2 && d.request_generation == 4);
    pool.scheduler.tick = 6;
    auto e = pool.Request(16, MemoryUsage::Upload);
    check(e.request_submission_tick == 6 && e.request_generation == 5);
    check(a.request_submission_tick != e.request_submission_tick);
    pool.request_generation_counter = (std::numeric_limits<u64>::max)();
    bool rejected = false;
    try { (void)pool.Request(16, MemoryUsage::Upload); }
    catch (const std::overflow_error&) { rejected = true; }
    check(rejected);
    check(pool.request_generation_counter == (std::numeric_limits<u64>::max)());

    // Execute the real CopyStagingUpload() body, not an independently
    // rewritten test version. CopyBuffer() is a recording-only host double.
    std::array<u8, 512> memory{};
    StagingBufferRef upload{
        .request_generation = 7, .request_submission_tick = 5,
        .buffer = 11, .offset = 128,
        .mapped_span = std::span<u8>{memory.data(), 256},
        .backing_location = {1001, 0}, .backing_capacity = 512,
    };
    Buffer dst;
    BufferCacheRuntime runtime;
    std::array<VideoCommon::BufferCopy, 2> adjacent{{
        {128, 0, 16}, {144, 16, 16},
    }};
    runtime.CopyStagingUpload(dst, upload, adjacent, true, true);
    check(runtime.copy_calls == 1 && runtime.actual_copies.size() == 1);
    check(runtime.actual_copies[0].src_offset == 128 &&
          runtime.actual_copies[0].dst_offset == 0 &&
          runtime.actual_copies[0].size == 32);
    check(runtime.actual_barrier && runtime.actual_reorder);
    adjacent[1].dst_offset = 64;
    runtime.CopyStagingUpload(dst, upload, adjacent, true, true);
    check(runtime.actual_copies.size() == 2);
    check(runtime.actual_copies[0].src_offset == 128 &&
          runtime.actual_copies[1].dst_offset == 64);

    Buffer same_owner = dst;
    same_owner.backing.memory = upload.backing_location.memory;
    std::array<VideoCommon::BufferCopy, 2> aliased{{
        {128, 144, 8}, {136, 152, 16},
    }};
    runtime.CopyStagingUpload(same_owner, upload, aliased, true, true);
    check(runtime.actual_copies.size() == 2); // combined aliases GPU memory

    // 65 isolated regions exceed the 64-element bounded helper. The real
    // call must pass the entire untouched original list to CopyBuffer().
    std::array<u8, 2048> large_data{};
    StagingBufferRef large_upload = upload;
    large_upload.offset = 0;
    large_upload.mapped_span = large_data;
    large_upload.backing_capacity = 2048;
    Buffer large_destination = dst;
    large_destination.backing.capacity = 4096;
    std::vector<VideoCommon::BufferCopy> many;
    for (u64 i = 0; i < 65; ++i) many.push_back({i * 24, i * 24, 8});
    runtime.CopyStagingUpload(large_destination, large_upload, many, true, true);
    check(runtime.actual_copies.size() == many.size());
    check(runtime.actual_copies.front().size == 8 &&
          runtime.actual_copies.back().src_offset == 64 * 24);
    const auto recorded = runtime.copy_calls;
    std::array<VideoCommon::BufferCopy, 1> invalid{{{127, 0, 1}}};
    bool refused = false;
    try { runtime.CopyStagingUpload(dst, upload, invalid, true, true); }
    catch (const std::out_of_range&) { refused = true; }
    check(refused && runtime.copy_calls == recorded);
    upload.backing_location.memory = 0;
    refused = false;
    try { runtime.CopyStagingUpload(dst, upload, adjacent, true, true); }
    catch (const std::out_of_range&) { refused = true; }
    check(refused && runtime.copy_calls == recorded);
    upload.backing_location.memory = 1001;
    upload.request_submission_tick = 4;
    runtime.CopyStagingUpload(dst, upload, adjacent, true, true);
    check(runtime.actual_barrier && !runtime.actual_reorder);
    upload.request_submission_tick = 5;
    upload.request_generation = 0;
    runtime.CopyStagingUpload(dst, upload, adjacent, true, true);
    check(runtime.actual_barrier && !runtime.actual_reorder);
    upload.request_generation = 7;
    std::array<VideoCommon::BufferCopy, 1> zero{{{128, 0, 0}}};
    runtime.CopyStagingUpload(dst, upload, zero, true, true);
    check(runtime.actual_copies.empty() && runtime.actual_barrier);
    std::cout << "PASS: " << checks
              << " exact scheduler/staging source-owner epoch cases\n";
}
"""

def extract_methods() -> tuple[str, str, str, str]:
    scheduler = SCHED.read_text()
    start = scheduler.index(
        "#if defined(PS5_NATIVE) && defined(EDEN_RESEARCH_NS2_STAGE_BACKING)\n"
        "    // An owner-thread observation"
    )
    end = scheduler.index("\n#endif", start) + len("\n#endif")
    scheduler_methods = scheduler[start:end]
    assert scheduler_methods.count("SnapshotR2Recording()") == 1
    assert scheduler_methods.count("R2RecordingStillCurrent(") == 1

    stage = STAGE.read_text()
    start = stage.index("StagingBufferRef StagingBufferPool::Request(")
    end = stage.index("void StagingBufferPool::FreeDeferred(", start)
    request = stage[start:end]
    assert request.count("++request_generation_counter") == 1
    assert request.count("scheduler.CurrentTick()") == 1
    native = SCHED_IMPL.read_text()
    start = native.index("void Scheduler::DispatchWork() {")
    end = native.index("void Scheduler::BeginRenderPassImpl(", start)
    dispatch = native[start:end]
    assert dispatch.count("++r2_dispatched_chunks") == 1
    assert dispatch.count("work_queue.push(std::move(chunk))") == 1
    runtime = RUNTIME.read_text()
    start = runtime.index(
        "#if defined(PS5_NATIVE) && defined(EDEN_RESEARCH_NS2_STAGE_BACKING)\n"
        "void BufferCacheRuntime::CopyStagingUpload("
    )
    end = runtime.index("void BufferCacheRuntime::CopyBuffer(", start)
    typed_copy = runtime[start:end]
    assert typed_copy.count("NS2Research::StagingRegionCoalescer<64> fused") == 1
    assert typed_copy.count("CopyBuffer(dst.handle, staging.buffer") == 2
    return scheduler_methods, request, dispatch, typed_copy

def run() -> None:
    for path, sha in SOURCE_BLOBS.items():
        got = subprocess.check_output(
            ["git", "hash-object", str(path)], text=True).strip()
        assert got == sha, ("Unreviewed real R2 owner byte change", str(path), got)

    # The two real R2 typed call sites require an explicit copy barrier.
    # PS5's preexisting upload-CB branch requires !barrier, so this gate
    # MUST remain a provenance/epoch check, not silently enable batching.
    core = CORE.read_text()
    runtime = RUNTIME.read_text()
    assert core.count(
        "runtime.CopyStagingUpload(buffer, upload_staging, copies, true, can_reorder);"
    ) == 2, "R2 upload owner/barrier call graph drift"
    assert runtime.count(
        "src_buffer == staging_pool.StreamBuf() && can_reorder_upload && !barrier"
    ) == 1, "PS5 ordered barrier/reorder guard drift"
    assert runtime.count("NS2Research::StagingRegionCoalescer<64> fused;") == 1
    assert runtime.count("fully_captured && fused.Size() < copies.size()") == 1
    assert runtime.count("barrier, may_reorder") == 2
    print("R2_TYPED_UPLOAD_STILL_ORDERED: callers=2 explicit_barriers=2 cross_call_batching=OFF in_call_region_fusion=ON")

    scheduler, request, dispatch, typed_copy = extract_methods()
    content = (TEMPLATE.replace("@@SCHEDULER_METHODS@@", scheduler)
                       .replace("@@STAGING_REQUEST@@", request)
                       .replace("@@DISPATCH_WORK@@", dispatch)
                       .replace("@@RUNTIME_METHOD@@", typed_copy))
    with tempfile.TemporaryDirectory(prefix="ns2-r2-source-owners-") as d:
        source = Path(d) / "source.cpp"
        source.write_text(content)
        for compiler in ("clang++", "g++"):
            for opt in ("0", "2"):
                exe = Path(d) / f"r2-{compiler.replace('+','p')}-O{opt}"
                cmd = [compiler, "-std=c++20", "-Wall", "-Wextra",
                       "-Werror", "-pedantic", f"-O{opt}",
                       "-DPS5_NATIVE=1", "-DEDEN_RESEARCH_NS2_STAGE_BACKING=1",
                       "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                       "-fno-omit-frame-pointer",
                       "-I", str(BOUNDS.parent), str(source), "-o", str(exe)]
                built = subprocess.run(cmd, capture_output=True, text=True)
                if built.returncode:
                    raise AssertionError(
                        "Real R2 method compile failed: " + " ".join(cmd) +
                        "\n" + built.stderr[-7000:])
                tested = subprocess.run([str(exe)], capture_output=True, text=True)
                if tested.returncode:
                    raise AssertionError(
                        f"R2 source-owner runtime failed {compiler} O{opt}:\n" +
                        tested.stdout[-2000:] + tested.stderr[-5000:])
                print(f"{compiler} -O{opt}: {tested.stdout.strip()}")
    print("PASS: 4 exact-method sanitizer binaries (scheduler, staging, typed-copy); no PS5 build")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.parse_args()
    run()
