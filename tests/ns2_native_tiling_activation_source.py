#!/usr/bin/env python3
"""Qualify unified NS1/NS2 native-tiling ownership.

Lichii enables the Switch 2 GOB layout on the GPU MemoryManager. Encore ships
one NS1/NS2 binary, so the flag must default false and may only be enabled after
the existing process-generation NativeGpuChannelRegistry admission succeeds.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
pinned = (root / "sources/eden/src/video_core/memory_manager.h").read_text()
overlay = (root / "encore-active/eden-core/overrides/native-tiling/video_core/memory_manager.h").read_text()
nvhost = (root / "encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()
channel = (root / "encore-active/ns2-runtime/gpu_dispatch/video_core/control/channel_state.cpp").read_text()
i2m = (root / "encore-active/eden-core/src/video_core/engines/engine_upload.cpp").read_text()
blitter = (root / "encore-active/eden-core/src/video_core/engines/sw_blitter/blitter.cpp").read_text()

dtor = "    ~MemoryManager();\n"
api = """    ~MemoryManager();

    /// Switch 2 lays out block-linear surfaces with the console's GOB swizzle.
    /// Disabled by default so the unified binary keeps Maxwell/NS1 layout.
    void EnableNativeTiling() {
        native_tiling = true;
    }
    [[nodiscard]] bool UsesNativeTiling() const {
        return native_tiling;
    }
"""
raster = "    VideoCore::RasterizerInterface* rasterizer = nullptr;\n"
assert pinned.count(dtor) == 1 and pinned.count(raster) == 1
assert "EnableNativeTiling" not in pinned and "UsesNativeTiling" not in pinned
expected = pinned.replace(dtor, api, 1).replace(raster, raster + "\n    bool native_tiling{};\n", 1)
assert overlay == expected, "native-tiling MemoryManager drift beyond reviewed upstream API"
assert overlay.count("bool native_tiling{};") == 1
assert overlay.count("EnableNativeTiling()") == 1
assert overlay.count("UsesNativeTiling() const") == 1

# The generation-neutral ChannelState must not globally opt every game in.
assert "EnableNativeTiling" not in channel

bind = "NativeGpuChannelRegistry::Instance().Bind("
enable = "channel_state->memory_manager->EnableNativeTiling();"
push = "gpu.PushGPUEntries(bind_id, std::move(entries));"
assert nvhost.count(enable) == 1
assert nvhost.index("if (native) {", nvhost.index(bind) - 600) < nvhost.index(bind)
assert nvhost.index(bind) < nvhost.index(enable) < nvhost.index(push)
assert "if (native && (!channel_state->initialized || !channel_state->memory_manager))" in nvhost

# Every reviewed block-linear writer consumes the same per-address-space flag.
assert i2m.count("memory_manager.UsesNativeTiling()") == 1
assert blitter.count("memory_manager.UsesNativeTiling()") == 2
print("NS2_NATIVE_TILING_ACTIVATION_PASS default_ns1=0 authenticated_ns2=1 i2m=1 blitter=2")
