#!/usr/bin/env python3
"""Source contract for bounded PS5 NS2 GPFIFO pre-submit diagnostics."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.cpp"
text = SRC.read_text()

# Observability must be opt-in and bounded. Normal gameplay does not consume
# counters or print these diagnostics.
assert 'eden_native_detailed_logging() noexcept __attribute__((weak))' in text
assert 'constexpr unsigned GpfifoTraceLimit = 32;' in text
assert 'if (!eden_native_detailed_logging || !eden_native_detailed_logging())' in text
assert text.count('NextDetailedGpfifoTrace(') >= 6

# Identify whether the guest reached the NVDRV route, wrapper-owned Base path,
# or SubmitGPFIFOImpl, without dumping guest command memory.
for route in ("ioctl1-submit", "ioctl1-kickoff", "ioctl2-kickoff",
              "base1-kickoff", "base1-submit", "base2-kickoff"):
    assert route in text, route
assert 'EDEN_NS2_GPFIFO_IMPL ticket=%u stage=enter' in text
for reason in (
    "kickoff-shape", "session", "guest-header-range", "inline-span",
    "native-plan", "owner-session", "mixed-owner", "invalid-native-wait",
    "unmapped-command", "channel-not-ready", "channel-bind",
):
    assert f"reason={reason}" in text, reason

# The probes wrap the reviewed safety gates; they do not remove or bypass
# native admission, owner/GMMU checks or the actual GPU submission.
assert 'NativeGpfifoGuestHeadersReadable(' in text
assert 'PlanNativeSubmission(' in text
assert 'IsFullyMappedRange(address, bytes)' in text
assert 'NativeGpuChannelRegistry::Instance().Bind(' in text
assert 'gpu.PushGPUEntries(bind_id, std::move(entries));' in text
assert 'return SubmitGPFIFOImpl(params, std::move(entries), fd);' in text

# Switch GPFIFO submit defines flags as input. OunceVK's native Ioctl2
# H:0x1B sends flags=6 and rejects a successful reply if those flags are
# overwritten. Native Encore preserves accepted flags; Maxwell keeps zero.
submit_impl = text.split("NvResult nvhost_gpu::SubmitGPFIFOImpl(",1)[1].split(
    "\n}",1)[0]
assert "if (!native)\n        flags.raw = 0;" in submit_impl
assert "OunceVK verifies it byte-for-byte" in submit_impl
assert submit_impl.index("gpu.PushGPUEntries(bind_id, std::move(entries));") < submit_impl.index(
    "if (!native)\n        flags.raw = 0;")

# Native waits capture a reservation lease before publication and use the
# existing native scheduler. Legacy wait packets remain NS1-only.
submit = text.split("NvResult nvhost_gpu::SubmitGPFIFOImpl(",1)[1].split(
    "\n}\n",1)[0]
for marker in ("if (native && flags.fence_wait.Value())",
               "syncpoint_manager.CaptureNativeSyncpointLease(",
               "static_cast<u32>(params.fence.id)",
               "entries.native_syncpoint_wait.emplace(",
               "return NvResult::BadParameter;",
               "return NvResult::InsufficientMemory;"):
    assert marker in submit, marker
assert submit.index("syncpoint_manager.CaptureNativeSyncpointLease(") < submit.index(
    "system.GPU().InitChannel(") < submit.index("gpu.PushGPUEntries(")
assert "if (!native && flags.fence_wait.Value())" in submit
dma=(ROOT/"encore-active/eden/src/video_core/dma_pusher.cpp").read_text()
resume=dma.split("bool DmaPusher::ResumeNativeAcquire() {",1)[1].split("\n}",1)[0]
assert resume.count("pending->lease->load(std::memory_order_acquire)")==2
assert resume.index("rasterizer->ReleaseFences(false);") < resume.index(
    "GetHostSyncpointValue(") < resume.index("pending.reset();")
assert "0x80000000U" in resume
assert "GetGuestSyncpointValue" not in resume
# Switch 2 0xC0504821 must bind a real initialized kernel-mode
# channel, not return success for missing GMMU or USERD/doorbell buffers.
hdr=(ROOT/"encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.h").read_text()
pure=(ROOT/"encore-active/ns2-runtime/hle/core/hle/service/nvdrv/devices/native_gpu_submission.h").read_text()
assert "static_assert(sizeof(IoctlChannelSetupBind) == 80" in hdr
for field in ("gpfifo_gpu_va", "userd_gpu_va", "usermode_mmio_gpu_va", "hw_channel_id"):
    assert field in hdr, field
assert "std::array<u32_le, 3> reserved{}" in hdr
assert "std::array<u32_le, 9> reserved{}" not in hdr
assert "NativeKernelChannelSetupStatus ValidateNativeKernelChannelSetup" in pure
assert "num_entries < 4" in pure and "num_entries > (1U << 28)" in pure
assert "request.flags != 0" in pure and "request.num_inflight_jobs != 0" in pure
assert "case 0x21: // Switch 2 NVGPU_IOCTL_CHANNEL_SETUP_BIND" in text
assert "command.raw != 0xC0504821U" in text
bind=text.split("NvResult nvhost_gpu::ChannelSetupBind(",1)[1].split("\n}\n",1)[0]
for needle in ("ValidateNativeKernelChannelSetup(request)",
               "ValidateChannelOwner(fd, program_id)",
               "native_runlist_id.has_value()",
               "channel_state->memory_manager",
               "system.GPU().InitChannel(*channel_state, program_id)",
               "if (!channel_state->initialized || channel_state->bind_id < 0)",
               "params.work_submit_token = 0",
               "params.gpfifo_gpu_va = 0",
               "params.userd_gpu_va = 0",
               "params.usermode_mmio_gpu_va = 0",
               "params.hw_channel_id = static_cast<u32>(channel_state->bind_id)",
               "return NvResult::NotImplemented"):
    assert needle in bind, needle
assert bind.index("ValidateNativeKernelChannelSetup(request)") < bind.index(
    "system.GPU().InitChannel(*channel_state, program_id)")
assert bind.index("ValidateChannelOwner(fd, program_id)") < bind.index(
    "system.GPU().InitChannel(*channel_state, program_id)")

# No command words or guest buffers are persisted by the new trace strings.
for forbidden in ("submitted_words", "command_word=", "guest_bytes=", "dump_pushbuffer"):
    assert forbidden not in "\n".join(
        line for line in text.splitlines() if "EDEN_NS2_GPFIFO_" in line
    )

print("P0_NS2_GPFIFO_PREIMPL_TRACE_SOURCE_PASS routes=6 reasons=11 bounded=32 detailed_only=1 semantic_bypass=0 setup_bind_s2_response=1")
