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
shim_declarations = ("""    // Lichii 12125bc / NX2 #46: guest NVN reads Host1x syncpoint words
    // through a read-only GPU virtual shim; zero preserves NS1 behavior.
    void SetNativeSyncpointShim(GPUVAddr address);
    // A channel may expose only an existing, initialized shim mapping,
    // never a guessed GPU virtual address.
    [[nodiscard]] GPUVAddr GetNativeSyncpointShim() const noexcept {
        return native_syncpoint_shim.load(std::memory_order_acquire);
    }
    static constexpr u32 NumShimSyncpoints = 192;

""")
combined_api = api.replace("    /// Switch 2 lays out block-linear surfaces",
                           shim_declarations + "    /// Switch 2 lays out block-linear surfaces", 1)
expected = pinned.replace(dtor, combined_api, 1).replace(raster, raster + "\n    bool native_tiling{};\n", 1).replace(
    "    static std::atomic<size_t> unique_identifier_generator;\n",
    "    static std::atomic<size_t> unique_identifier_generator;\n"
    "    std::atomic<GPUVAddr> native_syncpoint_shim{0};\n", 1)
assert overlay == expected, "native-tiling MemoryManager drift beyond reviewed upstream API"
assert overlay.count("bool native_tiling{};") == 1
assert overlay.count("EnableNativeTiling()") == 1
assert overlay.count("UsesNativeTiling() const") == 1
for name in ("SetNativeSyncpointShim", "GetNativeSyncpointShim",
             "NumShimSyncpoints", "native_syncpoint_shim"):
    assert name in overlay and name not in pinned, name
assert "EnqueueDeferredCPUWrite" not in overlay  # never activate experimental bridge

# The generation-neutral ChannelState must not globally opt every game in.
assert "EnableNativeTiling" not in channel

bind = "native_channel_registry.Bind("
enable = "channel_state->memory_manager->EnableNativeTiling();"
push = "push_gpu_entries(std::move(entries));"
assert nvhost.count(enable) == 1
assert nvhost.index("if (native) {", nvhost.index(bind) - 600) < nvhost.index(bind)
assert nvhost.index(bind) < nvhost.index(enable) < nvhost.index(push)
assert "gpu.PushGPUEntries(bind_id, std::move(list));" in nvhost
assert "if (native && (!channel_state->initialized || !channel_state->memory_manager))" in nvhost

# Every reviewed block-linear writer consumes the same per-address-space flag.
assert i2m.count("memory_manager.UsesNativeTiling()") == 1
assert blitter.count("memory_manager.UsesNativeTiling()") == 2
print("NS2_NATIVE_TILING_ACTIVATION_PASS default_ns1=0 authenticated_ns2=1 i2m=1 blitter=2")
