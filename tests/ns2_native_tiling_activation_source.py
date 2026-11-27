#!/usr/bin/env python3
"""Qualify unified NS1/NS2 native-tiling ownership.

Lichii enables the Switch 2 GOB layout on the GPU MemoryManager. Encore ships
one NS1/NS2 binary, so the flag must default false and may only be enabled after
the existing process-generation NativeGpuChannelRegistry admission succeeds.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
pinned = (root / "sources/eden/src/video_core/memory_manager.h").read_text()
overlay = (root / "encore-active/eden/src/video_core/memory_manager.h").read_text()
nvhost = (root / "encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()
channel = (root / "encore-active/eden/src/video_core/control/channel_state.cpp").read_text()
i2m = (root / "encore-active/eden/src/video_core/engines/engine_upload.cpp").read_text()
blitter = (root / "encore-active/eden/src/video_core/engines/sw_blitter/blitter.cpp").read_text()
texture_util = (root / "encore-active/eden/src/video_core/texture_cache/util.cpp").read_text()
dma = (root / "encore-active/eden/src/video_core/engines/maxwell_dma.cpp").read_text()

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
assert "DmaVolumeLinearRows(height, depth, src_params.layer" in source_volume
assert "static_cast<u64>(dst_operand.pitch) * destination_rows" in source_volume
assert "const u64 tail_lines = static_cast<u64>(line_count)" in dma
assert "remaining_slices = static_cast<u64>(depth) - layer" in dma
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

# I2M streaming admission: compile the actual maintained ProcessExec and both
# packet-consumption overloads. A fragmented GPFIFO upload must never read
# beyond a partial packet or publish unreceived bytes as a whole texture.
import shutil
import subprocess
import tempfile

i2m_start = i2m.index("void State::ProcessExec(")
i2m_stop = i2m.index("void State::ProcessData(std::span<const u8>", i2m_start)
active_upload_methods = i2m[i2m_start:i2m_stop]
assert "static_cast<u64>(regs.line_length_in) * regs.line_count" in active_upload_methods
assert "MaxInlineUploadBytes" in active_upload_methods
assert "write_offset == copy_size" in active_upload_methods
assert "num_data, (remaining + 3) / sizeof(u32)" in active_upload_methods
assert "ProcessData(std::span<const u8>{bytes, copy_size})" in active_upload_methods
assert active_upload_methods.count("inner_buffer.resize_destructive(copy_size);") == 2

inline_fixture = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
struct Registers { u32 line_length_in{}; u32 line_count{}; };
struct Scratch {
    std::vector<u8> storage;
    unsigned allocations{};
    void resize_destructive(std::size_t size) { if (size > storage.capacity()) ++allocations; storage.resize(size); }
    u8* data() { return storage.data(); }
    operator std::span<const u8>() const { return storage; }
};
class State {
public:
    explicit State(Registers& r) : regs(r) {}
    void ProcessExec(bool is_linear_);
    void ProcessData(u32 data, bool is_last_call);
    void ProcessData(const u32* data, std::size_t num_data);
    u32 Size() const { return copy_size; }
    u32 Offset() const { return write_offset; }
    std::size_t StagingSize() const { return inner_buffer.storage.size(); }
    unsigned StagingAllocations() const { return inner_buffer.allocations; }
    std::vector<u8> published;
    unsigned completions{};
private:
    void ProcessData(std::span<const u8> bytes) {
        assert(bytes.size() >= copy_size);
        ++completions;
        published.assign(bytes.begin(), bytes.begin() + copy_size);
    }
    u32 write_offset{};
    u32 copy_size{};
    Scratch inner_buffer;
    bool is_linear{};
    Registers& regs;
};
__MAINTAINED_METHODS__
int main() {
    std::array<u32, 3> words{0x01020304, 0x05060708, 0x090a0b0c};
    const auto* raw = reinterpret_cast<const u8*>(words.data());
    Registers dimensions{4,3};
    State full{dimensions};
    full.ProcessExec(true);
    assert(full.Size()==12 && full.StagingSize()==0 && full.StagingAllocations()==0);
    full.ProcessData(words.data(), 3);
    assert(full.completions==1 && full.Offset()==12);
    assert(full.StagingSize()==0 && full.StagingAllocations()==0);
    assert(std::memcmp(full.published.data(),raw,12)==0);
    full.ProcessData(words.data(), 3);
    assert(full.completions==1);

    State fragmented{dimensions};
    fragmented.ProcessExec(true);
    assert(fragmented.StagingAllocations()==0);
    fragmented.ProcessData(words.data(), 1);
    assert(fragmented.completions==0 && fragmented.Offset()==4);
    assert(fragmented.StagingSize()==12 && fragmented.StagingAllocations()==1);
    fragmented.ProcessData(words.data()+1, 1);
    assert(fragmented.completions==0 && fragmented.Offset()==8);
    fragmented.ProcessData(words.data()+2, 1);
    assert(fragmented.completions==1 && fragmented.Offset()==12);
    assert(std::memcmp(fragmented.published.data(),raw,12)==0);

    State scalar{dimensions};
    scalar.ProcessExec(false);
    assert(scalar.StagingAllocations()==0);
    scalar.ProcessData(words[0],true);
    assert(scalar.completions==0 && scalar.StagingAllocations()==1);
    scalar.ProcessData(words[1],false);
    assert(scalar.completions==0);
    scalar.ProcessData(words[2],true);
    assert(scalar.completions==1);
    assert(std::memcmp(scalar.published.data(),raw,12)==0);

    // A complete scalar upload must publish even if its last data word is
    // not marked as the final method of the enclosing GPFIFO group.
    State scalar_not_last{dimensions};
    scalar_not_last.ProcessExec(false);
    scalar_not_last.ProcessData(words[0],false);
    scalar_not_last.ProcessData(words[1],false);
    assert(scalar_not_last.completions==0);
    scalar_not_last.ProcessData(words[2],false);
    assert(scalar_not_last.completions==1 && scalar_not_last.Offset()==12);
    assert(std::memcmp(scalar_not_last.published.data(),raw,12)==0);
    scalar_not_last.ProcessData(words[2],true);
    assert(scalar_not_last.completions==1);

    Registers partial_length{5,1};
    State partial{partial_length};
    partial.ProcessExec(true);
    partial.ProcessData(words.data(), 2);
    assert(partial.completions==1 && partial.Size()==5 && partial.StagingAllocations()==0);
    assert(std::memcmp(partial.published.data(),raw,5)==0);

    State incomplete{dimensions};
    incomplete.ProcessExec(false);
    incomplete.ProcessData(words.data(), 2);
    assert(incomplete.completions==0 && incomplete.Offset()==8);
    incomplete.ProcessData(nullptr, std::numeric_limits<std::size_t>::max());
    assert(incomplete.completions==0);

    State absurd{dimensions};
    dimensions={std::numeric_limits<u32>::max(),std::numeric_limits<u32>::max()};
    absurd.ProcessExec(true);
    assert(absurd.Size()==0);
    absurd.ProcessData(words.data(), std::numeric_limits<std::size_t>::max());
    assert(absurd.completions==0 && absurd.Offset()==0);

    dimensions={1,(64u<<20)+1};
    absurd.ProcessExec(true);
    assert(absurd.Size()==0);
    dimensions={0,3};
    absurd.ProcessExec(true);
    assert(absurd.Size()==0);
}
""".replace("__MAINTAINED_METHODS__", active_upload_methods)
compiler = shutil.which("clang++") or shutil.which("g++")
assert compiler, "An available C++20 compiler is required for the I2M packet witness"
with tempfile.TemporaryDirectory(prefix="eden-i2m-packet-") as directory:
    path = Path(directory) / "i2m_packet.cpp"
    path.write_text(inline_fixture)
    for opt in ("-O0", "-O2"):
        executable = Path(directory) / ("i2m" + opt)
        compiled = subprocess.run(
            [compiler, "-std=c++20", opt, "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             str(path), "-o", str(executable)], capture_output=True, text=True, timeout=60
        )
        assert compiled.returncode == 0, (opt, compiled.stderr[-5000:])
        ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        assert ran.returncode == 0, (opt, ran.stderr[-5000:])
print("NS2_INLINE_UPLOAD_PACKET_BOUNDS_PASS full=1 fragmented=1 scalar_truncation=1 scalar_complete_without_tail=1 lazy_staging=1 unaligned=1 overflow=1 O0=1 O2=1 ASan=1 UBSan=1")

# The selected I2M writer must prove the entire GOB extent fits in the
# decoder's u32 address space and preserve compact inline words across Z.
i2m_span = i2m.split("void State::ProcessData(std::span<const u8> read_buffer) {", 1)[1]
# A packet cannot reuse a mutable register layout to overrun its received
# bytes; all pitched destinations must be mapped before the first write.
assert "static_cast<u64>(regs.line_length_in) * regs.line_count != copy_size" in i2m_span
assert i2m_span.index("!= copy_size") < i2m_span.index("if (is_linear)")
assert "regs.dest.pitch <= regs.line_length_in" in i2m_span
assert "last_offset + regs.line_length_in" in i2m_span
assert "IsFullyMappedRange(address, static_cast<size_t>(extent))" in i2m_span
assert "IsFullyMappedRange(dest_line, regs.line_length_in)" in i2m_span
assert i2m_span.index("IsFullyMappedRange(dest_line, regs.line_length_in)") < i2m_span.index("rasterizer->AccelerateInlineToMemory")
for guard in (
    "regs.dest.BlockWidth() != 0",
    "origin_y >= height || origin_z >= depth",
    "lines_per_slice * (static_cast<u64>(depth) - origin_z)",
    "aligned_width > MaxInlineUploadBytes / aligned_height",
    "aligned_width * aligned_height > MaxInlineUploadBytes / aligned_depth",
    "source_rows > MaxInlineUploadBytes / regs.line_length_in",
    "dst_size != destination_bytes",
    "memory_manager.IsFullyMappedRange(address, dst_size)",
    "padded_source.resize(static_cast<size_t>(source_bytes))",
    "source.data() + copied_rows * regs.line_length_in",
    "memory_manager.UsesNativeTiling(), origin_z",
):
    assert guard in i2m_span, ("I2M tiled safety contract lost", guard)
assert "source_bytes != copy_size" in i2m_span
