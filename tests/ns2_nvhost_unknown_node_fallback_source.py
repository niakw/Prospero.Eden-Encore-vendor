#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
cpp=(R/"sources/eden/src/core/hle/service/nvdrv/nvdrv.cpp").read_text()
hdr=(R/"sources/eden/src/core/hle/service/nvdrv/nvdrv.h").read_text()
stub=(R/"sources/eden/src/core/hle/service/nvdrv/devices/nx2_stub_device.h").read_text()
nvdev=(R/"sources/eden/src/core/hle/service/nvdrv/devices/nvdevice.h").read_text()
gpu=(R/"encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()
gpu_hdr=(R/"encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_gpu.h").read_text()
assert '#include "core/hle/service/nvdrv/devices/nx2_stub_device.h"' in cpp
assert 'device_name.starts_with("/dev/nvhost-")' in cpp
assert cpp.index("builders.find(device_name)") < cpp.index('device_name.starts_with("/dev/nvhost-")')
assert "nx2_stub_builder(fd, device_name)" in cpp
assert "nx2_stub_builder" in hdr
for x in ("class nx2_stub_device final",
          "// not implemented yet on Encore",
          "return NvResult::NotImplemented;"):
    assert x in stub,x
assert stub.count("// not implemented yet on Encore") >= 6
fallback=stub[stub.index("NvResult Stub("):]
assert "std::fill(output.begin(), output.end(), u8{0})" not in fallback
assert "std::copy_n(input.begin(), std::min(input.size(), output.size()), output.begin())" not in fallback
for ioctl in ("0xC0045401U", "0xC0045402U", "0xC018540BU",
              "0xC0085407U", "0xC0045408U", "0x8004540AU",
              "0x00005403U", "0x00005404U", "0x00005405U",
              "0xC0105409U", "0xC018540CU", "0xC008540DU"):
    assert ioctl in stub, ioctl
for needle in (
    "SetTsgEnabled",
    "TsgChannelController",
    "control_channel(*tsg_owner, channels[committed], enabled)",
    "previous_enabled = tsg_enabled",
    "previous_enabled);",
    "tsg_enabled = enabled",
    "bool tsg_enabled{}",
    "return SetTsgEnabled(true)",
    "return SetTsgEnabled(false)",
    "ConfigureTsgScheduling",
    "value > 2",
    "value < 1000 || value > 50000",
    "HasBoundTsgChannels()",
    "// not implemented yet on Encore: changing scheduling after bind",
    "tsg_interleave_level = value",
    "tsg_timeslice_us = value",
    "u32 tsg_timeslice_us{1024}",
    "GetTsgTimeslice",
    "std::memcpy(output.data(), &value, sizeof(value))",
):
    assert needle in stub, needle
for needle in (
    "virtual NvResult SetNativeTsgChannelEnabled",
    "return NvResult::NotImplemented;",
):
    assert needle in nvdev,needle
assert "SetNativeTsgChannelEnabled(DeviceFD fd, NvCore::SessionId session" in gpu_hdr
assert "it->second->SetNativeTsgChannelEnabled(" in cpp
for raw in ("0x0000480EU","0x0000480FU","0x00004810U","0x00004811U"):
    assert raw in gpu,raw
assert "if (!native_channel_enabled)" in gpu
assert "reason=channel-disabled" in gpu

for dev in ("/dev/nvhost-as-gpu","/dev/nvhost-gpu","/dev/nvhost-ctrl-gpu",
            "/dev/nvhost-ctrl","/dev/nvhost-nvdec","/dev/nvhost-vic"):
    assert f'builders["{dev}"]' in cpp,dev
print("NS2_NVHOST_UNKNOWN_NODE_FALLBACK_SOURCE_PASS upstream=5cb4786 tsg_open=1 known_devices_preserved=1 unknown_ioctl_fail_closed=1 tsg_prebind_scheduling=1 tsg_get_timeslice=1 tsg_enable_disable=shared_channel_state tsg_known_failclosed=4")
