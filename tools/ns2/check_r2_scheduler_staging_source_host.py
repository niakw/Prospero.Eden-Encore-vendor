#!/usr/bin/env python3
"""Compile exact R2 scheduler, staging reuse/retirement and typed-copy bodies.

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
STAGE_H = ROOT / "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_staging_buffer_pool.h"
CORE = ROOT / "encore-active/eden/src/video_core/buffer_cache/buffer_cache.h"
VK_BUFFER_H = ROOT / "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_buffer_cache.h"
RETIREMENT_H = ROOT / "encore-active/eden-vulkan/include/video_core/renderer_vulkan/ns2_gpu_retirement_hold.h"
RUNTIME = ROOT / "encore-active/eden-vulkan/vk_buffer_cache_cost.cpp"
BOUNDS = ROOT / "encore-active/eden-vulkan/include/video_core/renderer_vulkan/ns2_staging_upload_bounds.h"
SOURCE_BLOBS = {
    SCHED: "d2d7e8b6cef6ea6ba912d5c7ad41b646527f6f0d",
    SCHED_IMPL: "db83d90460d93c8c50212753e51c54924f4f2e61",
    STAGE: "87cf07f4522f7225610785f1908337f5eabdeb32",
    STAGE_H: "09152820218895c71bbbdf145a30db6d95559e7f",
    CORE: "b2ab2c539ae161fc4a9680d31258f6ea823e2517",
    VK_BUFFER_H: "88485385b530d678c2c4b4f161053569c723e270",
    RETIREMENT_H: "33e077863e503a956cabf2b99edb73f8967b89a8",
    RUNTIME: "fd3837717e8d3e3887c958bbd51c0bf91cd35a2f",
    BOUNDS: "8905d42732b2d5848aa49b5800340e031cd214c9",
}
TEMPLATE = r"""
#include <algorithm>
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
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>
#include "ns2_staging_upload_bounds.h"
#include "ns2_gpu_retirement_hold.h"
// The extracted method normally lives in namespace Vulkan.
namespace NS2Research = Vulkan::NS2Research;
#define ASSERT(condition) do { if (!(condition)) throw std::runtime_error("mock ASSERT"); } while (false)
namespace Common {
inline unsigned Log2Ceil(std::size_t v) { unsigned p=0; if (v <= 1) return 0; --v; while (v != 0) { v >>= 1; ++p; } return p; }
inline std::size_t AlignUp(std::size_t v, std::size_t a) { return ((v+a-1)/a)*a; }
}
constexpr std::uint64_t MAX_ALIGNMENT = 256;
using u64 = std::uint64_t;
using u8 = std::uint8_t;
using VkRenderPass = std::uintptr_t;
using VkBuffer = u64;
using VkDeviceMemory = u64;
using VkDeviceSize = u64;
constexpr VkBuffer VK_NULL_HANDLE = 0;
namespace boost::container { template <class T, std::size_t N> using small_vector = std::vector<T>; }
namespace VideoCommon { struct BufferCopy { u64 src_offset{}, dst_offset{}, size{}; }; }
struct MockMemoryLocation { u64 memory{}, offset{}; };
struct Framebuffer {};
enum class MemoryUsage { Upload, Download };
struct StagingBufferRef {
    // Preserve the exact maintained StagingBufferRef field order for source
    // C++20 designated initialization rather than weakening compiler checks.
    VkBuffer buffer{};
    u64 device_address{};
    u64 offset{};
    std::span<u8> mapped_span{};
    MemoryUsage usage{MemoryUsage::Upload};
    unsigned log2_level{};
    u64 index{};
    MockMemoryLocation backing_location{};
    u64 backing_capacity{};
    u64 request_generation{};
    u64 request_submission_tick{};
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
    struct GpuCompletion {
        u64 known_tick{};
        [[nodiscard]] u64 KnownGpuTick() const noexcept { return known_tick; }
    } gpu_completion{};
    GpuCompletion* shared_completion{};
    GpuCompletion& GetMasterSemaphore() noexcept {
        return shared_completion ? *shared_completion : gpu_completion;
    }
    const GpuCompletion& GetMasterSemaphore() const noexcept {
        return shared_completion ? *shared_completion : gpu_completion;
    }
    int finishes{};
    bool stale_after_finish{};
    void Finish() {
        ++finishes;
        gpu_completion.known_tick = stale_after_finish ? tick - 1 : tick;
        ++tick;
    }
    [[nodiscard]] u64 CurrentTick() const noexcept { return tick; }
    [[nodiscard]] bool IsFree(u64 submitted_tick) const noexcept {
        return submitted_tick != 0 && submitted_tick <= completed_gpu_tick;
    }
    u64 completed_gpu_tick{};
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
    std::size_t region_size{256};
    u64 request_generation_counter{};
    struct ActiveStagingRequest {
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        VkDeviceSize physical_begin{};
        VkDeviceSize bytes{};
        u64 generation{};
        u64 submission_tick{};
    };
    std::vector<ActiveStagingRequest> active_staging_requests{};
    u64 active_staging_tick{};
    struct StagingBuffer {
        u64 index{};
        u64 tick{};
        bool deferred{};
        StagingBufferRef Ref() const noexcept {
            return {.usage=MemoryUsage::Upload,.log2_level=5,.index=index,.kind=2};
        }
    };
    struct StagingBuffers {
        std::vector<StagingBuffer> entries{};
        std::size_t delete_index{}, iterate_index{};
    };
    using StagingBuffersCache = std::array<StagingBuffers, 65>;
    static constexpr std::size_t NUM_SYNCS = 16;
    std::array<u64, NUM_SYNCS> sync_ticks{};
    [[nodiscard]] bool AreRegionsActive(std::size_t begin, std::size_t end) const;
    struct MockStreamBuffer {
        VkBuffer handle{17};
        VkBuffer operator*() const noexcept { return handle; }
        MockMemoryLocation Location() const noexcept { return {4321,0}; }
    } stream_buffer{};
    u64 stream_buffer_address{0};
    std::array<u8,4096> stream_storage{};
    std::span<u8> stream_pointer{stream_storage};
    std::size_t stream_buffer_size{4096};
    std::size_t iterator{}, used_iterator{}, free_iterator{};
    [[nodiscard]] std::size_t Region(std::size_t offset) const noexcept {
        return offset / region_size;
    }
    StagingBuffersCache upload_cache{}, download_cache{};
    StagingBuffersCache& GetCache(MemoryUsage usage) {
        return usage == MemoryUsage::Upload ? upload_cache : download_cache;
    }
    std::optional<StagingBufferRef> TryGetReservedBuffer(std::size_t, MemoryUsage, bool);
    void FreeDeferred(StagingBufferRef&);
    void ReleaseLevel(StagingBuffersCache&, std::size_t);
    StagingBufferRef GetStreamBuffer(std::size_t);
    std::array<u8, 4096> fallback_storage{};
    u64 fallback_handle{100}, fallback_memory{6000};
    StagingBufferRef GetStagingBuffer(std::size_t size, MemoryUsage usage, bool = false) {
        if (size > fallback_storage.size()) throw std::out_of_range("mock staging allocation");
        return {
            .buffer = ++fallback_handle,
            .mapped_span = std::span<u8>{fallback_storage.data(), size},
            .usage = usage,
            .backing_location = {++fallback_memory, 0},
            .backing_capacity = fallback_storage.size(),
            .kind = 2,
        };
    }
    StagingBufferRef Request(std::size_t, MemoryUsage, bool deferred = false);
    [[nodiscard]] bool IsCurrentRequest(const StagingBufferRef&, u64 tick) const noexcept;
};
@@STAGING_REQUEST@@
@@STAGING_REUSE@@
@@STAGING_DEFERRED_FREE@@
@@STAGING_RETIRED_DELETE@@
@@STAGING_STREAM@@
@@STAGING_REGION_LIFETIME@@
@@DISPATCH_WORK@@
struct Buffer {
    static inline Buffer* last_moved_to{};
    struct NativeBackingSnapshot {
        VkBuffer handle{};
        u64 memory{}, binding_offset{}, capacity{};
    };
    NativeBackingSnapshot backing{22, 2002, 0, 512};
    int* owner_deletions{};
    Buffer()=default;
    Buffer(const Buffer&)=default;
    Buffer(Buffer&& other) noexcept
        : backing(other.backing),
          owner_deletions(std::exchange(other.owner_deletions,nullptr)) {
        last_moved_to = this; // test-only allocation recovery after simulated loss
    }
    ~Buffer() { if (owner_deletions) ++*owner_deletions; }
    [[nodiscard]] NativeBackingSnapshot GetNativeBackingSnapshot() const noexcept {
        return backing;
    }
};
class BufferCacheRuntime {
public:
    Scheduler scheduler{};
    StagingBufferPool staging_pool{};
    NS2Research::GpuRetirementHold<std::unique_ptr<Buffer>,256> deleted_gpu_buffers;
    ~BufferCacheRuntime() noexcept;
    void RetainDeletedBuffer(Buffer&);
    std::vector<VideoCommon::BufferCopy> actual_copies{};
    std::size_t copy_calls{};
    bool actual_barrier{}, actual_reorder{};
    void CopyBuffer(VkBuffer destination, VkBuffer source,
                    std::span<const VideoCommon::BufferCopy> copies,
                    bool barrier, bool can_reorder_upload) {
        (void)destination;
        (void)source;
        ++copy_calls;
        actual_copies.clear();
        // Match the real CopyBuffer(): zero-sized Vulkan regions are ignored.
        for (const auto& copy : copies)
            if (copy.size != 0) actual_copies.push_back(copy);
        actual_barrier = barrier;
        actual_reorder = can_reorder_upload;
    }
    void CopyStagingUpload(const Buffer& destination, const StagingBufferRef& staging,
                           std::span<const VideoCommon::BufferCopy> copies, bool barrier,
                           bool can_reorder_upload);
};
@@RUNTIME_LIFETIME@@
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
    check(a.buffer == 17 && a.mapped_span.size_bytes() == 32 &&
          a.request_generation == 1 && a.request_submission_tick == 5);
    auto b = pool.Request(257, MemoryUsage::Upload);
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

    // Two different uploads under the SAME recording tick are both valid.
    // Force a ring offset to recycle before GPU retirement: owner ledger
    // must fail CLOSED, then revoke the former token once completion is real.
    StagingBufferPool aba;
    const auto original = aba.Request(32, MemoryUsage::Upload);
    const auto neighbour = aba.Request(32, MemoryUsage::Upload);
    check(aba.IsCurrentRequest(original, 5) &&
          aba.IsCurrentRequest(neighbour, 5) &&
          original.offset != neighbour.offset);
    StagingBufferRef forged = original;
    forged.request_generation = neighbour.request_generation;
    check(!aba.IsCurrentRequest(forged, 5));
    aba.iterator = 0;
    aba.free_iterator = 0;
    aba.used_iterator = 0;
    // Hostile allocator witness deliberately corrupts the old ring tick.
    // Without this, the existing pool correctly chooses fallback staging,
    // and we would not exercise the new independent physical owner guard.
    aba.sync_ticks.fill(0);
    bool denied_overwrite = false;
    try { (void)aba.Request(32, MemoryUsage::Upload); }
    catch (const std::out_of_range&) { denied_overwrite = true; }
    check(denied_overwrite && aba.IsCurrentRequest(original, 5));
    // A rejected ledger admission must never advance stream ring state.
    // Previously a denied reuse left a cursor/tick partially committed.
    check(aba.iterator == 0 && aba.used_iterator == 0 &&
          aba.free_iterator == 0 && aba.sync_ticks[0] == 0);
    check(aba.request_generation_counter == neighbour.request_generation);
    aba.scheduler.completed_gpu_tick = 5;
    aba.iterator = aba.free_iterator = aba.used_iterator = 0;
    const auto replacement = aba.Request(32, MemoryUsage::Upload);
    check(replacement.request_generation > neighbour.request_generation);
    check(!aba.IsCurrentRequest(original, 5));
    check(aba.IsCurrentRequest(replacement, 5));
    check(aba.IsCurrentRequest(neighbour, 5));
    // Owner validation is tied to the actual buffer + physical span, not
    // just tick/generation, and invalidates on submission rollover.
    forged = replacement;
    ++forged.backing_location.offset;
    check(!aba.IsCurrentRequest(forged, 5));
    ++aba.scheduler.tick;
    check(!aba.IsCurrentRequest(replacement, 6));

    // A distinct submission tick MUST NOT discard a still in-flight prior
    // physical reservation. Corrupt the historical ring completion map to
    // force a reuse attempt; the new pool ledger is the independent guard.
    StagingBufferPool cross_tick;
    const auto pending_tick5 = cross_tick.Request(32, MemoryUsage::Upload);
    cross_tick.scheduler.tick = 6;
    cross_tick.iterator = cross_tick.used_iterator = cross_tick.free_iterator = 0;
    cross_tick.sync_ticks.fill(0);
    bool cross_tick_denied = false;
    try { (void)cross_tick.Request(32, MemoryUsage::Upload); }
    catch (const std::out_of_range&) { cross_tick_denied = true; }
    check(cross_tick_denied && cross_tick.active_staging_tick == 6);
    check(cross_tick.active_staging_requests.size() == 1);
    check(cross_tick.iterator == 0 && cross_tick.free_iterator == 0 &&
          cross_tick.sync_ticks[0] == 0);
    check(!cross_tick.IsCurrentRequest(pending_tick5, 6));
    cross_tick.scheduler.completed_gpu_tick = 5;
    const auto safe_tick6 = cross_tick.Request(32, MemoryUsage::Upload);
    check(safe_tick6.request_submission_tick == 6 &&
          safe_tick6.request_generation > pending_tick5.request_generation);
    check(cross_tick.IsCurrentRequest(safe_tick6, 6));
    check(cross_tick.active_staging_requests.size() == 1);
    cross_tick.scheduler.tick = 5;
    bool backwards_tick_denied = false;
    try { (void)cross_tick.Request(32, MemoryUsage::Upload); }
    catch (const std::out_of_range&) { backwards_tick_denied = true; }
    check(backwards_tick_denied && cross_tick.IsCurrentRequest(safe_tick6, 6));

    // Source-derived GPU retirement checks: an object in a pending tick
    // cannot be reused or erased, and deferred staging cannot be reused until
    // FreeDeferred stamps its submission tick and GPU completion reaches it.
    StagingBufferPool retiring;
    retiring.scheduler.tick = 10;
    retiring.scheduler.completed_gpu_tick = 8;
    auto& cache = retiring.GetCache(MemoryUsage::Upload);
    auto& slot = cache[5];
    slot.entries.push_back({.index=42,.tick=9,.deferred=false});
    check(!retiring.TryGetReservedBuffer(32,MemoryUsage::Upload,false).has_value());
    retiring.scheduler.completed_gpu_tick = 9;
    auto reused = retiring.TryGetReservedBuffer(32,MemoryUsage::Upload,false);
    check(reused.has_value() && reused->index == 42);
    check(slot.entries[0].tick == 10 && !slot.entries[0].deferred);
    check(!retiring.TryGetReservedBuffer(32,MemoryUsage::Upload,false).has_value());
    slot.entries.push_back({.index=43,.tick=~u64{},.deferred=true});
    check(!retiring.TryGetReservedBuffer(32,MemoryUsage::Upload,false).has_value());
    StagingBufferRef deferred{};
    deferred.usage = MemoryUsage::Upload;
    deferred.log2_level = 5;
    deferred.index = 43;
    retiring.FreeDeferred(deferred);
    check(!slot.entries[1].deferred && slot.entries[1].tick == 10);
    check(!retiring.TryGetReservedBuffer(32,MemoryUsage::Upload,false).has_value());
    // Validate the real ReleaseLevel source, with an already-retired object
    // in another slot and a pending one that must survive this scan.
    StagingBufferPool deleting;
    deleting.scheduler.completed_gpu_tick = 9;
    auto& level = deleting.GetCache(MemoryUsage::Upload)[5];
    level.entries.push_back({.index=55,.tick=8,.deferred=false});
    level.entries.push_back({.index=56,.tick=10,.deferred=false});
    deleting.ReleaseLevel(deleting.GetCache(MemoryUsage::Upload),5);
    check(level.entries.size()==1 && level.entries[0].index == 56);
    deleting.ReleaseLevel(deleting.GetCache(MemoryUsage::Upload),5);
    check(level.entries.size()==1 && level.entries[0].index == 56);
    deleting.scheduler.completed_gpu_tick=10;
    deleting.ReleaseLevel(deleting.GetCache(MemoryUsage::Upload),5);
    check(level.entries.empty());
    // Reproduce a first failed stream wrap into still-busy region zero.
    // The follow-up Request must not overwrite the rejected mapped region,
    // even though the historical free_iterator+1 probe skips it.
    StagingBufferPool wrap;
    wrap.scheduler.tick = 12;
    wrap.scheduler.gpu_completion.known_tick = 10;
    wrap.sync_ticks[0] = 11;
    wrap.iterator = wrap.stream_buffer_size - 32;
    wrap.used_iterator = wrap.iterator;
    wrap.free_iterator = wrap.iterator;
    const auto first = wrap.Request(32,MemoryUsage::Upload);
    check(first.kind == 2 && wrap.iterator == 0 && wrap.free_iterator == 32);
    const auto second = wrap.Request(32,MemoryUsage::Upload);
    check(second.kind == 2 && wrap.iterator == 0);
    wrap.scheduler.gpu_completion.known_tick = 11;
    const auto resumed = wrap.Request(32,MemoryUsage::Upload);
    check(resumed.buffer == 17 && resumed.offset == 0);
    check(resumed.mapped_span.size_bytes() == 32);
    // An exactly exhausted ring used to compute begin()+17 on a
    // 16-entry array, and std::any_of could receive an inverted range.
    // Use the real GetStreamBuffer body under ASan/UBSan to verify both
    // the full boundary and a second denied request before GPU completion.
    StagingBufferPool full;
    full.scheduler.tick=18;
    full.scheduler.gpu_completion.known_tick=16;
    full.sync_ticks[0]=17;
    full.iterator=full.stream_buffer_size;
    full.free_iterator=full.stream_buffer_size;
    full.used_iterator=full.stream_buffer_size;
    const auto boundary=full.Request(32, MemoryUsage::Upload);
    check(boundary.kind==2 && full.iterator==0 && full.free_iterator==32);
    const auto boundary_retry=full.Request(32, MemoryUsage::Upload);
    check(boundary_retry.kind==2 && full.iterator==0);
    full.scheduler.gpu_completion.known_tick=17;
    const auto boundary_resume=full.Request(32, MemoryUsage::Upload);
    check(boundary_resume.buffer==17 && boundary_resume.offset==0);

    // Execute the real CopyStagingUpload() body, not an independently
    // rewritten test version. CopyBuffer() is a recording-only host double.
    Buffer dst;
    BufferCacheRuntime runtime;
    runtime.staging_pool.iterator = 128;
    StagingBufferRef upload = runtime.staging_pool.Request(256, MemoryUsage::Upload);
    const u64 current_upload_generation = upload.request_generation;
    check(upload.offset == 128 && runtime.staging_pool.IsCurrentRequest(upload, 5));
    std::array<VideoCommon::BufferCopy, 2> adjacent{{
        {128, 0, 16}, {144, 16, 16},
    }};
    std::array<VideoCommon::BufferCopy, 1> one_region{{{128, 0, 16}}};
    runtime.CopyStagingUpload(dst, upload, one_region, true, true);
    check(runtime.copy_calls == 1 && runtime.actual_copies.size() == 1 &&
          runtime.actual_copies.front().size == 16 && runtime.actual_barrier);
    runtime.CopyStagingUpload(dst, upload, adjacent, true, true);
    check(runtime.copy_calls == 2 && runtime.actual_copies.size() == 1);
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
    // A command whose destination intervals overlap must not be fused,
    // including with two different VkDeviceMemory objects.
    std::array<VideoCommon::BufferCopy, 2> waw{{
        {128, 0, 8}, {160, 4, 8},
    }};
    runtime.CopyStagingUpload(dst, upload, waw, true, true);
    check(runtime.actual_copies.size() == 2);
    check(runtime.actual_copies[1].dst_offset == 4);
    // No fusion when an earlier write aliases a later source under the same
    // VkDeviceMemory, even though each region is individually valid.
    std::array<VideoCommon::BufferCopy, 2> union_hazard{{
        {128, 0, 16}, {144, 128, 16},
    }};
    runtime.CopyStagingUpload(same_owner, upload, union_hazard, true, true);
    check(runtime.actual_copies.size() == 2);
    check(runtime.actual_copies[1].src_offset == 144);


    // 65 isolated regions exceed the 64-element bounded helper. The real
    // call must pass the entire untouched original list to CopyBuffer().
    StagingBufferRef large_upload = runtime.staging_pool.Request(2048, MemoryUsage::Upload);
    check(runtime.staging_pool.IsCurrentRequest(large_upload, 5));
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
    upload.backing_location.memory = runtime.staging_pool.stream_buffer.Location().memory;
    const auto before_invalid_epoch = runtime.copy_calls;
    upload.request_submission_tick = 4;
    bool stale_epoch_refused = false;
    try { runtime.CopyStagingUpload(dst, upload, adjacent, true, true); }
    catch (const std::out_of_range&) { stale_epoch_refused = true; }
    check(stale_epoch_refused && runtime.copy_calls == before_invalid_epoch);
    upload.request_submission_tick = 5;
    upload.request_generation = 0;
    stale_epoch_refused = false;
    try { runtime.CopyStagingUpload(dst, upload, adjacent, true, true); }
    catch (const std::out_of_range&) { stale_epoch_refused = true; }
    check(stale_epoch_refused && runtime.copy_calls == before_invalid_epoch);
    upload.request_generation = current_upload_generation;
    std::array<VideoCommon::BufferCopy, 1> zero{{{128, 0, 0}}};
    runtime.CopyStagingUpload(dst, upload, zero, true, true);
    check(runtime.actual_copies.empty() && runtime.actual_barrier);
    int destroyed_owners=0;
    {
        BufferCacheRuntime deleting;
        deleting.scheduler.tick=11;
        Buffer doomed;
        doomed.owner_deletions=&destroyed_owners;
        deleting.RetainDeletedBuffer(doomed);
        check(deleting.deleted_gpu_buffers.Outstanding()==1 && destroyed_owners==0);
        check(deleting.deleted_gpu_buffers.RetireCompleted(10)==0 && destroyed_owners==0);
        // Source-derived destructor must submit and wait before collection.
    }
    check(destroyed_owners==1);
    {
        // One source-derived staging ring predicate and one real retained
        // destination owner, both observing the SAME completed timeline.
        int joint_destroyed = 0;
        BufferCacheRuntime destination;
        StagingBufferPool stage;
        stage.scheduler.shared_completion = &destination.scheduler.gpu_completion;
        destination.scheduler.tick = 51;
        stage.sync_ticks[3] = 51;
        Buffer owner;
        owner.owner_deletions = &joint_destroyed;
        destination.RetainDeletedBuffer(owner);
        check(stage.AreRegionsActive(3,4));
        check(destination.deleted_gpu_buffers.RetireCompleted(50) == 0);
        check(joint_destroyed == 0);
        destination.scheduler.gpu_completion.known_tick = 50;
        check(stage.AreRegionsActive(3,4) && joint_destroyed == 0);
        destination.scheduler.gpu_completion.known_tick = 51;
        check(!stage.AreRegionsActive(3,4));
        check(destination.deleted_gpu_buffers.RetireCompleted(51) == 1);
        check(joint_destroyed == 1);
        // A later pending region blocks independently of the retired owner.
        stage.sync_ticks[5] = 52;
        check(stage.AreRegionsActive(5,6));
        destination.scheduler.gpu_completion.known_tick = 52;
        check(!stage.AreRegionsActive(5,6));
    }
    {
        BufferCacheRuntime deleting;
        deleting.scheduler.tick=33;
        for (int i=0;i<257;++i) {
            Buffer doomed;
            doomed.owner_deletions=&destroyed_owners;
            deleting.RetainDeletedBuffer(doomed);
        }
        check(deleting.scheduler.finishes==1);
        check(deleting.deleted_gpu_buffers.Outstanding()==0);
        check(destroyed_owners==258);
    }
    // Even when Finish() returns without throwing, an insufficient GPU
    // timeline must fail closed. Recover the deliberately abandoned test
    // owner manually so ASan/LSan still validates this exception path.
    int stale_destroyed=0;
    {
        BufferCacheRuntime deleting;
        deleting.scheduler.tick=45;
        for(int i=0;i<256;++i) {
            Buffer owner;
            owner.owner_deletions=&stale_destroyed;
            deleting.RetainDeletedBuffer(owner);
        }
        check(deleting.deleted_gpu_buffers.Outstanding()==256);
        deleting.scheduler.stale_after_finish=true;
        Buffer extra;
        extra.owner_deletions=&stale_destroyed;
        bool denied=false;
        try { deleting.RetainDeletedBuffer(extra); }
        catch(const std::runtime_error& ex) {
            denied=std::string(ex.what()).find("GPU completion did not reach")!=std::string::npos;
        }
        check(denied);
        check(stale_destroyed==0 && deleting.deleted_gpu_buffers.Outstanding()==256);
        Buffer* abandoned = Buffer::last_moved_to;
        check(abandoned != nullptr);
        deleting.scheduler.stale_after_finish=false;
        deleting.scheduler.gpu_completion.known_tick=45;
        check(deleting.deleted_gpu_buffers.RetireCompleted(45)==256);
        delete abandoned; // simulated device recovery only; NOT production
        check(stale_destroyed==257);
    }
    std::cout << "PASS: " << checks
              << " exact scheduler/staging/retirement source-owner epoch cases\n";
}
"""

def extract_methods() -> tuple[str, str, str, str, str, str, str, str, str, str]:
    scheduler = SCHED.read_text()
    start = scheduler.index(
        "#if defined(PS5_NATIVE) && defined(EDEN_NS2_STAGE_BACKING)\n"
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
    free_start = stage.index("void StagingBufferPool::FreeDeferred(")
    free_end = stage.index("void StagingBufferPool::TickFrame()",free_start)
    free_deferred = stage[free_start:free_end]
    reuse_start = stage.index("std::optional<StagingBufferRef> StagingBufferPool::TryGetReservedBuffer(")
    reuse_end = stage.index("StagingBufferRef StagingBufferPool::CreateStagingBuffer(",reuse_start)
    reuse = stage[reuse_start:reuse_end]
    reclaim_start = stage.index("void StagingBufferPool::ReleaseLevel(")
    reclaim = stage[reclaim_start:stage.index("} // namespace Vulkan",reclaim_start)]
    assert reuse.count("scheduler.IsFree(entry.tick)") == 1
    assert free_deferred.count("it->tick = scheduler.CurrentTick()") == 1
    assert reclaim.count("scheduler.IsFree(entry.tick)") == 1
    stream_start = stage.index("StagingBufferRef StagingBufferPool::GetStreamBuffer(")
    stream_end = stage.index("bool StagingBufferPool::AreRegionsActive(", stream_start)
    stream = stage[stream_start:stream_end]
    assert stream.count("const size_t active_begin = (std::min)(Region(free_iterator) + 1, NUM_SYNCS)") == 1
    assert stream.count("active_begin < active_end && AreRegionsActive(active_begin, active_end)") == 1
    assert stream.count("if (iterator == 0 && free_iterator > 0 &&") == 1
    assert stream.count("AreRegionsActive(0, (std::min)(Region(size) + 1, NUM_SYNCS))") == 1
    active_region_start = stage.index("bool StagingBufferPool::AreRegionsActive(")
    active_region_end = stage.index("StagingBufferRef StagingBufferPool::GetStagingBuffer(",
                                     active_region_start)
    active_region = stage[active_region_start:active_region_end]
    assert active_region.count("KnownGpuTick()") == 1
    assert active_region.count("gpu_tick < sync_tick") == 1
    native = SCHED_IMPL.read_text()
    start = native.index("void Scheduler::DispatchWork() {")
    end = native.index("void Scheduler::BeginRenderPassImpl(", start)
    dispatch = native[start:end]
    assert dispatch.count("++r2_dispatched_chunks") == 1
    assert dispatch.count("work_queue.push(std::move(chunk))") == 1
    runtime = RUNTIME.read_text()
    start = runtime.index(
        "#if defined(PS5_NATIVE) && defined(EDEN_NS2_STAGE_BACKING)\n"
        "void BufferCacheRuntime::CopyStagingUpload("
    )
    end = runtime.index("void BufferCacheRuntime::CopyBuffer(", start)
    typed_copy = runtime[start:end]
    lifetime_start=runtime.index(
        "#if defined(PS5_NATIVE) && defined(EDEN_NS2_STAGE_BACKING)\n"
        "BufferCacheRuntime::~BufferCacheRuntime() noexcept"
    )
    lifetime_end=runtime.index(
        "\n#endif\n\nvoid BufferCacheRuntime::TickFrame",lifetime_start
    ) + len("\n#endif")
    lifetime=runtime[lifetime_start:lifetime_end]
    assert lifetime.count("deleted_gpu_buffers.AbandonUnsafeOwners()") == 2
    assert lifetime.count("scheduler.Finish()") == 2
    assert "if (completed_tick < submit_tick)" in lifetime
    assert typed_copy.count("R2: stale staging reservation epoch") == 1
    assert typed_copy.count("staging_pool.IsCurrentRequest(") == 1
    assert request.count("active_staging_requests.push_back(") == 1
    assert "active_staging_requests.clear();" not in request
    assert request.count("scheduler.IsFree(old.submission_tick)") == 1
    assert request.count("invalid staging submission tick") == 1
    assert request.count("bool StagingBufferPool::IsCurrentRequest(") == 1
    assert STAGE_H.read_text().count("std::vector<ActiveStagingRequest> active_staging_requests") == 1
    assert "throw std::runtime_error(\"R2: GPU completion did not reach deletion tick\")" in lifetime
    assert typed_copy.count("NS2Research::StagingRegionCoalescer<64> fused") == 1
    assert typed_copy.count("CopyBuffer(dst.handle, staging.buffer") == 3
    return scheduler_methods, request, dispatch, typed_copy, reuse, free_deferred, reclaim, stream, active_region, lifetime

def run() -> None:
    # Production owners moved from the opt-in research macro to the exact
    # selected native stage gate. Normalize only that reviewed flag change.
    # The renderer-cache QueryCache UAF fix and upstream 3e6a5566 descriptor
    # fix are independently checked and reversed for archival R2 hash parity.
    new_cache_guard = "    // Rasterizer query cache is destroyed before the buffer cache. The latter\n    // may issue a final Scheduler::Finish() during its own teardown. Release\n    // this borrowed pointer once the rasterizer has drained its last queries.\n    void ClearQueryCache() noexcept {\n        query_cache = nullptr;\n    }\n\n"
    post_texel_owner = "        // In descriptor-buffer mode there is no VkBufferView: creating one\n        // before checking a missing address can attempt SurfaceFormat(255).\n        // Match Eden upstream 3e6a5566 while keeping legacy views intact.\n        if (guest_descriptor_queue.UsesDescriptorBuffer()) {\n            const VkDeviceAddress address = buffer.DeviceAddress();\n            const VkFormat vk_format = address == 0 ? VK_FORMAT_UNDEFINED : TexelBufferFormat(format);\n            guest_descriptor_queue.AddTexelBuffer(VK_NULL_HANDLE, address, offset, size, vk_format);\n        } else {\n            guest_descriptor_queue.AddTexelBuffer(buffer.View(offset, size, format));\n        }"
    pre_texel_owner = "        guest_descriptor_queue.AddTexelBuffer(buffer.View(offset, size, format),\n                                              buffer.DeviceAddress(), offset, size,\n                                              TexelBufferFormat(format));"
    import hashlib
    for path, sha in SOURCE_BLOBS.items():
        live = path.read_text()
        historical = live
        if path == RUNTIME:
            # Lichii #414 owns the now-bounded Quad LUT growth and retires
            # the previous VkBuffer after GPU completion. This is unrelated
            # to R2 staging uploads. Require a complete exact active SHA,
            # reverse ONLY these reviewed LUT changes on the audit copy and
            # still verify the original immutable R2 runtime blob below.
            current_blob = hashlib.sha1(
                b"blob " + str(len(live.encode())).encode() + bytes([0]) + live.encode()
            ).hexdigest()
            if current_blob == "873b5552b429f42294c8d410070df0572b4ff84c":
                new_quad = "    void UpdateBuffer(u32 num_indices_) {\n        // Lichii #414: the old LUT can remain referenced by recorded GPU\n        // commands. Reclaim it only after its real submission tick completes,\n        // rather than stalling all GPU work on every index-buffer growth.\n        while (!retired_buffers.empty() && scheduler.IsFree(retired_buffers.front().first)) {\n            retired_buffers.erase(retired_buffers.begin());\n        }\n        if (num_indices_ <= num_indices) {\n            return;\n        }\n\n        if (buffer) {\n            retired_buffers.emplace_back(scheduler.CurrentTick(), std::move(buffer));\n        }\n\n        // Geometric growth avoids repeatedly replacing the LUT for small\n        // draw-count increases. Do not overflow the guest-provided u32 count.\n        const u32 headroom = std::numeric_limits<u32>::max() - num_indices;\n        const u32 grown = num_indices + (std::min)(num_indices / 2, headroom);\n        num_indices = (std::max)(num_indices_, grown);\n        index_type = IndexTypeFromNumElements(device, num_indices);\n\n        const u32 num_quads = GetQuadsNum(num_indices);\n        const u32 num_triangle_indices = num_quads * 6;\n        const u32 num_first_offset_copies = 4;\n        const size_t bytes_per_index = BytesPerIndex(index_type);\n        const size_t size_bytes = num_triangle_indices * bytes_per_index * num_first_offset_copies;\n        const VkBufferCreateInfo buffer_ci = {\n            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,\n            .pNext = nullptr,\n            .flags = 0,\n            .size = size_bytes,\n            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,\n            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,\n            .queueFamilyIndexCount = 0,\n            .pQueueFamilyIndices = nullptr,\n        };\n        buffer = memory_allocator.CreateBuffer(buffer_ci, MemoryUsage::DeviceLocal);\n        if (device.HasDebuggingToolAttached()) {\n            buffer.SetObjectNameEXT(\"Quad LUT\");\n        }\n\n        const bool host_visible = buffer.IsHostVisible();\n        const StagingBufferRef staging = [&] {\n            if (host_visible) {\n                return StagingBufferRef{};\n            }\n            return staging_pool.Request(size_bytes, MemoryUsage::Upload);\n        }();\n\n        u8* staging_data = host_visible ? buffer.Mapped().data() : staging.mapped_span.data();\n        const size_t quad_size = bytes_per_index * 6;\n\n        for (u32 first = 0; first < num_first_offset_copies; ++first) {\n            for (u32 quad = 0; quad < num_quads; ++quad) {\n                MakeAndUpdateIndices(staging_data, quad_size, quad, first);\n                staging_data += quad_size;\n            }\n        }\n\n        if (!host_visible) {\n            scheduler.RequestOutsideRenderPassOperationContext();\n            scheduler.Record([src_buffer = staging.buffer, src_offset = staging.offset,\n                              dst_buffer = *buffer, size_bytes](vk::CommandBuffer cmdbuf) {\n                const VkBufferCopy copy{\n                    .srcOffset = src_offset,\n                    .dstOffset = 0,\n                    .size = size_bytes,\n                };\n                const VkBufferMemoryBarrier write_barrier{\n                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,\n                    .pNext = nullptr,\n                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,\n                    .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,\n                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,\n                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,\n                    .buffer = dst_buffer,\n                    .offset = 0,\n                    .size = size_bytes,\n                };\n                cmdbuf.CopyBuffer(src_buffer, dst_buffer, copy);\n                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,\n                                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, write_barrier);\n            });\n        } else {\n            buffer.Flush();\n        }\n    }\n\n"
                old_quad = "    void UpdateBuffer(u32 num_indices_) {\n        if (num_indices_ <= num_indices) {\n            return;\n        }\n\n        // The very first LUT has no previous device buffer to protect.\n        // Keep the original GPU fence when replacing an already-used buffer.\n        if (buffer) {\n            { auto timer = Eden::Performance::VulkanTimer(7); scheduler.Finish(); }\n        }\n\n        num_indices = num_indices_;\n        index_type = IndexTypeFromNumElements(device, num_indices);\n\n        const u32 num_quads = GetQuadsNum(num_indices);\n        const u32 num_triangle_indices = num_quads * 6;\n        const u32 num_first_offset_copies = 4;\n        const size_t bytes_per_index = BytesPerIndex(index_type);\n        const size_t size_bytes = num_triangle_indices * bytes_per_index * num_first_offset_copies;\n        const VkBufferCreateInfo buffer_ci = {\n            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,\n            .pNext = nullptr,\n            .flags = 0,\n            .size = size_bytes,\n            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,\n            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,\n            .queueFamilyIndexCount = 0,\n            .pQueueFamilyIndices = nullptr,\n        };\n        buffer = memory_allocator.CreateBuffer(buffer_ci, MemoryUsage::DeviceLocal);\n        if (device.HasDebuggingToolAttached()) {\n            buffer.SetObjectNameEXT(\"Quad LUT\");\n        }\n\n        const bool host_visible = buffer.IsHostVisible();\n        const StagingBufferRef staging = [&] {\n            if (host_visible) {\n                return StagingBufferRef{};\n            }\n            return staging_pool.Request(size_bytes, MemoryUsage::Upload);\n        }();\n\n        u8* staging_data = host_visible ? buffer.Mapped().data() : staging.mapped_span.data();\n        const size_t quad_size = bytes_per_index * 6;\n\n        for (u32 first = 0; first < num_first_offset_copies; ++first) {\n            for (u32 quad = 0; quad < num_quads; ++quad) {\n                MakeAndUpdateIndices(staging_data, quad_size, quad, first);\n                staging_data += quad_size;\n            }\n        }\n\n        if (!host_visible) {\n            scheduler.RequestOutsideRenderPassOperationContext();\n            scheduler.Record([src_buffer = staging.buffer, src_offset = staging.offset,\n                              dst_buffer = *buffer, size_bytes](vk::CommandBuffer cmdbuf) {\n                const VkBufferCopy copy{\n                    .srcOffset = src_offset,\n                    .dstOffset = 0,\n                    .size = size_bytes,\n                };\n                const VkBufferMemoryBarrier write_barrier{\n                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,\n                    .pNext = nullptr,\n                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,\n                    .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,\n                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,\n                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,\n                    .buffer = dst_buffer,\n                    .offset = 0,\n                    .size = size_bytes,\n                };\n                cmdbuf.CopyBuffer(src_buffer, dst_buffer, copy);\n                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,\n                                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, write_barrier);\n            });\n        } else {\n            buffer.Flush();\n        }\n    }\n\n"
                assert historical.count(new_quad) == 1
                historical = historical.replace(new_quad, old_quad, 1)
                assert historical.count("#include <limits>\n") == 1
                historical = historical.replace("#include <limits>\n", "", 1)
                retired_member = "    std::vector<std::pair<u64, vk::Buffer>> retired_buffers;\n"
                assert historical.count(retired_member) == 1
                historical = historical.replace(retired_member, "", 1)
        if path == CORE and hashlib.sha1(
            b"blob " + str(len(live.encode())).encode() + bytes([0]) + live.encode()
        ).hexdigest() == "6bbbae48f3c45fa5fd68d82940f5c5c508cbfd7b":
            # Four audited NS1 DMACopy/underflow hunks coexist with NS2 R2.
            # Reverse them in the audit copy only, and require byte-for-byte
            # parity with the NS2 main source before accepting the old R2 gate.
            ns1_reverse = (
                ("bool BufferCache<P>::DMACopy(\n    GPUVAddr src_address, GPUVAddr dest_address, u64 amount, bool* gpu_written,\n    boost::container::small_vector<std::pair<DAddr, size_t>, 4>* gpu_written_dest) {\n    if (gpu_written_dest) {\n        gpu_written_dest->clear();\n    }\n", "bool BufferCache<P>::DMACopy(GPUVAddr src_address, GPUVAddr dest_address, u64 amount,\n                             bool* gpu_written) {\n"),
                ("    // vkCmdCopyBuffer forbids overlapping source and destination DEVICE\n    // memory (VUID-vkCmdCopyBuffer-pRegions-00117). Distinct GMMU virtual\n    // addresses can alias one physical allocation, and different VkBuffer\n    // handles do not make those overlapping memory accesses legal.\n    // Reject BEFORE dirty-range bookkeeping, ClearDownload, or GPU commands:\n    // MaxwellDMA will read the authoritative source and snapshot the alias.\n    // Compare distances rather than forming a potentially overflowing end.\n    const DAddr source = *cpu_src_address;\n    const DAddr destination = *cpu_dest_address;\n    if ((source <= destination && destination - source < amount) ||\n        (destination < source && source - destination < amount)) {\n        return false;\n    }\n", ""),
                ("    if (gpu_written_dest) {\n        *gpu_written_dest = tmp_intervals;\n    }\n", ""),
                ("        // A stream near the start of physical memory may begin before\n        // min_page. Subtraction in the original test then wraps around and\n        // turns an ordinary backward leap into a huge invalid GPU buffer.\n        // Never move begin forwards past the range we already own.\n        if (begin <= min_page || add_value > begin - min_page) {\n            begin = (std::min)(begin, min_page);\n", "        if (add_value > begin - min_page) {\n            begin = min_page;\n"),
            )
            for maintained, earlier in ns1_reverse:
                assert historical.count(maintained) == 1, "Unexpected integrated BufferCache hunk"
                historical = historical.replace(maintained, earlier, 1)
            data = historical.encode()
            assert hashlib.sha1(
                b"blob " + str(len(data)).encode() + bytes([0]) + data
            ).hexdigest() == "41239f19b9e16741d49e31145c504e0086b1095e", "NS1/NS2 BufferCache reverse parity failed"

        if path == CORE:
            # The R2 historical owner is still immutable in SOURCE_BLOBS.
            # Current PS5 buffer_cache.h has subsequently gained CPU sibling
            # preservation, Lichii's page query optimization and complete
            # pending-CPU readback protection. Replacing its entire method
            # with the early R2 byte string no longer reconstructs this
            # source. Pin ALL current bytes and keep the important R2 and
            # FC27 runtime safety invariants. Unexpected drift remains red.
            current = hashlib.sha1(
                b"blob " + str(len(historical.encode())).encode() + bytes([0]) + historical.encode()
            ).hexdigest()
            if current == "41239f19b9e16741d49e31145c504e0086b1095e":
                assert live.count("page_has_gpu_writes") >= 3
                assert "IsRegionGpuModified(page_begin, page_end - page_begin)" in live
                assert "DownloadMemory(page_begin, page_end - page_begin);" in live
                assert "gpu_modified_ranges.Subtract(device_addr, size);" in live
                assert "void BufferCache<P>::ClaimCpuWrites" in live
                assert "BufferCache<P>::PendingCpuWriteRanges" in live
                assert "BufferCache<P>::WriteDownloadedMemory" in live
                assert "runtime.RetainDeletedBuffer(slot_buffers[buffer_id]);" in live
                assert "runtime.CopyStagingUpload(buffer, upload_staging, copies, true, can_reorder);" in live
                continue
        if path == CORE:
            # Restore the *exact* historical R2 WriteMemory body for its
            # immutable Git blob receipt. The later Lichii b13c4c1 sibling
            # preservation and 44bf47d single-query optimization are
            # separately verified by fc27_gpu_page_sibling_preservation_host.py.
            # Do not update the historical receipt or weaken its comparison.
            current_query = "!IsRegionGpuModified(page_begin, page_end - page_begin)"
            assert live.count(current_query) == 1, "Pinned RangeSet ABI adaptation missing"
            assert "!gpu_modified_ranges.Intersects(page_begin" not in live
            begin = historical.index("template <class P>\nvoid BufferCache<P>::WriteMemory(")
            finish = historical.index(
                "\ntemplate <class P>\nvoid BufferCache<P>::CachedWriteMemory", begin)
            new_method = historical[begin:finish]
            assert new_method.count("DownloadMemory(page_begin, page_end - page_begin);") == 1
            assert new_method.count("IsRegionGpuModified(page_begin, page_end - page_begin)") == 2
            assert new_method.index("DownloadMemory(page_begin") < new_method.rindex(
                "memory_tracker.MarkRegionAsCpuModified(device_addr, size);")
            historical_method = (
                "template <class P>\n"
                "void BufferCache<P>::WriteMemory(DAddr device_addr, u64 size) {\n"
                "    if (IsRegionGpuModified(device_addr, size)) {\n"
                "        ClearDownload(device_addr, size);\n"
                "        gpu_modified_ranges.Subtract(device_addr, size);\n"
                "    }\n"
                "    memory_tracker.MarkRegionAsCpuModified(device_addr, size);\n"
                "}\n"
            )
            historical = historical[:begin] + historical_method + historical[finish:]
        if path == CORE and "BufferCache<P>::ClaimCpuWrites" in historical:
            # FC27/Lichii #397 adaptation: keep the live stale-readback
            # protection, but reverse exactly that reviewed delta before
            # checking the immutable R2 owner receipt.
            helper_start = historical.index(
                "template <class P>\nvoid BufferCache<P>::ClaimCpuWrites")
            helper_end = historical.index(
                "template <class P>\nvoid BufferCache<P>::PopAsyncBuffers",
                helper_start)
            helper = historical[helper_start:helper_end]
            assert helper.count("BufferCache<P>::ClaimCpuWrites") == 1
            assert helper.count("BufferCache<P>::PendingCpuWriteRanges") == 1
            assert helper.count("BufferCache<P>::WriteDownloadedMemory") == 1
            historical = historical[:helper_start] + historical[helper_end:]

            async_guard = (
                "    // The fencing thread may land an older GPU snapshot while the guest CPU\n"
                "    // has already produced newer bytes. Preserve every pending CPU range.\n"
                "    const Common::RangeSet<DAddr> cpu_written = PendingCpuWriteRanges();\n"
            )
            assert historical.count(async_guard) == 1
            historical = historical.replace(async_guard, "", 1)

            async_after = (
                "            WriteDownloadedMemory(cpu_written, start,\n"
                "                                  &read_mapped_memory[start - device_addr], end - start);"
            )
            async_before = (
                "            device_memory.WriteBlockUnsafe(start, &read_mapped_memory[start - device_addr],\n"
                "                                           end - start);"
            )
            assert historical.count(async_after) == 1
            historical = historical.replace(async_after, async_before, 1)

            download_guard = (
                "    const Common::RangeSet<DAddr> cpu_written = PendingCpuWriteRanges();\n\n"
            )
            assert historical.count(download_guard) == 1
            historical = historical.replace(download_guard, "\n", 1)

            mapped_after = (
                "            WriteDownloadedMemory(cpu_written, copy_device_addr, copy_mapped_memory, copy.size);"
            )
            mapped_before = (
                "            device_memory.WriteBlockUnsafe(copy_device_addr, copy_mapped_memory, copy.size);"
            )
            assert historical.count(mapped_after) == 1
            historical = historical.replace(mapped_after, mapped_before, 1)

            immediate_after = (
                "            WriteDownloadedMemory(cpu_written, copy_device_addr, immediate_buffer.data(), copy.size);"
            )
            immediate_before = (
                "            device_memory.WriteBlockUnsafe(copy_device_addr, immediate_buffer.data(), copy.size);"
            )
            assert historical.count(immediate_after) == 1
            historical = historical.replace(immediate_after, immediate_before, 1)
        if path == SCHED:
            assert live.count(new_cache_guard) == 1, "NS1 QueryCache revocation lost"
            historical = historical.replace(new_cache_guard, "", 1)
        if path == VK_BUFFER_H:
            # No VkBufferView creation is valid for descriptor-buffer mode.
            # Preserve this upstream-compatible guard in the live source.
            assert live.count(post_texel_owner) == 1, "Descriptor-buffer fix lost"
            historical = historical.replace(post_texel_owner, pre_texel_owner, 1)
        if "defined(EDEN_NS2_STAGE_BACKING)" in historical:
            historical = historical.replace(
                "defined(EDEN_NS2_STAGE_BACKING)",
                "defined(EDEN_RESEARCH_NS2_STAGE_BACKING)")
        encoded = historical.encode("utf-8")
        got = hashlib.sha1(b"blob " + str(len(encoded)).encode() + bytes([0]) + encoded).hexdigest()
        assert got == sha, ("Unreviewed real R2 owner byte change", str(path), got)
    # Assert real gated buffer deletion and GPU retirement path integrity.
    core_owner = CORE.read_text()
    assert core_owner.count("runtime.RetainDeletedBuffer(slot_buffers[buffer_id]);") == 1
    assert core_owner.count("BufferCache<P>::~BufferCache() {") == 1
    assert core_owner.count("std::terminate();") == 1
    assert core_owner.count("runtime.Finish();") >= 1
    assert core_owner.index("BufferCache<P>::~BufferCache() {") < core_owner.index("BufferCache<P>::TickFrame()")
    # This is a source gate, NOT an actual full Vulkan object teardown/link proof.
    assert VK_BUFFER_H.read_text().count(
        "GpuRetirementHold<std::unique_ptr<Buffer>, 256> deleted_gpu_buffers") == 1
    runtime_lifetime = RUNTIME.read_text()
    assert runtime_lifetime.count(
        "void BufferCacheRuntime::RetainDeletedBuffer(Buffer& deleted)") == 1
    assert runtime_lifetime.count(
        "BufferCacheRuntime::~BufferCacheRuntime() noexcept") == 1
    assert runtime_lifetime.count("deleted_gpu_buffers.AbandonUnsafeOwners()") == 2
    assert runtime_lifetime.count(
        "scheduler.GetMasterSemaphore().KnownGpuTick()") >= 4
    assert "if (deleted_gpu_buffers.Outstanding())" in runtime_lifetime
    print("R2_GPU_OWNER_LIFETIME: exact gated deletion, retirement and shutdown sources pinned")

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
    assert runtime.count("barrier, may_reorder") == 3
    assert runtime.count("if (copies.size() < 2)") == 1
    print("R2_TYPED_UPLOAD_STILL_ORDERED: callers=2 explicit_barriers=2 cross_call_batching=OFF in_call_region_fusion=ON")

    scheduler, request, dispatch, typed_copy, reuse, free_deferred, reclaim, stream, active_region, lifetime = extract_methods()
    content = (TEMPLATE.replace("@@SCHEDULER_METHODS@@", scheduler)
                       .replace("@@STAGING_REQUEST@@", request)
                       .replace("@@STAGING_REUSE@@", reuse)
                       .replace("@@STAGING_DEFERRED_FREE@@", free_deferred)
                       .replace("@@STAGING_RETIRED_DELETE@@", reclaim)
                       .replace("@@STAGING_STREAM@@", stream)
                       .replace("@@STAGING_REGION_LIFETIME@@", active_region)
                       .replace("@@DISPATCH_WORK@@", dispatch)
                       .replace("@@RUNTIME_METHOD@@", typed_copy)
                       .replace("@@RUNTIME_LIFETIME@@", lifetime))
    with tempfile.TemporaryDirectory(prefix="ns2-r2-source-owners-") as d:
        source = Path(d) / "source.cpp"
        source.write_text(content)
        for compiler in ("clang++", "g++"):
            for opt in ("0", "2"):
                exe = Path(d) / f"r2-{compiler.replace('+','p')}-O{opt}"
                cmd = [compiler, "-std=c++20", "-Wall", "-Wextra",
                       "-Werror", "-pedantic", f"-O{opt}",
                       "-DPS5_NATIVE=1", "-DEDEN_NS2_STAGE_BACKING=1",
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
    print("PASS: 4 exact-method sanitizer binaries (scheduler, staging reuse/retire, typed-copy); no PS5 build")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.parse_args()
    run()
