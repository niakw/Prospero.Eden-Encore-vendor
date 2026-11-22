// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <limits>

#include "common/assert.h"
#include "common/logging.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/sw_blitter/blitter.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/surface.h"
#include "video_core/textures/decoders.h"

using VideoCore::Surface::BytesPerBlock;
using VideoCore::Surface::PixelFormatFromRenderTargetFormat;

namespace Tegra::Engines {

using namespace Texture;

Fermi2D::Fermi2D(MemoryManager& memory_manager_) : memory_manager{memory_manager_} {
    sw_blitter = std::make_unique<Blitter::SoftwareBlitEngine>(memory_manager);
    // Nvidia's OpenGL driver seems to assume these values
    regs.src.depth = 1;
    regs.dst.depth = 1;

    execution_mask.reset();
    execution_mask[FERMI2D_REG_INDEX(pixels_from_memory.src_y0) + 1] = true;
}

Fermi2D::~Fermi2D() = default;

void Fermi2D::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
}

void Fermi2D::CallMethod(Core::System& system, u32 method, u32 method_argument, bool is_last_call) {
    // Assertions can be soft in release. Reject the guest method before
    // indexing the register file; an out-of-range write corrupts emulator state.
    if (method >= Regs::NUM_REGS) {
        ASSERT_MSG(false, "Invalid Fermi2D register, increase the size of the Regs structure");
        return;
    }
    regs.reg_array[method] = method_argument;

    if (method == FERMI2D_REG_INDEX(pixels_from_memory.src_y0) + 1) {
        Blit();
    }
}

void Fermi2D::CallMultiMethod(Core::System& system, u32 method, const u32* base_start, u32 amount, u32 methods_pending) {
    for (u32 i = 0; i < amount; ++i) {
        CallMethod(system, method, base_start[i], methods_pending - i <= 1);
    }
}

void Fermi2D::ConsumeSinkImpl(Core::System& system) {
    for (auto [method, value] : method_sink) {
        if (method >= Regs::NUM_REGS) {
            ASSERT_MSG(false, "Invalid deferred Fermi2D register");
            continue;
        }
        regs.reg_array[method] = value;
    }
    method_sink.clear();
}

void Fermi2D::Blit() {
    LOG_DEBUG(HW_GPU, "called. source address={:#x}, destination address={:#x}", regs.src.Address(), regs.dst.Address());

    // Never treat an unsupported 2D operation as a SrcCopy: soft
    // assertions return in release and would silently overwrite guest RAM.
    if (regs.operation != Operation::SrcCopy) {
        UNIMPLEMENTED_MSG("Operation is not copy");
        return;
    }
    // NVIDIA 2D pitch surfaces have contiguous, pitch*height-sized slices.
    // Support only explicitly addressed layers of two pitch-linear images.
    // Block-linear layers require a separate verified origin_z decoder.
    const bool pitch_layer_copy = regs.src.layer != 0 || regs.dst.layer != 0;
    if (pitch_layer_copy) {
        const auto valid_pitch_layers = [](const Surface& surface) {
            constexpr u64 MAX_PLANE_BYTES = 256ULL << 20;
            const u64 bpp = BytesPerBlock(
                PixelFormatFromRenderTargetFormat(surface.format));
            if (surface.linear != MemoryLayout::Pitch || surface.depth == 0 ||
                surface.layer >= surface.depth || surface.width == 0 ||
                surface.height == 0 || surface.pitch == 0 || bpp == 0 ||
                bpp > 16 || static_cast<u64>(surface.width) * bpp > surface.pitch) {
                return false;
            }
            const u64 plane = static_cast<u64>(surface.pitch) * surface.height;
            return plane != 0 && plane <= MAX_PLANE_BYTES;
        };
        if (!valid_pitch_layers(regs.src) || !valid_pitch_layers(regs.dst) ||
            regs.src.format != regs.dst.format || regs.clip_enable != 0) {
            UNIMPLEMENTED_MSG("Unsupported layered Fermi2D surface or operation");
            return;
        }
    } else if (regs.src.depth == 0 || regs.dst.depth == 0) {
        UNIMPLEMENTED_MSG("Invalid zero-depth Fermi2D surface");
        return;
    }

    const auto& args = regs.pixels_from_memory;
    constexpr s64 null_derivative = 1ULL << 32;
    Surface src = regs.src;
    const auto bytes_per_pixel = BytesPerBlock(PixelFormatFromRenderTargetFormat(src.format));
    const bool delegate_to_gpu = src.width > 512 && src.height > 512 && bytes_per_pixel <= 8 &&
                                 src.format != regs.dst.format;

    auto srcX = args.src_x0;
    auto srcY = args.src_y0;
    if (args.sample_mode.origin == Origin::Corner) {
        // Shift-left of a negative signed derivative is undefined in C++.
        // The integer-pixel correction has the same value as the original
        // 32.32 expression, but must not overflow the signed guest range.
        const s64 adjust_x = (args.du_dx >> 33) * (s64{1} << 32);
        const s64 adjust_y = (args.dv_dy >> 33) * (s64{1} << 32);
        if (__builtin_sub_overflow(srcX, adjust_x, &srcX) ||
            __builtin_sub_overflow(srcY, adjust_y, &srcY)) {
            AssertFailSoftImpl();
            return;
        }
    }

    // Compute the 32.32 blit extents only after validating signed guest
    // arithmetic. The unclipped fallback previously multiplied two guest
    // values before checking overflow, and added two signed s32 coordinates.
    const bool clip_enabled = regs.clip_enable != 0;
    s32 dst_end_x = args.dst_x0;
    s32 dst_end_y = args.dst_y0;
    s32 src_end_x = static_cast<s32>(srcX >> 32);
    s32 src_end_y = static_cast<s32>(srcY >> 32);
    if (!clip_enabled) {
        const s64 dst_x_wide = static_cast<s64>(args.dst_x0) + args.dst_width;
        const s64 dst_y_wide = static_cast<s64>(args.dst_y0) + args.dst_height;
        s64 step_x{}, step_y{}, far_x{}, far_y{};
        if (dst_x_wide < (std::numeric_limits<s32>::min)() ||
            dst_x_wide > (std::numeric_limits<s32>::max)() ||
            dst_y_wide < (std::numeric_limits<s32>::min)() ||
            dst_y_wide > (std::numeric_limits<s32>::max)() ||
            __builtin_mul_overflow(args.du_dx, static_cast<s64>(args.dst_width), &step_x) ||
            __builtin_mul_overflow(args.dv_dy, static_cast<s64>(args.dst_height), &step_y) ||
            __builtin_add_overflow(srcX, step_x, &far_x) ||
            __builtin_add_overflow(srcY, step_y, &far_y)) {
            AssertFailSoftImpl();
            return;
        }
        dst_end_x = static_cast<s32>(dst_x_wide);
        dst_end_y = static_cast<s32>(dst_y_wide);
        src_end_x = static_cast<s32>(far_x >> 32);
        src_end_y = static_cast<s32>(far_y >> 32);
    }
    Config config{
        .operation = regs.operation,
        .filter = args.sample_mode.filter,
        .must_accelerate =
            args.du_dx != null_derivative || args.dv_dy != null_derivative || delegate_to_gpu,
        .dst_x0 = args.dst_x0,
        .dst_y0 = args.dst_y0,
        .dst_x1 = dst_end_x,
        .dst_y1 = dst_end_y,
        .src_x0 = static_cast<s32>(srcX >> 32),
        .src_y0 = static_cast<s32>(srcY >> 32),
        .src_x1 = src_end_x,
        .src_y1 = src_end_y,
    };

    // NVIDIA 2D CLIP_X/Y/W/H is a destination scissor, not a change to
    // the source image origin. Restrict the destination while advancing the
    // source coordinates with the guest's original signed 32.32 derivatives.
    // This is shared Maxwell/Fermi2D semantics, independent of the title.
    if (regs.clip_enable != 0) {
        const s64 left = (std::max)(static_cast<s64>(args.dst_x0),
                                     static_cast<s64>(regs.clip_x0));
        const s64 top = (std::max)(static_cast<s64>(args.dst_y0),
                                    static_cast<s64>(regs.clip_y0));
        const s64 right = (std::min)(static_cast<s64>(args.dst_x0) + args.dst_width,
                                      static_cast<s64>(regs.clip_x0) + regs.clip_width);
        const s64 bottom = (std::min)(static_cast<s64>(args.dst_y0) + args.dst_height,
                                       static_cast<s64>(regs.clip_y0) + regs.clip_height);
        if (right <= left || bottom <= top) {
            return; // Empty destination intersection: no memory writes.
        }
        // Avoid overflow in large/malformed guest rectangles. A 32.32 source
        // coordinate must fit both signed 64-bit arithmetic and the existing
        // signed 32-bit blit rect that consumes its integer component.
        const auto clipped_source = [](s64 origin, s64 derivative, s64 offset,
                                       s32& result) -> bool {
            s64 scaled{};
            s64 position{};
            if (__builtin_mul_overflow(derivative, offset, &scaled) ||
                __builtin_add_overflow(origin, scaled, &position)) {
                return false;
            }
            const s64 integer = position >> 32;
            if (integer < static_cast<s64>((std::numeric_limits<s32>::min)()) ||
                integer > static_cast<s64>((std::numeric_limits<s32>::max)())) {
                return false;
            }
            result = static_cast<s32>(integer);
            return true;
        };
        if (right > (std::numeric_limits<s32>::max)() ||
            bottom > (std::numeric_limits<s32>::max)() ||
            !clipped_source(srcX, args.du_dx, left - args.dst_x0, config.src_x0) ||
            !clipped_source(srcX, args.du_dx, right - args.dst_x0, config.src_x1) ||
            !clipped_source(srcY, args.dv_dy, top - args.dst_y0, config.src_y0) ||
            !clipped_source(srcY, args.dv_dy, bottom - args.dst_y0, config.src_y1)) {
            AssertFailSoftImpl();
            return; // Never reinterpret overflow as a valid guest image span.
        }
        config.dst_x0 = static_cast<s32>(left);
        config.dst_x1 = static_cast<s32>(right);
        config.dst_y0 = static_cast<s32>(top);
        config.dst_y1 = static_cast<s32>(bottom);
        // The software fallback treats extents as unsigned. Never pass a
        // negative/reversed/out-of-surface source rectangle to that decoder
        // if this destination scissor exposes an unsupported sample range.
        if (config.src_x0 < 0 || config.src_y0 < 0 ||
            config.src_x1 <= config.src_x0 || config.src_y1 <= config.src_y0 ||
            static_cast<u64>(config.src_x1) > src.width ||
            static_cast<u64>(config.src_y1) > src.height ||
            static_cast<u64>(config.dst_x1) > regs.dst.width ||
            static_cast<u64>(config.dst_y1) > regs.dst.height) {
            AssertFailSoftImpl();
            return;
        }
    }

    const auto need_align_to_pitch =
        bytes_per_pixel != 0 &&
        src.linear == Tegra::Engines::Fermi2D::MemoryLayout::Pitch &&
        static_cast<s32>(src.width) == config.src_x1 &&
        config.src_x1 > static_cast<s32>(src.pitch / bytes_per_pixel) && config.src_x0 > 0;
    if (need_align_to_pitch) {
        // Widen both operands before multiplying, not merely the result.
        // Reject a guest address that would wrap when advancing the row.
        const u64 x_offset = static_cast<u64>(config.src_x0) *
                             static_cast<u64>(bytes_per_pixel);
        const u64 base_address = src.Address();
        if (x_offset > ~u64{0} - base_address) {
            AssertFailSoftImpl();
            return;
        }
        const u64 address = base_address + x_offset;
        src.addr_upper = static_cast<u32>(address >> 32);
        src.addr_lower = static_cast<u32>(address);
        src.width -= config.src_x0;
        config.src_x1 -= config.src_x0;
        config.src_x0 = 0;
    }

    memory_manager.FlushCaching();
    if (pitch_layer_copy) {
        // Copy ONLY the selected pitch-linear slice. The software blitter
        // snapshots the source before constructing the destination writer,
        // preserving overlapping guest surfaces. Do not route a layered
        // operation to TextureCache's layer-zero Vulkan acceleration.
        Surface dst = regs.dst;
        const auto select_pitch_layer = [this](Surface& surface) {
            constexpr u64 MAX_PLANE_BYTES = 256ULL << 20;
            const u64 plane = static_cast<u64>(surface.pitch) * surface.height;
            if (plane == 0 || plane > MAX_PLANE_BYTES ||
                surface.depth == 0 || surface.layer >= surface.depth ||
                (surface.layer != 0 && plane > ~u64{0} / surface.layer)) {
                return false;
            }
            const u64 offset = plane * surface.layer;
            const GPUVAddr base = surface.Address();
            if (base > ~u64{0} - offset ||
                base + offset > ~u64{0} - plane ||
                !memory_manager.IsFullyMappedRange(base + offset,
                                                   static_cast<size_t>(plane))) {
                return false;
            }
            const GPUVAddr selected = base + offset;
            surface.addr_upper = static_cast<u32>(selected >> 32);
            surface.addr_lower = static_cast<u32>(selected);
            surface.layer = 0;
            surface.depth = 1;
            return true;
        };
        if (!select_pitch_layer(src) || !select_pitch_layer(dst)) {
            AssertFailSoftImpl();
            return; // Reject without writing a partial destination layer.
        }
        sw_blitter->Blit(src, dst, config);
        return;
    }
    if (!rasterizer->AccelerateSurfaceCopy(src, regs.dst, config)) {
        // A block-linear volume copy names one depth slice by the surface
        // address. The software blitter still assumes a single decoded slice
        // and must never receive a depth>1 fallback: TextureCache either
        // resolves the cached 3D slices or the command is rejected unchanged.
        if (src.depth != 1 || regs.dst.depth != 1) {
            UNIMPLEMENTED_MSG("Unresolved Fermi2D volume-slice copy");
            return;
        }
        sw_blitter->Blit(src, regs.dst, config);
    }
}

} // namespace Tegra::Engines
