// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
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

void State::ProcessData(const u32 data, const bool is_last_call) {
    if (copy_size == 0 || write_offset >= copy_size) {
        return;
    }
    const u32 sub_copy_size = (std::min)(4U, copy_size - write_offset);
    std::memcpy(inner_buffer.data() + write_offset, &data, sub_copy_size);
    write_offset += sub_copy_size;
    // A last-call marker on a truncated pushbuffer must never publish
    // unreceived scratch bytes into guest textures or shader input.
    if (is_last_call && write_offset == copy_size) {
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
    if (is_linear) {
        for (size_t line = 0; line < regs.line_count; ++line) {
            const GPUVAddr dest_line = address + line * regs.dest.pitch;
            std::span<const u8> buffer(read_buffer.data() + line * regs.line_length_in,
                                       regs.line_length_in);
            rasterizer->AccelerateInlineToMemory(dest_line, regs.line_length_in, buffer);
        }
    } else {
        u32 width = regs.dest.width;
        u32 x_elements = regs.line_length_in;
        u32 x_offset = regs.dest.x;
        const u32 bpp_shift = Common::FoldRight(
            4U, [](u32 x, u32 y) { return (std::min)(x, static_cast<u32>(std::countr_zero(y))); },
            width, x_elements, x_offset, static_cast<u32>(address));
        width >>= bpp_shift;
        x_elements >>= bpp_shift;
        x_offset >>= bpp_shift;
        const u32 bytes_per_pixel = 1U << bpp_shift;
        const std::size_t dst_size = Tegra::Texture::CalculateSize(
            true, bytes_per_pixel, width, regs.dest.height, regs.dest.depth,
            regs.dest.BlockHeight(), regs.dest.BlockDepth());

        Tegra::Memory::GpuGuestMemoryScoped<u8,
                                            Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
            tmp(memory_manager, address, dst_size, &tmp_buffer);

        Tegra::Texture::SwizzleSubrect(tmp, read_buffer, bytes_per_pixel, width, regs.dest.height,
                                       regs.dest.depth, x_offset, regs.dest.y, x_elements,
                                       regs.line_count, regs.dest.BlockHeight(),
                                       regs.dest.BlockDepth(), regs.line_length_in,
                                       memory_manager.UsesNativeTiling());
    }
}

} // namespace Tegra::Engines::Upload
