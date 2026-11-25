// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>
#include <span>

#include "common/algorithm.h"
#include "common/assert.h"
#include "video_core/engines/engine_upload.h"
#include "video_core/guest_memory.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines::Upload {

State::State(MemoryManager& memory_manager_, Registers& regs_)
    : regs{regs_}, memory_manager{memory_manager_} {}

State::~State() = default;

void State::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
}

void State::ProcessExec(const bool is_linear_) {
    write_offset = 0;
    copy_size = 0;
    is_linear = is_linear_;
    // Inline uploads are received as u32 words, but the guest controls both
    // dimensions. Do not wrap a 32-bit product into a small host allocation.
    // This is staging, not an unbounded guest memory reservation.
    constexpr u64 MaxInlineUploadBytes = u64{64} << 20;
    const u64 bytes = static_cast<u64>(regs.line_length_in) * regs.line_count;
    if (bytes == 0 || bytes > MaxInlineUploadBytes) {
        inner_buffer.resize_destructive(0);
        return;
    }
    copy_size = static_cast<u32>(bytes);
    inner_buffer.resize_destructive(copy_size);
}

void State::ProcessData(const u32 data, const bool /*is_last_call*/) {
    if (copy_size == 0 || write_offset >= copy_size) {
        return;
    }
    const u32 sub_copy_size = (std::min)(4U, copy_size - write_offset);
    std::memcpy(inner_buffer.data() + write_offset, &data, sub_copy_size);
    write_offset += sub_copy_size;
    // The last-call marker ends a GPFIFO method group, not necessarily
    // the upload. Publish only when ALL requested bytes have arrived,
    // including when scalar words finish before the group terminates.
    if (write_offset == copy_size) {
        ProcessData(inner_buffer);
    }
}

void State::ProcessData(const u32* data, size_t num_data) {
    if (copy_size == 0 || write_offset >= copy_size || data == nullptr || num_data == 0) {
        return;
    }
    const size_t remaining = copy_size - write_offset;
    // Limit the word multiplication before converting a potentially enormous
    // guest command count to bytes. Each packet may be only part of an upload.
    const size_t words = (std::min)(num_data, (remaining + 3) / sizeof(u32));
    const size_t received = (std::min)(remaining, words * sizeof(u32));
    const auto* const bytes = reinterpret_cast<const u8*>(data);
    if (write_offset == 0 && received == copy_size) {
        // Normal complete pushbuffer path: keep the original zero-copy read.
        write_offset = copy_size;
        ProcessData(std::span<const u8>{bytes, copy_size});
        return;
    }
    std::memcpy(inner_buffer.data() + write_offset, bytes, received);
    write_offset += static_cast<u32>(received);
    if (write_offset == copy_size) {
        ProcessData(inner_buffer);
    }
}

void State::ProcessData(std::span<const u8> read_buffer) {
    if (!rasterizer || copy_size == 0 || read_buffer.size() < copy_size) {
        return;
    }
    const GPUVAddr address{regs.dest.Address()};
    constexpr u64 MaxInlineUploadBytes = u64{64} << 20;
    constexpr u64 MaxAddress = (std::numeric_limits<u64>::max)();
    if (is_linear) {
        // The staging size is already bounded by ProcessExec. Prove the last
        // pitched guest row cannot wrap the GPU VA before ANY row is written.
        const u64 last_offset =
            (static_cast<u64>(regs.line_count) - 1) * regs.dest.pitch;
        if (regs.line_length_in > MaxAddress - address ||
            last_offset > MaxAddress - address - regs.line_length_in) {
            return;
        }
        for (size_t line = 0; line < regs.line_count; ++line) {
            const GPUVAddr dest_line = address + line * regs.dest.pitch;
            std::span<const u8> buffer(read_buffer.data() + line * regs.line_length_in,
                                       regs.line_length_in);
            rasterizer->AccelerateInlineToMemory(dest_line, regs.line_length_in, buffer);
        }
        return;
    }

    // The public GOB decoder uses u32 offsets and assumes full-height Z
    // strides. Validate the entire surface, Y/Z extent and the exact GMMU
    // destination before constructing a writable guest-memory span.
    const u32 height = regs.dest.height;
    const u32 depth = regs.dest.depth;
    const u32 origin_y = regs.dest.y;
    const u32 origin_z = regs.dest.layer;
    const u32 block_height = regs.dest.BlockHeight();
    const u32 block_depth = regs.dest.BlockDepth();
    if (regs.dest.width == 0 || height == 0 || depth == 0 ||
        regs.line_length_in == 0 || regs.line_count == 0 ||
        regs.dest.BlockWidth() != 0 || block_height > 5 || block_depth > 5 ||
        origin_y >= height || origin_z >= depth ||
        (origin_z != 0 && (depth <= 1 || block_depth == 0)) ||
        regs.dest.x > regs.dest.width ||
        regs.line_length_in > regs.dest.width - regs.dest.x) {
        return;
    }
    const u64 lines_per_slice = static_cast<u64>(height) - origin_y;
    if (static_cast<u64>(regs.line_count) >
        lines_per_slice * (static_cast<u64>(depth) - origin_z)) {
        return;
    }

    u32 width = regs.dest.width;
    u32 x_elements = regs.line_length_in;
    u32 x_offset = regs.dest.x;
    const u32 bpp_shift = Common::FoldRight(
        4U, [](u32 x, u32 y) {
            return (std::min)(x, static_cast<u32>(std::countr_zero(y)));
        },
        width, x_elements, x_offset, static_cast<u32>(address));
    width >>= bpp_shift;
    x_elements >>= bpp_shift;
    x_offset >>= bpp_shift;
    const u32 bytes_per_pixel = 1U << bpp_shift;

    const u64 aligned_width = (static_cast<u64>(width) * bytes_per_pixel + 63) & ~u64{63};
    const u64 gob_height = u64{8} << block_height;
    const u64 aligned_height = (static_cast<u64>(height) + gob_height - 1) &
                               ~(gob_height - 1);
    const u64 gob_depth = u64{1} << block_depth;
    const u64 aligned_depth = (static_cast<u64>(depth) + gob_depth - 1) &
                              ~(gob_depth - 1);
    const u64 slices_used = 1 + (static_cast<u64>(regs.line_count) - 1) / lines_per_slice;
    const u64 last_slice_lines = static_cast<u64>(regs.line_count) -
                                 (slices_used - 1) * lines_per_slice;
    const u64 source_rows = (slices_used - 1) * height + last_slice_lines;
    if (aligned_width == 0 || aligned_height == 0 || aligned_depth == 0 ||
        aligned_width > MaxInlineUploadBytes / aligned_height ||
        aligned_width * aligned_height > MaxInlineUploadBytes / aligned_depth ||
        source_rows > MaxInlineUploadBytes / regs.line_length_in) {
        return;
    }
    const u64 destination_bytes = aligned_width * aligned_height * aligned_depth;
    const size_t dst_size = Tegra::Texture::CalculateSize(
        true, bytes_per_pixel, width, height, depth, block_height, block_depth);
    if (dst_size != destination_bytes || address > MaxAddress - dst_size ||
        !memory_manager.IsFullyMappedRange(address, dst_size)) {
        return;
    }

    // Pushbuffer inline words are compact even when a 3D subrectangle starts
    // partway down every Z slice. Expand only the missing full-height gaps so
    // SwizzleSubrect never reads past the received data on later slices.
    std::span<const u8> source = read_buffer.first(copy_size);
    std::vector<u8> padded_source;
    const u64 source_bytes = source_rows * regs.line_length_in;
    if (source_bytes != copy_size) {
        padded_source.resize(static_cast<size_t>(source_bytes));
        u64 copied_rows = 0;
        for (u64 slice = 0; slice < slices_used; ++slice) {
            const u64 slice_rows = (std::min)(
                lines_per_slice, static_cast<u64>(regs.line_count) - copied_rows);
            std::memcpy(padded_source.data() + slice * height * regs.line_length_in,
                        source.data() + copied_rows * regs.line_length_in,
                        static_cast<size_t>(slice_rows * regs.line_length_in));
            copied_rows += slice_rows;
        }
        source = std::span<const u8>{padded_source.data(), padded_source.size()};
    }

    Tegra::Memory::GpuGuestMemoryScoped<
        u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
        tmp(memory_manager, address, dst_size, &tmp_buffer);
    Tegra::Texture::SwizzleSubrect(
        tmp, source, bytes_per_pixel, width, height, depth, x_offset, origin_y,
        x_elements, regs.line_count, block_height, block_depth, regs.line_length_in,
        memory_manager.UsesNativeTiling(), origin_z);
}

} // namespace Tegra::Engines::Upload
