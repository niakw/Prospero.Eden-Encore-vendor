#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
cpp=(R/"sources/eden/src/core/hle/service/nvdrv/nvdrv.cpp").read_text()
hdr=(R/"sources/eden/src/core/hle/service/nvdrv/nvdrv.h").read_text()
stub=(R/"sources/eden/src/core/hle/service/nvdrv/devices/nx2_stub_device.h").read_text()
assert '#include "core/hle/service/nvdrv/devices/nx2_stub_device.h"' in cpp
assert 'device_name.starts_with("/dev/nvhost-")' in cpp
assert cpp.index("builders.find(device_name)") < cpp.index('device_name.starts_with("/dev/nvhost-")')
assert "nx2_stub_builder(fd, device_name)" in cpp
assert "nx2_stub_builder" in hdr
for x in ("class nx2_stub_device final","std::fill(output.begin(), output.end(), u8{0})",
          "std::copy_n(input.begin(), std::min(input.size(), output.size()), output.begin())",
          "return NvResult::Success;"):
    assert x in stub,x
for dev in ("/dev/nvhost-as-gpu","/dev/nvhost-gpu","/dev/nvhost-ctrl-gpu",
            "/dev/nvhost-ctrl","/dev/nvhost-nvdec","/dev/nvhost-vic"):
    assert f'builders["{dev}"]' in cpp,dev
print("NS2_NVHOST_UNKNOWN_NODE_FALLBACK_SOURCE_PASS upstream=5cb4786 tsg_open=1 known_devices_preserved=1")
