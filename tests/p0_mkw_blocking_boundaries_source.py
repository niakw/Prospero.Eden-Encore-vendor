#!/usr/bin/env python3
"""Exact source proof for bounded MKW sparse blocking-boundary diagnostics."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.cpp"
TRACE = ROOT / "encore-active/ns2-helpers/include/core/loader/ns2_svc_trace.h"
SVC = ROOT / "encore-active/ns2-runtime/gpu_access_svc/core/hle/kernel/svc.cpp"

def blob(path: Path) -> str:
    data = path.read_bytes()
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

assert blob(SRC) == "38f4563b8106ef640759dd4bbc69d987a44c8e4a"
text = SRC.read_text()
for op in ("214_map_page_group", "212_wait_idle", "213_wait_idle", "215_wait_idle", "216_wait_idle"):
    assert text.count('BlockingBoundaryTrace trace{"' + op) == 1, op
assert text.count("EDEN_NS2_BLOCKING_BOUNDARY") == 2
assert "ticket <= 32 || (ticket & (ticket - 1)) == 0" in text
assert "std::chrono::steady_clock::now()" in text
# The probes wrap existing potentially blocking calls; they do not replace,
# bypass, retry or weaken the underlying ownership operations.
assert text.count("system.GPU().WaitForIdle();") >= 5
assert "table.MapPageGroup(candidate, *pages, Kernel::KMemoryState::IoMemory" in text
assert "KeepWrites(" not in text and "GpuSplit" not in text
trace = TRACE.read_text()
svc = SVC.read_text()
assert 'constexpr std::uint32_t MaxSamples = 32;' in trace
assert 'eden_native_detailed_logging() noexcept __attribute__((weak))' in trace
assert 'if (!eden_native_detailed_logging || !eden_native_detailed_logging())' in trace
assert '#ifdef EDEN_NS2_SVC_TRACE' in svc
assert 'SvcTrace::Next(process.GetProcessId())' in svc
assert 'NS2 real HLE SVC[{}]' in svc
print("P0_MKW_BLOCKING_BOUNDARY_SOURCE_PASS ops=5 bounded=1 svc_samples=32 detailed_only=1 lichii_1413_not_blindly_ported=1")
