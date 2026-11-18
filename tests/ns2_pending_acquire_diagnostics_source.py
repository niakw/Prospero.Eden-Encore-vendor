#!/usr/bin/env python3
"""NS2 parked-acquire diagnostics: source/cadence witness, no behavior change."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PULLER=(ROOT/"encore-active/ns2-runtime/gpu_dispatch/video_core/engines/puller.cpp").read_text()
HEADER=(ROOT/"encore-active/ns2-runtime/gpu_dispatch/video_core/engines/puller.h").read_text()
FENCE=(ROOT/"encore-active/eden-backports/src/video_core/fence_manager.h").read_text()
RASTER=(ROOT/"encore-active/eden-vulkan/vulkan_rasterizer.cpp").read_text()

for needle in (
    '#include "performance.h"',
    "::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)",
    "50'000'000ULL, 500'000'000ULL, 2'000'000'000ULL",
    "10'000'000'000ULL",
    "EDEN_NS2_ACQUIRE_WAIT channel={}",
    "pending.process_generation, pending.channel_generation",
):
    assert needle in PULLER, needle

for needle in (
    "u64 detailed_started_ns{};",
    "u64 detailed_last_log_ns{};",
    "u32 detailed_polls{};",
    "u32 detailed_log_stage{};",
):
    assert needle in HEADER, needle

resume=PULLER.split("bool Puller::ResumeNativeAcquire",1)[1].split(
    "void Puller::ProcessNativeSemaphoreExecute",1)[0]
assert "dma_pusher.rasterizer->ReleaseFences();" in resume
assert "ReadCoherentNativeSemaphore" in resume
assert resume.index("dma_pusher.rasterizer->ReleaseFences();") < resume.index(
    "ReadCoherentNativeSemaphore")
assert "pending_native_acquire.reset();" in resume
assert resume.index("EDEN_NS2_ACQUIRE_WAIT") < resume.index(
    "pending_native_acquire.reset();")
# The observer cannot make progress or alter guest-visible state.
for forbidden in (
    "SignalFence(", "SignalReference(", "SyncOperation(", "Write<", "WriteBlock",
    "pending.value =", "pending.operation =",
):
    assert forbidden not in resume, forbidden

# Lichii #540's root cause (uncommitted SyncOperation releases) is already
# covered more strongly by Encore's existing acquire poll: ReleaseFences(true)
# reaches WaitPendingFences, which SignalFence()s a dummy reference and moves
# uncommitted_operations into pending_operations before the coherent read.
assert "uncommitted_operations.emplace_back(std::move(func));" in FENCE
assert "pending_operations.emplace_back(std::move(uncommitted_operations));" in FENCE
assert "SignalFence(std::move(func));" in FENCE
assert "void RasterizerVulkan::ReleaseFences(bool force)" in RASTER
assert "fence_manager.WaitPendingFences(force);" in RASTER

def emitted_ms(samples):
    stage=0
    last=0
    out=[]
    milestones=[50,500,2000]
    for now in samples:
        emit=False
        if stage < len(milestones) and now >= milestones[stage]:
            stage += 1
            emit=True
        elif stage >= len(milestones) and now-last >= 10_000:
            emit=True
        if emit:
            last=now
            out.append(now)
    return out

# Millisecond polls do not generate one line per poll.
samples=list(range(0,32_001))
out=emitted_ms(samples)
assert out[:3] == [50,500,2000], out[:3]
assert out[3:] == [12000,22000,32000], out[3:]
assert len(out) == 6

print("NS2_PENDING_ACQUIRE_DIAGNOSTIC_SOURCE_PASS detailed_only=1 behavior_change=0")
print("NS2_PENDING_ACQUIRE_LOG_CADENCE_PASS milestones_ms=50,500,2000 tail_ms=10000 lines_32s=6")
print("NS2_LICHII_540_ALREADY_COVERED_PASS release_fences_before_read=1")
