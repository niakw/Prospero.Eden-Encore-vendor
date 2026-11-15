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
    "native-plan", "owner-session", "mixed-owner", "unresolved-native-wait",
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

# No command words or guest buffers are persisted by the new trace strings.
for forbidden in ("submitted_words", "command_word=", "guest_bytes=", "dump_pushbuffer"):
    assert forbidden not in "\n".join(
        line for line in text.splitlines() if "EDEN_NS2_GPFIFO_" in line
    )

print("P0_NS2_GPFIFO_PREIMPL_TRACE_SOURCE_PASS routes=6 reasons=11 bounded=32 detailed_only=1 semantic_bypass=0")
