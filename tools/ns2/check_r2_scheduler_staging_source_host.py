#!/usr/bin/env python3
"""Compile exact R2 scheduler/Request() method bodies with bounded host doubles.

Source-method syntax and epoch semantics only: NOT full Vulkan includes, GPU
submission, allocation identity, resource retirement or PS5 validation.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCHED = ROOT / "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_scheduler.h"
STAGE = ROOT / "encore-active/eden-vulkan/vk_staging_buffer_pool.cpp"
SOURCE_BLOBS = {
    SCHED: "06ed61044892311993f4aa2f6b96a58f6ed22417",
    STAGE: "992d44db45352aa511cbddd0514ecb09a7806727",
}
TEMPLATE = r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
using u64 = std::uint64_t;
using VkRenderPass = std::uintptr_t;
struct Framebuffer {};
enum class MemoryUsage { Upload, Download };
struct StagingBufferRef {
    u64 request_generation{};
    u64 request_submission_tick{};
    int kind{};
};
class MockScheduler {
public:
    [[nodiscard]] u64 CurrentTick() const noexcept { return tick; }
@@SCHEDULER_METHODS@@
    u64 tick{5};
    u64 recorded_command_serial{30};
    struct State { VkRenderPass renderpass{}; } state{};
    struct DeferredClear { const Framebuffer* framebuffer{}; } deferred_clear{};
};
class StagingBufferPool {
public:
    MockScheduler scheduler{};
    std::size_t region_size{64};
    u64 request_generation_counter{};
    StagingBufferRef GetStreamBuffer(std::size_t) { return {.kind=1}; }
    StagingBufferRef GetStagingBuffer(std::size_t, MemoryUsage, bool) {
        return {.kind=2};
    }
    StagingBufferRef Request(std::size_t, MemoryUsage, bool deferred = false);
};
@@STAGING_REQUEST@@
int main() {
    int checks = 0;
    auto check = [&](bool ok) {
        ++checks;
        if (!ok) throw std::runtime_error("R2 real-owner test " + std::to_string(checks));
    };
    MockScheduler scheduler;
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
    std::cout << "PASS: " << checks
              << " exact scheduler/staging source-owner epoch cases\n";
}
"""

def extract_methods() -> tuple[str, str]:
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
    return scheduler_methods, request

def run() -> None:
    for path, sha in SOURCE_BLOBS.items():
        got = subprocess.check_output(
            ["git", "hash-object", str(path)], text=True).strip()
        assert got == sha, ("Unreviewed real R2 owner byte change", str(path), got)

    scheduler, request = extract_methods()
    content = (TEMPLATE.replace("@@SCHEDULER_METHODS@@", scheduler)
                       .replace("@@STAGING_REQUEST@@", request))
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
                       "-fno-omit-frame-pointer", str(source), "-o", str(exe)]
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
    print("PASS: 4 source-method sanitizer binaries; no PS5 build")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.parse_args()
    run()
