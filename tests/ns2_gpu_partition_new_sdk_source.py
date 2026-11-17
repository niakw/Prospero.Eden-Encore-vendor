#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
cpp=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_session.cpp").read_text()
hdr=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_session.h").read_text()
proto=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/gpu_session_protocol.h").read_text()
for marker in (
    '{201, &IGpuSession::GetUserPartition, "GetUserPartition"}',
    '{202, &IGpuSession::GetGsvPartition, "GetGsvPartition"}',
    '{203, &IGpuSession::GetVaRange, "GetVaRange"}',
):
    assert marker in cpp, marker
assert "GetAddressSpacePartition" not in cpp
assert "GetAddressSpacePartition" not in hdr
for cmd in ("201","202","203"):
    assert f"DecodeAddressSpacePartition(command, {cmd})" in cpp
assert "command != 201 && command != 202 && command != 203" in proto
assert "first = Nvidia::Devices::NativeNs2GpuVaStart;" in cpp
assert "size = Nvidia::Devices::NativeNs2GpuVaEnd - first;" in cpp
assert "first = ~u64{0};" in cpp and "last = 0;" in cpp
assert "kind = 1;" in cpp
# The obsolete nine-u64 GetVARegions reply was a shifted older contract.
assert "std::array<u64, 9> values" not in cpp
print("NS2_GPU_PARTITION_NEW_SDK_SOURCE_PASS commands=201,202,203 upstream=Lichii")
