#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
pipe=(R/"encore-active/eden/src/video_core/renderer_vulkan/vk_pipeline_cache_cost.cpp").read_text()
nv=(R/"encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_ctrl.cpp").read_text()

p0=pipe.index("PipelineCache::~PipelineCache()")
p1=pipe.index("\n}",p0)
body=pipe[p0:p1]
assert "Lichii 499e2e4" in body
assert "workers.WaitForRequests();" in body
assert "serialization_thread.WaitForRequests();" in body
assert body.index("workers.WaitForRequests();") < body.index("serialization_thread.WaitForRequests();")

start=nv.index("const auto check_failing = [&]()")
end=nv.index("\n    };",start)
wait=nv[start:end]
for marker in (
    "detailed_gpu_profile.load(std::memory_order_relaxed)",
    "host1x_syncpoint_manager.GetHostSyncpointValue(fence_id)",
    "auto lk = system.StallApplication();",
    "host1x_syncpoint_manager.WaitHost(fence_id, target_value);",
    "system.UnstallApplication();",
    "EDEN_NVHOST_STALL",
    "suspend_ns=%lld wait_ns=%lld",
):
    assert marker in wait, marker
assert wait.index("StallApplication") < wait.index("WaitHost")
assert wait.index("WaitHost") < wait.index("UnstallApplication")
print("FC27_PIPELINE_TEARDOWN_SYNCWAIT_SOURCE_PASS worker_drain=1 detailed_wait_split=1")
