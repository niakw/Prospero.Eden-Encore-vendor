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
texture_util = (root / "encore-active/eden-core/src/video_core/texture_cache/util.cpp").read_text()
dma = (root / "encore-active/eden-core/src/video_core/engines/maxwell_dma.cpp").read_text()

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
# The active B0B5 CPU fallback must use the authenticated channel's GOB
# layout on every tiled source/destination, including remaps and aliases.
# Missing this changes Switch 2's 16-byte intra-GOB ordering while NS1 is
# supposed to keep the old default.
import re
dma_calls = re.findall(r"(?m)^\s*(?:Unswizzle|Swizzle)Subrect\([\s\S]*?\);", dma)
assert len(dma_calls) == 9, len(dma_calls)
for call in dma_calls:
    assert call.count("memory_manager.UsesNativeTiling()") == 1, call
assert dma.count("memory_manager.UsesNativeTiling()") == 9
# MaxwellDMA now consumes the same reviewed 3D origin-Z decoder as
# Fermi2D. Every software readback/upload path must carry the actual Z layer,
# including the overlap-snapshot path; only depth-tiled volumes are admitted.
source_volume = dma.split("void MaxwellDMA::CopyBlockLinearToPitch() {", 1)[1].split(
    "void MaxwellDMA::CopyPitchToBlockLinear() {", 1
)[0]
destination_volume = dma.split("void MaxwellDMA::CopyPitchToBlockLinear() {", 1)[1].split(
    "void MaxwellDMA::CopyBlockLinearToBlockLinear() {", 1
)[0]
for body, layer in (
    (source_volume, "src_params.layer"),
    (destination_volume, "dst_params.layer"),
):
    selected = re.findall(r"(?m)^\s*(?:Unswizzle|Swizzle)Subrect\([\s\S]*?\);", body)
    assert len(selected) == 2, (layer, len(selected))
    for call in selected:
        assert call.count("memory_manager.UsesNativeTiling()") == 1, layer
        assert call.count(layer) == 1, (layer, call)
assert "src_geometry.layer >= src_geometry.depth" in source_volume
assert "src_geometry.block_size.depth == 0" in source_volume
assert "src_size != aligned_width * aligned_height * aligned_depth" in source_volume
assert "dst_params.block_size.depth == 0" in destination_volume
assert "dst_params.layer >= regs.dst_params.depth" in destination_volume
assert "lines_per_slice * (depth - dst_params.layer)" in destination_volume
assert "MAX_SOFTWARE_TILED_DMA_BYTES" in source_volume
assert "MAX_SOFTWARE_TILED_DMA_BYTES" in destination_volume
# The same Z addressability also applies to non-remapped tiled-to-tiled
# copies: one source slice is fully unswizzled before any destination writes.
# Reuse the existing GOB3D bounds and explicit per-side layer arguments.
tiled_to_tiled = dma.split("void MaxwellDMA::CopyBlockLinearToBlockLinear() {", 1)[1]
selected = re.findall(r"(?m)^\s*(?:Unswizzle|Swizzle)Subrect\([\s\S]*?\);", tiled_to_tiled)
assert len(selected) == 2, len(selected)
assert "memory_manager.UsesNativeTiling(), src.layer" in selected[0]
assert "memory_manager.UsesNativeTiling(), dst.layer" in selected[1]
assert "return image.depth > 1 && image.block_size.depth != 0" in tiled_to_tiled
assert "image.layer < image.depth" in tiled_to_tiled
assert "aligned_width * aligned_height * aligned_depth" in tiled_to_tiled
assert "src.depth, src.block_size.height" in tiled_to_tiled
assert "dst.depth, dst.block_size.height" in tiled_to_tiled
assert "MAX_SOFTWARE_TILED_DMA_BYTES" in tiled_to_tiled
# The remapped tiled DMA path must use Z for each selected 3D surface
# while keeping the 16 MiB full-volume cap and the NO_WRITE destination
# snapshot; a 2D array layer never becomes a GOB Z coordinate.
tiled_remap = dma.split("bool MaxwellDMA::CopyBlockLinearRemap() {", 1)[1].split(
    "void MaxwellDMA::CopyBlockLinearToPitch() {", 1
)[0]
remap_calls = re.findall(r"(?m)^\s*(?:Unswizzle|Swizzle)Subrect\([\s\S]*?\);", tiled_remap)
assert len(remap_calls) == 3, len(remap_calls)
assert "memory_manager.UsesNativeTiling(),\n                             regs.src_params.layer" in remap_calls[0]
assert "memory_manager.UsesNativeTiling(),\n                             regs.dst_params.layer" in remap_calls[1]
assert "memory_manager.UsesNativeTiling(),\n                       regs.dst_params.layer" in remap_calls[2]
assert "const bool is_volume = image.layer != 0 || image.depth != 1" in tiled_remap
assert "image.layer >= image.depth" in tiled_remap
assert "block_depth > 5" in tiled_remap
assert "padded_width * padded_height * padded_depth" in tiled_remap
assert "MAX_BYTES = 16ULL << 20" in tiled_remap
assert "if (needs_destination)" in tiled_remap
# CMake selects this maintained texture utility for both generations.
# Its block-linear upload and readback must both use the same channel flag.
swizzle = texture_util.split("void SwizzleBlockLinearImage(", 1)[1].split("\n} // Anonymous namespace", 1)[0]
unswizzle = texture_util.split("UnswizzleImage(Tegra::MemoryManager& gpu_memory,", 1)[1].split("\nvoid ConvertImage(", 1)[0]
assert swizzle.count("gpu_memory.UsesNativeTiling()") == 1
assert unswizzle.count("gpu_memory.UsesNativeTiling()") == 1
assert texture_util.count("gpu_memory.UsesNativeTiling()") == 2
print("NS2_NATIVE_TILING_ACTIVATION_PASS default_ns1=0 authenticated_ns2=1 i2m=1 blitter=2 dma=9 volume_z=9 texture_util=2")
