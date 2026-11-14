#!/usr/bin/env python3
"""Host/source contract for bounded NS1 NVDEC -> VIC stage witnesses.

Diagnostic only: no decoder, VIC or texture semantics are changed here.
"""
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
FF=ROOT/"encore-active/eden-core/src/video_core/host1x/ffmpeg.cpp"
VIC=ROOT/"encore-active/eden-core/src/video_core/host1x/vic.cpp"
ORIG=ROOT/"sources/eden/src/video_core/host1x/vic.cpp"

def blob(path):
    return subprocess.check_output(["git","hash-object",str(path.relative_to(ROOT))],
                                   cwd=ROOT,text=True).strip()

assert blob(FF)=="9a3cf0b0a2cf92e467a619352b9345c2b7823d8b"
assert blob(VIC)=="7e84c9eea181f72f8fc7d3364725d0486d50330b"
assert blob(ORIG)=="1a89f3de06981f95be9e5dc743a7c36b8ab6c4cf"

ff=FF.read_text()
vic=VIC.read_text()
orig=ORIG.read_text()

# Preserve the already-qualified FFmpeg packet-EAGAIN retry path.
for marker in (
    "constexpr int kMaxDrainRetries = 16;",
    "m_ready_frames.push(std::move(drained));",
    "NVDEC EAGAIN retry limit reached",
):
    assert marker in ff
assert ff.count("EDEN_NVDEC_STAGE stage=decode") == 1
assert ff.count("static std::atomic<unsigned> decoded_witness{};") == 1
assert "if (witness <= 3)" in ff

# VIC remains the pristine implementation plus bounded receipts; no black fill
# or format path is removed.
for marker in (
    "ReadY8__V8U8_N420(slot_config, regs.surfaces[i], std::move(frame), true);",
    "ReadY8__V8U8_N420(slot_config, regs.surfaces[i], std::move(frame), false);",
    "std::fill(output_surface.begin(), output_surface.end(), Pixel{});",
    "WriteABGR(config.output_surface_config, format);",
    "WriteY8__V8U8_N420(config.output_surface_config);",
):
    assert marker in vic and marker in orig
assert vic.count("EDEN_NVDEC_STAGE stage=vic_input") == 1
assert vic.count("EDEN_NVDEC_STAGE stage=vic_output") == 1
assert vic.count("nvdec_vic_input_witness.fetch_add") == 1
assert vic.count("nvdec_vic_output_witness.fetch_add") == 1
assert vic.count("if (witness <= 3)") == 2
assert "std::memory_order_relaxed" in vic

# There are exactly three stage sites total. This rejects accidental per-frame
# diagnostic proliferation.
combined=ff+vic
assert combined.count("EDEN_NVDEC_STAGE stage=") == 3
print("FC27_NVDEC_VIC_STAGE_WITNESS_SOURCE_PASS stages=3 max_each=3 eagain_preserved=1 semantic_fix=0")
