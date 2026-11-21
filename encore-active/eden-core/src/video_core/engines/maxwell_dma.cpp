// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#include "common/algorithm.h"
#include "common/assert.h"
#include "common/logging.h"
#include <ranges>
#include "common/settings.h"
#include "core/core.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/guest_memory.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_base.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

using namespace Texture;

namespace {
// Fold a row-wise DMA into one guest-memory copy only when every row is
// contiguous, both byte ranges are representable and do not overlap.
constexpr bool CanCoalescePitchDma(u64 source, u64 dest, u32 width, u32 rows,
                                   s64 pitch_in, s64 pitch_out, u64& total) noexcept {
    total = 0;
    if (width == 0 || rows == 0 || pitch_in != static_cast<s64>(width) ||
        pitch_out != static_cast<s64>(width) ||
        width > (~u32{0}) / rows) {
        return false;
    }
    const u64 bytes = static_cast<u64>(width) * rows;
    if (source > ~u64{0} - bytes || dest > ~u64{0} - bytes) {
        return false;
    }
    if (!(source + bytes <= dest || dest + bytes <= source)) {
        return false;
    }
    total = bytes;
    return true;
}

// Remapped DMA lengths are measured in pixels/blocks, NOT bytes. Component
// width and per-pixel component counts are configured independently by the
// B0B5 SET_REMAP_COMPONENTS register. Apply all four destination selectors;
// NO_WRITE must preserve the current destination component. This is generic
// copy-engine behavior, shared by every guest title using this class.
bool CopyPitchRemap(MemoryManager& memory, const MaxwellDMA::RemapConst& remap,
                    GPUVAddr source, GPUVAddr destination, s32 source_pitch,
                    s32 destination_pitch, u32 blocks, u32 rows,
                    Common::ScratchBuffer<u8>& source_scratch,
                    Common::ScratchBuffer<u8>& destination_scratch) {
    using Swizzle = MaxwellDMA::RemapConst::Swizzle;
    const u32 component_size = remap.component_size_minus_one + 1;
    const u32 src_components = remap.num_src_components_minus_one + 1;
    const u32 dst_components = remap.num_dst_components_minus_one + 1;
    // Both counts come from B0B5 two-bit fields. Assert the bounds in the
    // consuming C++ too: selector indexing must never depend on bitfield
    // inlining or guessed range analysis.
    if (component_size == 0 || component_size > 4 ||
        src_components == 0 || src_components > 4 ||
        dst_components == 0 || dst_components > 4) {
        return false;
    }
    const std::array<Swizzle, 4> selectors{
        remap.GetComponent(0), remap.GetComponent(1),
        remap.GetComponent(2), remap.GetComponent(3),
    };
    bool needs_source = false;
    bool needs_destination = false;
    bool has_writes = false;
    bool identity = src_components == dst_components;
    for (u32 slot = 0; slot < dst_components; ++slot) {
        const auto selector = selectors[slot];
        const u32 value = static_cast<u32>(selector);
        if (selector == Swizzle::NO_WRITE) {
            needs_destination = true;
            identity = false;
        } else if (selector == Swizzle::CONST_A || selector == Swizzle::CONST_B) {
            has_writes = true;
            identity = false;
        } else if (value < 4 && value < src_components) {
            needs_source = true;
            has_writes = true;
            identity &= value == slot;
        } else {
            return false; // Reserved or out-of-range source component.
        }
    }
    if (blocks == 0 || rows == 0 || !has_writes) {
        return true;
    }
    const u64 src_stride = static_cast<u64>(component_size) * src_components;
    const u64 dst_stride = static_cast<u64>(component_size) * dst_components;
    const u64 src_row_bytes = static_cast<u64>(blocks) * src_stride;
    const u64 dst_row_bytes = static_cast<u64>(blocks) * dst_stride;
    constexpr u64 MAX_CPU_REMAP_BYTES = 64ULL << 20;
    if (dst_row_bytes > MAX_CPU_REMAP_BYTES / rows ||
        (needs_source && src_row_bytes > MAX_CPU_REMAP_BYTES / rows)) {
        return false; // Do not OOM the PS5 on a guest-controlled DMA span.
    }

    // Validate the full signed-pitch address envelopes before any writes.
    const auto envelope = [](GPUVAddr base, s32 pitch, u32 row_count, u64 bytes,
                             u64& begin, u64& end) {
        const u64 magnitude = pitch < 0 ? static_cast<u64>(-static_cast<s64>(pitch))
                                        : static_cast<u64>(pitch);
        const u64 delta = magnitude * (row_count - 1);
        constexpr u64 MAX_ADDRESS = (std::numeric_limits<u64>::max)();
        if (pitch < 0) {
            if (delta > base || bytes > MAX_ADDRESS - base) {
                return false;
            }
            begin = base - delta;
            end = base + bytes;
        } else {
            if (delta > MAX_ADDRESS - base ||
                bytes > MAX_ADDRESS - base - delta) {
                return false;
            }
            begin = base;
            end = base + delta + bytes;
        }
        return true;
    };
    u64 src_begin{}, src_end{}, dst_begin{}, dst_end{};
    const s32 actual_src_pitch = rows == 1 ? 0 : source_pitch;
    const s32 actual_dst_pitch = rows == 1 ? 0 : destination_pitch;
    if ((needs_source &&
         !envelope(source, actual_src_pitch, rows, src_row_bytes, src_begin, src_end)) ||
        !envelope(destination, actual_dst_pitch, rows, dst_row_bytes, dst_begin, dst_end)) {
        return false;
    }
    const bool overlap = needs_source && src_begin < dst_end && dst_begin < src_end;
    const auto row_address = [](GPUVAddr base, s32 pitch, u32 row) {
        const u64 magnitude = pitch < 0 ? static_cast<u64>(-static_cast<s64>(pitch))
                                        : static_cast<u64>(pitch);
        const u64 delta = magnitude * row;
        return pitch < 0 ? base - delta : base + delta;
    };

    // Common identity remaps remain a normal DMA block copy; never turn
    // every format-preserving upload into a CPU per-component loop.
    if (identity && !overlap) {
        u64 contiguous_bytes{};
        if (src_row_bytes <= static_cast<u64>((std::numeric_limits<u32>::max)()) &&
            CanCoalescePitchDma(source, destination, static_cast<u32>(src_row_bytes),
                                rows, actual_src_pitch, actual_dst_pitch,
                                contiguous_bytes)) {
            memory.CopyBlock(destination, source, static_cast<size_t>(contiguous_bytes));
        } else {
            for (u32 row = 0; row < rows; ++row) {
                memory.CopyBlock(row_address(destination, actual_dst_pitch, row),
                                 row_address(source, actual_src_pitch, row),
                                 static_cast<size_t>(src_row_bytes));
            }
        }
        return true;
    }

    // Snapshot overlapping source rows before modifying any destination row.
    // A partial stencil/depth remap may leave some destination components
    // unchanged; read those bytes before writing each output chunk.
    std::vector<u8> snapshot;
    if (overlap) {
        snapshot.resize(static_cast<size_t>(src_row_bytes * rows));
        for (u32 row = 0; row < rows; ++row) {
            memory.ReadBlock(row_address(source, actual_src_pitch, row),
                             snapshot.data() + static_cast<size_t>(src_row_bytes * row),
                             static_cast<size_t>(src_row_bytes));
        }
    }
    constexpr size_t CHUNK_BYTES = 64 * 1024;
    const u64 chunk_blocks = (std::max)(u64{1}, CHUNK_BYTES / (std::max)(src_stride, dst_stride));
    source_scratch.resize_destructive(static_cast<size_t>(chunk_blocks * src_stride));
    destination_scratch.resize_destructive(static_cast<size_t>(chunk_blocks * dst_stride));
    for (u32 row = 0; row < rows; ++row) {
        const u64 source_base = row_address(source, actual_src_pitch, row);
        const u64 destination_base = row_address(destination, actual_dst_pitch, row);
        for (u64 start = 0; start < blocks; start += chunk_blocks) {
            const u64 count = (std::min)(chunk_blocks, static_cast<u64>(blocks) - start);
            const size_t src_bytes = static_cast<size_t>(count * src_stride);
            const size_t dst_bytes = static_cast<size_t>(count * dst_stride);
            const u64 src_address = source_base + start * src_stride;
            const u64 dst_address = destination_base + start * dst_stride;
            const u8* src_bytes_ptr = nullptr;
            if (needs_source) {
                if (overlap) {
                    src_bytes_ptr = snapshot.data() +
                        static_cast<size_t>(row * src_row_bytes + start * src_stride);
                } else {
                    memory.ReadBlock(src_address, source_scratch.data(), src_bytes);
                    src_bytes_ptr = source_scratch.data();
                }
            }
            if (needs_destination) {
                memory.ReadBlock(dst_address, destination_scratch.data(), dst_bytes);
            }
            u8* const dst = destination_scratch.data();
            for (u64 element = 0; element < count; ++element) {
                const u8* const src = needs_source ? src_bytes_ptr + element * src_stride : nullptr;
                for (u32 slot = 0; slot < dst_components; ++slot) {
                    const auto sel = selectors[slot];
                    u8* const target = dst + element * dst_stride + slot * component_size;
                    if (sel == Swizzle::NO_WRITE) {
                        continue;
                    }
                    if (static_cast<u32>(sel) < 4) {
                        const u8* component = src + static_cast<u32>(sel) * component_size;
                        std::copy_n(component, component_size, target);
                    } else {
                        const u32 value = sel == Swizzle::CONST_A
                                              ? remap.remap_consta_value
                                              : remap.remap_constb_value;
                        for (u32 byte = 0; byte < component_size; ++byte) {
                            target[byte] = static_cast<u8>(value >> (8 * byte));
                        }
                    }
                }
            }
            memory.WriteBlock(dst_address, dst, dst_bytes);
        }
    }
    return true;
}
} // namespace

MaxwellDMA::MaxwellDMA(MemoryManager& memory_manager_)
    : memory_manager{memory_manager_}
{
    execution_mask.reset();
    execution_mask[offsetof(Regs, launch_dma) / sizeof(u32)] = true;
}

MaxwellDMA::~MaxwellDMA() = default;

void MaxwellDMA::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
}

void MaxwellDMA::ConsumeSinkImpl(Core::System& system) {
    for (auto [method, value] : method_sink) {
        regs.reg_array[method] = value;
    }
    method_sink.clear();
}

void MaxwellDMA::CallMethod(Core::System& system, u32 method, u32 method_argument, bool is_last_call) {
    ASSERT_MSG(method < NUM_REGS, "Invalid MaxwellDMA register");

    regs.reg_array[method] = method_argument;

    if (method == offsetof(Regs, launch_dma) / sizeof(u32)) {
        Launch();
    }
}

void MaxwellDMA::CallMultiMethod(Core::System& system, u32 method, const u32* base_start, u32 amount, u32 methods_pending) {
    for (u32 i = 0; i < amount; ++i) {
        CallMethod(system, method, base_start[i], methods_pending - i <= 1);
    }
}

void MaxwellDMA::Launch() {
    LOG_TRACE(Render_OpenGL, "DMA copy {:#x} -> {:#x}", static_cast<GPUVAddr>(regs.offset_in), GPUVAddr(regs.offset_out));

    // TODO(Subv): Perform more research and implement all features of this engine.
    const LaunchDMA& launch = regs.launch_dma;
    ASSERT(launch.interrupt_type == LaunchDMA::InterruptType::NONE);

    if (launch.remap_enable != 0 &&
        launch.src_memory_layout == LaunchDMA::MemoryLayout::PITCH &&
        launch.dst_memory_layout == LaunchDMA::MemoryLayout::PITCH) {
        // SET_REMAP_COMPONENTS defines the width and swizzle for *all*
        // components, even when multi-line DMA is enabled. Do not perform
        // the old raw byte copy with an implicit one-byte block width.
        memory_manager.FlushCaching();
        const u32 rows = launch.multi_line_enable != 0 ? regs.line_count : 1;
        if (!CopyPitchRemap(memory_manager, regs.remap_const, regs.offset_in, regs.offset_out,
                            regs.pitch_in, regs.pitch_out, regs.line_length_in, rows,
                            read_buffer, write_buffer)) {
            LOG_WARNING(Render_OpenGL, "Unsupported or oversized pitch DMA remap");
            AssertFailSoftImpl();
        }
        ReleaseSemaphore();
        return;
    }

    if (launch.multi_line_enable != 0 && launch.remap_enable != 0 &&
        (launch.src_memory_layout != LaunchDMA::MemoryLayout::PITCH ||
         launch.dst_memory_layout != LaunchDMA::MemoryLayout::PITCH)) {
        // Tiled remaps are measured in components, not raw bytes. Never
        // silently copy only LINE_LENGTH_IN bytes of a depth/stencil image.
        memory_manager.FlushCaching();
        if (!CopyBlockLinearRemap()) {
            LOG_WARNING(Render_OpenGL, "Unsupported or oversized B0B5 tiled DMA remap");
            AssertFailSoftImpl();
        }
        ReleaseSemaphore();
        return;
    }

    if (launch.multi_line_enable) {
        const bool is_src_pitch = launch.src_memory_layout == LaunchDMA::MemoryLayout::PITCH;
        const bool is_dst_pitch = launch.dst_memory_layout == LaunchDMA::MemoryLayout::PITCH;
        memory_manager.FlushCaching();
        if (!is_src_pitch && !is_dst_pitch) {
            // If both the source and the destination are in block layout, assert.
            CopyBlockLinearToBlockLinear();
            ReleaseSemaphore();
            return;
        }

        if (is_src_pitch && is_dst_pitch) {
            u64 contiguous_bytes = 0;
            if (CanCoalescePitchDma(regs.offset_in, regs.offset_out, regs.line_length_in,
                                    regs.line_count, regs.pitch_in, regs.pitch_out,
                                    contiguous_bytes)) {
                // A single mapped copy/flush avoids a row-wise GPU memory walk.
                memory_manager.CopyBlock(regs.offset_out, regs.offset_in,
                                         static_cast<size_t>(contiguous_bytes));
            } else {
                // Retain exact row-by-row semantics for overlapping, gapped,
                // negative-pitch or large transfers.
                for (u32 line = 0; line < regs.line_count; ++line) {
                    const GPUVAddr source_line =
                        regs.offset_in + static_cast<size_t>(line) * regs.pitch_in;
                    const GPUVAddr dest_line =
                        regs.offset_out + static_cast<size_t>(line) * regs.pitch_out;
                    memory_manager.CopyBlock(dest_line, source_line, regs.line_length_in);
                }
            }
        } else {
            if (!is_src_pitch && is_dst_pitch) {
                CopyBlockLinearToPitch();
            } else {
                CopyPitchToBlockLinear();
            }
        }
    } else {
        // TODO: allow multisized components.
        auto& accelerate = rasterizer->AccessAccelerateDMA();
        const bool is_const_a_dst = regs.remap_const.dst_x == RemapConst::Swizzle::CONST_A;
        if (regs.launch_dma.remap_enable != 0 && is_const_a_dst) {
            const u32 component_size = regs.remap_const.component_size_minus_one + 1;
            // NVIDIA B0B5 SET_REMAP_COMPONENTS allows 1..4 bytes per component.
            // The old u32 fill followed by a shorter 1/2-byte copy produced
            // 00-padded words instead of contiguous CONST_A components.
            ASSERT(component_size >= 1 && component_size <= 4);
            if (component_size == 4) {
                accelerate.BufferClear(regs.offset_out, regs.line_length_in, regs.remap_const.remap_consta_value);
            }
            const size_t byte_count = static_cast<size_t>(regs.line_length_in) * component_size;
            read_buffer.resize_destructive(byte_count);
            u8* const bytes = read_buffer.data();
            if (component_size == 4) {
                // Retain the original optimized bulk fill for ordinary u32 clears.
                std::ranges::fill(std::span<u32>(reinterpret_cast<u32*>(bytes), regs.line_length_in),
                                  regs.remap_const.remap_consta_value);
            } else {
                const u32 value = regs.remap_const.remap_consta_value;
                for (u32 element = 0; element < regs.line_length_in; ++element) {
                    const size_t base = static_cast<size_t>(element) * component_size;
                    for (u32 byte = 0; byte < component_size; ++byte) {
                        bytes[base + byte] = static_cast<u8>(value >> (byte * 8));
                    }
                }
            }
            // CONST_A also modifies textures and shader-visible CPU pages.
            // WriteBlockUnsafe bypasses RasterizerVulkan::InvalidateRegion,
            // leaving TextureCache/BufferCache with old pixels after the clear.
            // BufferClear above may accelerate u32 fills, but guest memory
            // must still publish its CPU copy through normal cache ownership.
            memory_manager.WriteBlock(regs.offset_out, bytes, byte_count);
        } else {
            memory_manager.FlushCaching();
            const auto convert_linear_2_blocklinear_addr = [](u64 address) {
                return (address & ~0x1f0ULL) | ((address & 0x40) >> 2) | ((address & 0x10) << 1) |
                       ((address & 0x180) >> 1) | ((address & 0x20) << 3);
            };
            const auto src_kind = memory_manager.GetPageKind(regs.offset_in);
            const auto dst_kind = memory_manager.GetPageKind(regs.offset_out);
            const bool is_src_pitch = IsPitchKind(src_kind);
            const bool is_dst_pitch = IsPitchKind(dst_kind);
            if (is_src_pitch != is_dst_pitch) {
                // The B0B5 1D layout permutation preserves the low four address
                // bits. A copy is contiguous only up to the next 16-byte
                // microtile boundary; a final short chunk must never write a
                // full 16 bytes beyond LINE_LENGTH_IN.
                const bool src_tiled = !is_src_pitch;
                const GPUVAddr tiled_base = src_tiled ? regs.offset_in : regs.offset_out;
                const GPUVAddr linear_base = src_tiled ? regs.offset_out : regs.offset_in;
                const u64 length = regs.line_length_in;
                constexpr u64 MAX_ADDRESS = (std::numeric_limits<u64>::max)();
                if (tiled_base > MAX_ADDRESS - length ||
                    linear_base > MAX_ADDRESS - length) {
                    AssertFailSoftImpl();
                } else {
                    // B0B5 permutes only address bits 4..8 inside each
                    // 512-byte region. GPU VAs can map the same device pages
                    // without overlapping as virtual intervals; an address-
                    // only alias check corrupts unread source in that case.
                    const auto mapped_ranges_overlap =
                        [&](GPUVAddr first, u64 first_size, GPUVAddr second,
                            u64 second_size) -> bool {
                        const auto intersects = [](u64 a, u64 a_end,
                                                   u64 b, u64 b_end) {
                            return a < b_end && b < a_end;
                        };
                        if (memory_manager.IsContinuousRange(first,
                                static_cast<size_t>(first_size)) &&
                            memory_manager.IsContinuousRange(second,
                                static_cast<size_t>(second_size))) {
                            const auto a = memory_manager.GpuToCpuAddress(first);
                            const auto b = memory_manager.GpuToCpuAddress(second);
                            if (!a || !b ||
                                *a > MAX_ADDRESS - first_size ||
                                *b > MAX_ADDRESS - second_size) {
                                return true; // Unknown physical owner: snapshot.
                            }
                            return intersects(*a, *a + first_size,
                                              *b, *b + second_size);
                        }
                        using Interval = std::pair<u64, u64>;
                        const auto collect = [&](GPUVAddr base, u64 bytes,
                                                 std::vector<Interval>& out) {
                            constexpr u64 GPU_PAGE = 4096;
                            for (u64 offset = 0; offset < bytes;) {
                                const GPUVAddr address = base + offset;
                                const u64 count = (std::min)(
                                    bytes - offset,
                                    GPU_PAGE - (address & (GPU_PAGE - 1)));
                                const auto physical = memory_manager.GpuToCpuAddress(address);
                                if (!physical || *physical > MAX_ADDRESS - count) {
                                    return false;
                                }
                                out.emplace_back(*physical, *physical + count);
                                offset += count;
                            }
                            std::sort(out.begin(), out.end());
                            return true;
                        };
                        std::vector<Interval> first_segments;
                        std::vector<Interval> second_segments;
                        if (!collect(first, first_size, first_segments) ||
                            !collect(second, second_size, second_segments)) {
                            return true; // Unmapped/unknown pages may alias.
                        }
                        size_t i = 0;
                        size_t j = 0;
                        while (i < first_segments.size() && j < second_segments.size()) {
                            const auto& a = first_segments[i];
                            const auto& b = second_segments[j];
                            if (intersects(a.first, a.second, b.first, b.second)) {
                                return true;
                            }
                            if (a.second <= b.second) {
                                ++i;
                            } else {
                                ++j;
                            }
                        }
                        return false;
                    };
                    const bool virtual_alias = length != 0 &&
                        linear_base <= ((tiled_base + length - 1) | u64{0x1ff}) &&
                        linear_base + length - 1 >= (tiled_base & ~u64{0x1ff});
                    const u64 tiled_begin = tiled_base & ~u64{0x1ff};
                    const u64 tiled_end = length == 0 ? tiled_begin :
                        ((tiled_base + length - 1) | u64{0x1ff});
                    const bool may_alias = length != 0 &&
                        (virtual_alias ||
                         mapped_ranges_overlap(tiled_begin,
                             tiled_end - tiled_begin + 1, linear_base, length));
                    constexpr u64 MAX_ALIAS_SNAPSHOT = u64{16} << 20;
                    if (may_alias && length > MAX_ALIAS_SNAPSHOT) {
                        LOG_WARNING(Render_OpenGL, "Oversized overlapping 1D tiled DMA");
                        AssertFailSoftImpl();
                    } else if (may_alias) {
                        // Snapshot all source bytes before the first cached
                        // write; the next microtile may overlap the first
                        // destination when guest addresses alias.
                        std::vector<u8> snapshot(static_cast<size_t>(length));
                        bool mapped = true;
                        for (u64 offset = 0; offset < length;) {
                            const GPUVAddr tile = tiled_base + offset;
                            const size_t count = static_cast<size_t>(
                                (std::min)(length - offset, u64{16} - (tile & u64{15})));
                            const GPUVAddr permuted = convert_linear_2_blocklinear_addr(tile);
                            const GPUVAddr source = src_tiled ? permuted : linear_base + offset;
                            const GPUVAddr destination = src_tiled ? linear_base + offset : permuted;
                            if (!memory_manager.IsFullyMappedRange(source, count) ||
                                !memory_manager.IsFullyMappedRange(destination, count)) {
                                mapped = false;
                                break;
                            }
                            memory_manager.ReadBlock(source, snapshot.data() + offset, count);
                            offset += count;
                        }
                        if (!mapped) {
                            AssertFailSoftImpl(); // No partial destination writes.
                        } else {
                            for (u64 offset = 0; offset < length;) {
                                const GPUVAddr tile = tiled_base + offset;
                                const size_t count = static_cast<size_t>(
                                    (std::min)(length - offset, u64{16} - (tile & u64{15})));
                                const GPUVAddr permuted = convert_linear_2_blocklinear_addr(tile);
                                const GPUVAddr destination = src_tiled ? linear_base + offset : permuted;
                                memory_manager.WriteBlockCached(destination,
                                                                snapshot.data() + offset, count);
                                offset += count;
                            }
                        }
                    } else {
                        read_buffer.resize_destructive(16);
                        for (u64 offset = 0; offset < length;) {
                            const GPUVAddr tile = tiled_base + offset;
                            const size_t count = static_cast<size_t>(
                                (std::min)(length - offset, u64{16} - (tile & u64{15})));
                            const GPUVAddr permuted = convert_linear_2_blocklinear_addr(tile);
                            const GPUVAddr source = src_tiled ? permuted : linear_base + offset;
                            const GPUVAddr destination = src_tiled ? linear_base + offset : permuted;
                            Tegra::Memory::GpuGuestMemoryScoped<
                                u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
                                transfer(memory_manager, source, count, &read_buffer);
                            transfer.SetAddressAndSize(destination, count);
                            offset += count;
                        }
                    }
                }
            } else {
                if (!accelerate.BufferCopy(regs.offset_in, regs.offset_out, regs.line_length_in)) {
                    Tegra::Memory::GpuGuestMemoryScoped<
                        u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
                        tmp_write_buffer(memory_manager, regs.offset_in, regs.line_length_in,
                                         &read_buffer);
                    tmp_write_buffer.SetAddressAndSize(regs.offset_out, regs.line_length_in);
                }
            }
        }
    }

    ReleaseSemaphore();
}

bool MaxwellDMA::CopyBlockLinearRemap() {
    using Swizzle = RemapConst::Swizzle;
    const auto& remap = regs.remap_const;
    const u32 component_size = remap.component_size_minus_one + 1;
    const u32 src_count = remap.num_src_components_minus_one + 1;
    const u32 dst_count = remap.num_dst_components_minus_one + 1;
    // Real SET_REMAP_COMPONENTS fields are 2-bit counts; enforce the
    // four-selector ABI before indexing any destination slot.
    if (component_size == 0 || component_size > 4 ||
        src_count == 0 || src_count > 4 || dst_count == 0 || dst_count > 4) {
        return false;
    }
    const u64 src_bpp = static_cast<u64>(component_size) * src_count;
    const u64 dst_bpp = static_cast<u64>(component_size) * dst_count;
    const u32 blocks = regs.line_length_in;
    const u32 rows = regs.line_count;
    if (blocks == 0 || rows == 0) {
        return true;
    }

    // The shared subrectangle decoder's BPP=3/6/12 paths copy whole
    // pixels across 16-byte GOB microtiles. Those memcpy ranges can overlap
    // the next Y row. Decode every byte using its own GOB address instead:
    // width/origin/extent are scaled to bytes and BPP=1 in the decoder.
    // All 1..4 components of size 1..4 are then representable (1..16 B).
    if (src_bpp == 0 || dst_bpp == 0 || src_bpp > 16 || dst_bpp > 16) {
        return false;
    }

    bool needs_source = false;
    bool needs_destination = false;
    bool has_write = false;
    std::array<Swizzle, 4> selectors{};
    for (u32 i = 0; i < dst_count; ++i) {
        selectors[i] = remap.GetComponent(i);
        const auto value = static_cast<u32>(selectors[i]);
        if (selectors[i] == Swizzle::NO_WRITE) {
            needs_destination = true;
        } else if (selectors[i] == Swizzle::CONST_A ||
                   selectors[i] == Swizzle::CONST_B) {
            has_write = true;
        } else if (value < 4 && value < src_count) {
            has_write = true;
            needs_source = true;
        } else {
            return false;
        }
    }
    if (!has_write) {
        return true;
    }

    // Bound total temporary memory on the PS5. A failed admission must
    // return without changing any source or destination guest bytes.
    constexpr u64 MAX_BYTES = 16ULL << 20;
    const u64 src_row_bytes = static_cast<u64>(blocks) * src_bpp;
    const u64 dst_row_bytes = static_cast<u64>(blocks) * dst_bpp;
    if (dst_row_bytes > MAX_BYTES / rows ||
        (needs_source && src_row_bytes > MAX_BYTES / rows)) {
        return false;
    }
    const bool src_tiled =
        regs.launch_dma.src_memory_layout == LaunchDMA::MemoryLayout::BLOCKLINEAR;
    const bool dst_tiled =
        regs.launch_dma.dst_memory_layout == LaunchDMA::MemoryLayout::BLOCKLINEAR;

    // Guard the decoder's u32 width/height/offset calculations BEFORE
    // calling CalculateSize(), UnswizzleSubrect() or SwizzleSubrect().
    const auto valid_image = [&](const DMA::Parameters& image, u64 bpp, GPUVAddr address,
                                 size_t& byte_count) {
        const u64 origin_x = image.origin.x.Value();
        const u64 origin_y = image.origin.y.Value();
        const u32 block_height = image.block_size.height.Value();
        if (image.layer != 0 || image.depth != 1 ||
            image.block_size.width != 0 || image.block_size.depth != 0 ||
            block_height > 5 || image.width == 0 || image.height == 0 ||
            origin_x > image.width || origin_y > image.height ||
            blocks > image.width - origin_x ||
            rows > image.height - origin_y) {
            return false;
        }
        const u64 pixel_width = static_cast<u64>(image.width) * bpp;
        const u64 padded_width = (pixel_width + 63) & ~u64{63};
        const u64 gob_height = u64{8} << block_height;
        const u64 padded_height =
            (static_cast<u64>(image.height) + gob_height - 1) & ~(gob_height - 1);
        // Multiply after the cap check. A malformed guest can otherwise
        // overflow u64 before the decoder's image-allocation admission.
        if (padded_width == 0 || padded_height == 0 ||
            padded_width > MAX_BYTES / padded_height) {
            return false;
        }
        const u64 bytes = padded_width * padded_height;
        // Existing decoders address GOBs through u32 offsets and require
        // a correctly allocated full image, including its tile padding.
        if (bytes > (std::numeric_limits<u32>::max)() ||
            address > (std::numeric_limits<u64>::max)() - bytes) {
            return false;
        }
        byte_count = static_cast<size_t>(bytes);
        return memory_manager.IsFullyMappedRange(address, byte_count);
    };
    const auto row_addr = [&](GPUVAddr base, s32 pitch, u32 row, size_t bytes,
                              GPUVAddr& result) {
        const u64 magnitude = pitch < 0 ? static_cast<u64>(-static_cast<s64>(pitch))
                                        : static_cast<u64>(pitch);
        const u64 delta = magnitude * row;
        if (pitch < 0) {
            if (delta > base) {
                return false;
            }
            result = base - delta;
        } else {
            if (delta > (std::numeric_limits<u64>::max)() - base) {
                return false;
            }
            result = base + delta;
        }
        return bytes <= (std::numeric_limits<u64>::max)() - result &&
               memory_manager.IsFullyMappedRange(result, bytes);
    };

    size_t src_tiled_size{}, dst_tiled_size{};
    if ((needs_source && src_tiled &&
         !valid_image(regs.src_params, src_bpp, regs.offset_in, src_tiled_size)) ||
        (dst_tiled &&
         !valid_image(regs.dst_params, dst_bpp, regs.offset_out, dst_tiled_size))) {
        return false;
    }
    // Perform a complete address preflight before any destination write.
    GPUVAddr row_address{};
    for (u32 row = 0; row < rows; ++row) {
        if ((needs_source && !src_tiled &&
             !row_addr(regs.offset_in, regs.pitch_in, row,
                       static_cast<size_t>(src_row_bytes), row_address)) ||
            (!dst_tiled &&
             !row_addr(regs.offset_out, regs.pitch_out, row,
                       static_cast<size_t>(dst_row_bytes), row_address))) {
            return false;
        }
    }

    std::vector<u8> linear_src;
    if (needs_source) {
        linear_src.resize(static_cast<size_t>(src_row_bytes * rows));
        if (src_tiled) {
            std::vector<u8> tiled_src(src_tiled_size);
            memory_manager.ReadBlock(regs.offset_in, tiled_src.data(), tiled_src.size());
            UnswizzleSubrect(linear_src, tiled_src, 1,
                             static_cast<u32>(static_cast<u64>(regs.src_params.width) * src_bpp),
                             regs.src_params.height, 1,
                             static_cast<u32>(static_cast<u64>(regs.src_params.origin.x.Value()) *
                                              src_bpp),
                             regs.src_params.origin.y.Value(),
                             static_cast<u32>(src_row_bytes), rows,
                             regs.src_params.block_size.height.Value(), 0,
                             static_cast<u32>(src_row_bytes));
        } else {
            for (u32 row = 0; row < rows; ++row) {
                if (!row_addr(regs.offset_in, regs.pitch_in, row,
                              static_cast<size_t>(src_row_bytes), row_address)) {
                    return false;
                }
                memory_manager.ReadBlock(row_address,
                    linear_src.data() + static_cast<size_t>(row * src_row_bytes),
                    static_cast<size_t>(src_row_bytes));
            }
        }
    }

    std::vector<u8> linear_dst(static_cast<size_t>(dst_row_bytes * rows));
    std::vector<u8> tiled_dst;
    if (dst_tiled) {
        // Preserve all GOB bytes outside the destination rectangle, even
        // for a full four-component overwrite within that rectangle.
        tiled_dst.resize(dst_tiled_size);
        memory_manager.ReadBlock(regs.offset_out, tiled_dst.data(), tiled_dst.size());
        if (needs_destination) {
            UnswizzleSubrect(linear_dst, tiled_dst, 1,
                             static_cast<u32>(static_cast<u64>(regs.dst_params.width) * dst_bpp),
                             regs.dst_params.height, 1,
                             static_cast<u32>(static_cast<u64>(regs.dst_params.origin.x.Value()) *
                                              dst_bpp),
                             regs.dst_params.origin.y.Value(),
                             static_cast<u32>(dst_row_bytes), rows,
                             regs.dst_params.block_size.height.Value(), 0,
                             static_cast<u32>(dst_row_bytes));
        }
    } else if (needs_destination) {
        for (u32 row = 0; row < rows; ++row) {
            if (!row_addr(regs.offset_out, regs.pitch_out, row,
                          static_cast<size_t>(dst_row_bytes), row_address)) {
                return false;
            }
            memory_manager.ReadBlock(row_address,
                linear_dst.data() + static_cast<size_t>(row * dst_row_bytes),
                static_cast<size_t>(dst_row_bytes));
        }
    }

    // All source bytes are snapshotted before the first destination write;
    // overlapping src/dst GPU addresses cannot create read-after-write
    // corruption, and NO_WRITE retains the original destination component.
    for (u64 element = 0; element < static_cast<u64>(blocks) * rows; ++element) {
        const u8* src = needs_source ? linear_src.data() + element * src_bpp : nullptr;
        u8* dst = linear_dst.data() + element * dst_bpp;
        for (u32 slot = 0; slot < dst_count; ++slot) {
            const Swizzle selector = selectors[slot];
            u8* target = dst + slot * component_size;
            if (selector == Swizzle::NO_WRITE) {
                continue;
            }
            if (static_cast<u32>(selector) < src_count) {
                const u8* part = src + static_cast<u32>(selector) * component_size;
                std::copy_n(part, component_size, target);
            } else {
                const u32 value = selector == Swizzle::CONST_A
                                      ? remap.remap_consta_value
                                      : remap.remap_constb_value;
                for (u32 byte = 0; byte < component_size; ++byte) {
                    target[byte] = static_cast<u8>(value >> (byte * 8));
                }
            }
        }
    }
    if (dst_tiled) {
        SwizzleSubrect(tiled_dst, linear_dst, 1,
                       static_cast<u32>(static_cast<u64>(regs.dst_params.width) * dst_bpp),
                       regs.dst_params.height, 1,
                       static_cast<u32>(static_cast<u64>(regs.dst_params.origin.x.Value()) *
                                        dst_bpp),
                       regs.dst_params.origin.y.Value(),
                       static_cast<u32>(dst_row_bytes), rows,
                       regs.dst_params.block_size.height.Value(), 0,
                       static_cast<u32>(dst_row_bytes));
        memory_manager.WriteBlock(regs.offset_out, tiled_dst.data(), tiled_dst.size());
    } else {
        for (u32 row = 0; row < rows; ++row) {
            if (!row_addr(regs.offset_out, regs.pitch_out, row,
                          static_cast<size_t>(dst_row_bytes), row_address)) {
                return false;
            }
            memory_manager.WriteBlock(row_address,
                linear_dst.data() + static_cast<size_t>(row * dst_row_bytes),
                static_cast<size_t>(dst_row_bytes));
        }
    }
    return true;
}

void MaxwellDMA::CopyBlockLinearToPitch() {
   

    u32 bytes_per_pixel = 1;
    DMA::ImageOperand src_operand;
    src_operand.bytes_per_pixel = bytes_per_pixel;
    src_operand.params = regs.src_params;
    src_operand.address = regs.offset_in;

    DMA::BufferOperand dst_operand;
    // Widen before absolute value: s32 minimum cannot be negated in s32.
    dst_operand.pitch = static_cast<u32>(std::abs(static_cast<s64>(regs.pitch_out)));
    dst_operand.width = regs.line_length_in;
    dst_operand.height = regs.line_count;
    dst_operand.address = regs.offset_out;
    DMA::ImageCopy copy_info{};
    copy_info.length_x = regs.line_length_in;
    copy_info.length_y = regs.line_count;
    auto& accelerate = rasterizer->AccessAccelerateDMA();
    if (accelerate.ImageToBuffer(copy_info, src_operand, dst_operand)) {
        return;
    }

    UNIMPLEMENTED_IF(regs.src_params.block_size.width != 0);
    UNIMPLEMENTED_IF(regs.src_params.block_size.depth != 0);
    UNIMPLEMENTED_IF(regs.src_params.block_size.depth == 0 && regs.src_params.depth != 1);

    // Deswizzle the input and copy it over.
    const DMA::Parameters& src_params = regs.src_params;

    const bool is_remapping = regs.launch_dma.remap_enable != 0;

    const u32 num_remap_components = regs.remap_const.num_dst_components_minus_one + 1;
    const u32 remap_components_size = regs.remap_const.component_size_minus_one + 1;

    const u32 base_bpp = !is_remapping ? 1U : num_remap_components * remap_components_size;

    u32 width = src_params.width;
    u32 x_elements = regs.line_length_in;
    u32 x_offset = src_params.origin.x;
    u32 bpp_shift = 0U;
    if (!is_remapping) {
        bpp_shift = Common::FoldRight(
            4U, [](u32 x, u32 y) { return (std::min)(x, static_cast<u32>(std::countr_zero(y))); },
            width, x_elements, x_offset, static_cast<u32>(regs.offset_in));
        width >>= bpp_shift;
        x_elements >>= bpp_shift;
        x_offset >>= bpp_shift;
    }

    bytes_per_pixel = base_bpp << bpp_shift;
    const u32 height = src_params.height;
    const u32 depth = src_params.depth;
    const u32 block_height = src_params.block_size.height;
    const u32 block_depth = src_params.block_size.depth;
    const size_t src_size =
        CalculateSize(true, bytes_per_pixel, width, height, depth, block_height, block_depth);

    const u64 dst_size_wide = static_cast<u64>(dst_operand.pitch) * regs.line_count;
    if (dst_size_wide > static_cast<u64>(~u32{0})) {
        LOG_WARNING(Render_OpenGL, "Oversized DMA linear destination");
        return;
    }
    const size_t dst_size = static_cast<size_t>(dst_size_wide);

    Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> tmp_read_buffer(
        memory_manager, src_operand.address, src_size, &read_buffer);
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::UnsafeReadCachedWrite>
        tmp_write_buffer(memory_manager, dst_operand.address, dst_size, &write_buffer);

    UnswizzleSubrect(tmp_write_buffer, tmp_read_buffer, bytes_per_pixel, width, height, depth,
                     x_offset, src_params.origin.y, x_elements, regs.line_count, block_height,
                     block_depth, dst_operand.pitch);
}

void MaxwellDMA::CopyPitchToBlockLinear() {
    UNIMPLEMENTED_IF_MSG(regs.dst_params.block_size.width != 0, "Block width is not one");
    UNIMPLEMENTED_IF(regs.dst_params.layer != 0);

    const bool is_remapping = regs.launch_dma.remap_enable != 0;
    const u32 num_remap_components = regs.remap_const.num_dst_components_minus_one + 1;
    const u32 remap_components_size = regs.remap_const.component_size_minus_one + 1;

    u32 bytes_per_pixel = 1;
    DMA::ImageOperand dst_operand;
    dst_operand.bytes_per_pixel = bytes_per_pixel;
    dst_operand.params = regs.dst_params;
    dst_operand.address = regs.offset_out;
    DMA::BufferOperand src_operand;
    src_operand.pitch = regs.pitch_in;
    src_operand.width = regs.line_length_in;
    src_operand.height = regs.line_count;
    src_operand.address = regs.offset_in;
    DMA::ImageCopy copy_info{};
    copy_info.length_x = regs.line_length_in;
    copy_info.length_y = regs.line_count;
    auto& accelerate = rasterizer->AccessAccelerateDMA();
    if (accelerate.BufferToImage(copy_info, src_operand, dst_operand)) {
        return;
    }

    const auto& dst_params = regs.dst_params;

    const u32 base_bpp = !is_remapping ? 1U : num_remap_components * remap_components_size;

    u32 width = dst_params.width;
    u32 x_elements = regs.line_length_in;
    u32 x_offset = dst_params.origin.x;
    u32 bpp_shift = 0U;
    if (!is_remapping) {
        bpp_shift = Common::FoldRight(
            4U, [](u32 x, u32 y) { return (std::min)(x, static_cast<u32>(std::countr_zero(y))); },
            width, x_elements, x_offset, static_cast<u32>(regs.offset_out));
        width >>= bpp_shift;
        x_elements >>= bpp_shift;
        x_offset >>= bpp_shift;
    }

    bytes_per_pixel = base_bpp << bpp_shift;
    const u32 height = dst_params.height;
    const u32 depth = dst_params.depth;
    const u32 block_height = dst_params.block_size.height;
    const u32 block_depth = dst_params.block_size.depth;
    const size_t dst_size =
        CalculateSize(true, bytes_per_pixel, width, height, depth, block_height, block_depth);
    const u64 src_size_wide = static_cast<u64>(regs.pitch_in) * regs.line_count;
    if (src_size_wide > static_cast<u64>(~u32{0})) {
        LOG_WARNING(Render_OpenGL, "Oversized DMA linear source");
        return;
    }
    const size_t src_size = static_cast<size_t>(src_size_wide);

    GPUVAddr src_addr = regs.offset_in;
    GPUVAddr dst_addr = regs.offset_out;
    Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> tmp_read_buffer(
        memory_manager, src_addr, src_size, &read_buffer);
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::UnsafeReadCachedWrite>
        tmp_write_buffer(memory_manager, dst_addr, dst_size, &write_buffer);

    //  If the input is linear and the output is tiled, swizzle the input and copy it over.
    SwizzleSubrect(tmp_write_buffer, tmp_read_buffer, bytes_per_pixel, width, height, depth,
                   x_offset, dst_params.origin.y, x_elements, regs.line_count, block_height,
                   block_depth, regs.pitch_in);
}

void MaxwellDMA::CopyBlockLinearToBlockLinear() {
    UNIMPLEMENTED_IF(regs.src_params.block_size.width != 0);

    const bool is_remapping = regs.launch_dma.remap_enable != 0;

    // Deswizzle the input and copy it over.
    const DMA::Parameters& src = regs.src_params;
    const DMA::Parameters& dst = regs.dst_params;

    const u32 num_remap_components = regs.remap_const.num_dst_components_minus_one + 1;
    const u32 remap_components_size = regs.remap_const.component_size_minus_one + 1;

    const u32 base_bpp = !is_remapping ? 1U : num_remap_components * remap_components_size;

    u32 src_width = src.width;
    u32 dst_width = dst.width;
    u32 x_elements = regs.line_length_in;
    u32 src_x_offset = src.origin.x;
    u32 dst_x_offset = dst.origin.x;
    u32 bpp_shift = 0U;
    if (!is_remapping) {
        bpp_shift = Common::FoldRight(
            4U, [](u32 x, u32 y) { return (std::min)(x, static_cast<u32>(std::countr_zero(y))); },
            src_width, dst_width, x_elements, src_x_offset, dst_x_offset,
            static_cast<u32>(regs.offset_in), static_cast<u32>(regs.offset_out));
        src_width >>= bpp_shift;
        dst_width >>= bpp_shift;
        x_elements >>= bpp_shift;
        src_x_offset >>= bpp_shift;
        dst_x_offset >>= bpp_shift;
    }

    const u32 bytes_per_pixel = base_bpp << bpp_shift;
    const size_t src_size = CalculateSize(true, bytes_per_pixel, src_width, src.height, src.depth,
                                          src.block_size.height, src.block_size.depth);
    const size_t dst_size = CalculateSize(true, bytes_per_pixel, dst_width, dst.height, dst.depth,
                                          dst.block_size.height, dst.block_size.depth);

    const u64 pitch_wide = static_cast<u64>(x_elements) * bytes_per_pixel;
    const u64 max_transfer = static_cast<u64>(~u32{0});
    if (pitch_wide > max_transfer ||
        (regs.line_count != 0 && pitch_wide > max_transfer / regs.line_count)) {
        LOG_WARNING(Render_OpenGL, "Oversized DMA intermediate buffer");
        return;
    }
    const u32 pitch = static_cast<u32>(pitch_wide);
    const size_t mid_buffer_size = static_cast<size_t>(pitch_wide * regs.line_count);

    intermediate_buffer.resize_destructive(mid_buffer_size);

    Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> tmp_read_buffer(
        memory_manager, regs.offset_in, src_size, &read_buffer);
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
        tmp_write_buffer(memory_manager, regs.offset_out, dst_size, &write_buffer);

    UnswizzleSubrect(intermediate_buffer, tmp_read_buffer, bytes_per_pixel, src_width, src.height,
                     src.depth, src_x_offset, src.origin.y, x_elements, regs.line_count,
                     src.block_size.height, src.block_size.depth, pitch);

    SwizzleSubrect(tmp_write_buffer, intermediate_buffer, bytes_per_pixel, dst_width, dst.height,
                   dst.depth, dst_x_offset, dst.origin.y, x_elements, regs.line_count,
                   dst.block_size.height, dst.block_size.depth, pitch);
}

void MaxwellDMA::ReleaseSemaphore() {
    const auto type = regs.launch_dma.semaphore_type;
    const GPUVAddr address = regs.semaphore.address;
    const u32 payload = regs.semaphore.payload;
    VideoCommon::QueryPropertiesFlags flags{VideoCommon::QueryPropertiesFlags::IsAFence};
    switch (type) {
    case LaunchDMA::SemaphoreType::NONE:
        break;
    case LaunchDMA::SemaphoreType::RELEASE_ONE_WORD_SEMAPHORE: {
        rasterizer->Query(address, VideoCommon::QueryType::Payload, flags, payload, 0);
        break;
    }
    case LaunchDMA::SemaphoreType::RELEASE_FOUR_WORD_SEMAPHORE: {
        rasterizer->Query(address, VideoCommon::QueryType::Payload,
                          flags | VideoCommon::QueryPropertiesFlags::HasTimeout, payload, 0);
        break;
    }
    default:
        ASSERT_MSG(false, "Unknown semaphore type: {}", static_cast<u32>(type.Value()));
        break;
    }
}

} // namespace Tegra::Engines
