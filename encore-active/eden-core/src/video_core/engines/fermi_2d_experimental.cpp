#include <atomic>

namespace {
unsigned EdenGpuBoundedSample(std::atomic<unsigned>& samples) noexcept {
    unsigned current = samples.load(std::memory_order_relaxed);
    while (current < 8u) {
        if (samples.compare_exchange_weak(current, current + 1u,
                                          std::memory_order_relaxed))
            return current;
    }
    return 8u;
}
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

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
    ASSERT_MSG(method < Regs::NUM_REGS, "Invalid Fermi2D register, increase the size of the Regs structure");
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
        regs.reg_array[method] = value;
    }
    method_sink.clear();
}

void Fermi2D::Blit() {
    LOG_DEBUG(HW_GPU, "called. source address={:#x}, destination address={:#x}", regs.src.Address(), regs.dst.Address());

    UNIMPLEMENTED_IF_MSG(regs.operation != Operation::SrcCopy, "Operation is not copy");
    // For pitch-linear images, one layer starts exactly pitch*height
    // bytes after the preceding one. Keep nonzero layers of block-linear
    // 3D images unsupported until origin_z has a verified implementation.
    const bool pitch_layer_copy =
        (regs.src.layer != 0 || regs.dst.layer != 0) &&
        regs.src.linear == MemoryLayout::Pitch &&
        regs.dst.linear == MemoryLayout::Pitch &&
        regs.src.layer < regs.src.depth && regs.dst.layer < regs.dst.depth &&
        regs.src.pitch != 0 && regs.dst.pitch != 0 &&
        regs.src.width != 0 && regs.src.height != 0 &&
        regs.dst.width != 0 && regs.dst.height != 0 &&
        regs.src.format == regs.dst.format &&
        static_cast<u64>(regs.src.pitch) >=
            static_cast<u64>(regs.src.width) *
                BytesPerBlock(PixelFormatFromRenderTargetFormat(regs.src.format)) &&
        static_cast<u64>(regs.dst.pitch) >=
            static_cast<u64>(regs.dst.width) *
                BytesPerBlock(PixelFormatFromRenderTargetFormat(regs.dst.format)) &&
        regs.operation == Operation::SrcCopy && regs.clip_enable == 0;
    if ((regs.src.layer != 0 || regs.dst.layer != 0) && !pitch_layer_copy) {
        static std::atomic<unsigned> unsupported_layer_reports{0};
        const unsigned count = EdenGpuBoundedSample(unsupported_layer_reports);
        if (count < 8)
            LOG_CRITICAL(Debug,
                "EDEN_GPU_FERMI2D_UNSUPPORTED_LAYER src_layer={} dst_layer={} "
                "src_depth={} dst_depth={} src_linear={} dst_linear={} sample={}",
                regs.src.layer, regs.dst.layer, regs.src.depth, regs.dst.depth,
                static_cast<unsigned>(regs.src.linear), static_cast<unsigned>(regs.dst.linear),
                count + 1);
        AssertFailSoftImpl();
        return; // unsupported layers must not silently copy the wrong slice
    }
    // The software swizzler already handles the z=0 subrectangle when
    // EITHER the source OR destination has 3D depth, provided both
    // layers are zero. Keep nonzero layers explicitly unsupported:
    // the current UnswizzleSubrect API hardcodes origin_z = 0.
    const bool base_layer_3d_copy =
        (regs.src.depth > 1 || regs.dst.depth > 1) &&
        regs.src.depth >= 1 && regs.dst.depth >= 1 &&
        regs.src.layer == 0 && regs.dst.layer == 0 &&
        regs.operation == Operation::SrcCopy && regs.clip_enable == 0;
    if (regs.src.depth != 1 && !base_layer_3d_copy && !pitch_layer_copy) {
        static std::atomic<unsigned> depth_reports{0};
        const unsigned count = EdenGpuBoundedSample(depth_reports);
        // Previously this logged the same generic warning for every
        // Fermi2D blit. Do not normalize depth to one: the texture-cache
        // source currently cannot represent arbitrary 3D slices.
        if (count < 8) {
            LOG_CRITICAL(Debug,
                "EDEN_GPU_FERMI2D_UNSUPPORTED src_depth={} dst_depth={} src_layer={} dst_layer={} "
                "src_block_depth={} dst_block_depth={} src_addr={:#x} dst_addr={:#x} sample={}",
                regs.src.depth, regs.dst.depth, regs.src.layer, regs.dst.layer,
                regs.src.block_depth, regs.dst.block_depth, regs.src.Address(),
                regs.dst.Address(), count + 1);
        }
        // Preserve UNIMPLEMENTED_IF_MSG soft-assert / debug-break semantics
        // even after the diagnostic log's bounded eight samples.
        AssertFailSoftImpl();
        return; // unsupported depth is not a valid copy operation
    }
    UNIMPLEMENTED_IF_MSG(regs.clip_enable != 0, "Clipped blit enabled");

    const auto& args = regs.pixels_from_memory;
    constexpr s64 null_derivative = 1ULL << 32;
    Surface src = regs.src;
    const auto bytes_per_pixel = BytesPerBlock(PixelFormatFromRenderTargetFormat(src.format));
    const bool delegate_to_gpu = src.width > 512 && src.height > 512 && bytes_per_pixel <= 8 &&
                                 src.format != regs.dst.format;

    auto srcX = args.src_x0;
    auto srcY = args.src_y0;
    if (args.sample_mode.origin == Origin::Corner) {
        srcX -= (args.du_dx >> 33) << 32;
        srcY -= (args.dv_dy >> 33) << 32;
    }

    Config config{
        .operation = regs.operation,
        .filter = args.sample_mode.filter,
        .must_accelerate =
            args.du_dx != null_derivative || args.dv_dy != null_derivative || delegate_to_gpu,
        .dst_x0 = args.dst_x0,
        .dst_y0 = args.dst_y0,
        .dst_x1 = args.dst_x0 + args.dst_width,
        .dst_y1 = args.dst_y0 + args.dst_height,
        .src_x0 = static_cast<s32>(srcX >> 32),
        .src_y0 = static_cast<s32>(srcY >> 32),
        .src_x1 = static_cast<s32>((srcX + args.du_dx * args.dst_width) >> 32),
        .src_y1 = static_cast<s32>((srcY + args.dv_dy * args.dst_height) >> 32),
    };

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
    if (base_layer_3d_copy || pitch_layer_copy) {
        // These new software-only paths must not turn negative or
        // out-of-bounds subrectangles into oversized unsigned copies.
        // The existing general Fermi copy path is unchanged.
        const u64 src_bpp = BytesPerBlock(
            PixelFormatFromRenderTargetFormat(src.format));
        const u64 dst_bpp = BytesPerBlock(
            PixelFormatFromRenderTargetFormat(regs.dst.format));
        // The pinned blitter evaluates pitch*height and x_extent*y_extent
        // in u32 before promotion to size_t. Never feed it operands that
        // overflow those intermediate products, nor zero-byte formats.
        const auto safe_plane = [](const Surface& surface) {
            return surface.linear != MemoryLayout::Pitch ||
                (surface.pitch != 0 && surface.height != 0 &&
                 static_cast<u64>(surface.pitch) * surface.height <= 0xffffffffULL);
        };
        const bool copy_storage_valid =
            src_bpp != 0 && dst_bpp != 0 &&
            safe_plane(src) && safe_plane(regs.dst);
        const bool rect_valid =
            config.src_x0 >= 0 && config.src_y0 >= 0 &&
            config.dst_x0 >= 0 && config.dst_y0 >= 0 &&
            config.src_x1 > config.src_x0 && config.src_y1 > config.src_y0 &&
            config.dst_x1 > config.dst_x0 && config.dst_y1 > config.dst_y0 &&
            static_cast<u64>(config.src_x1) <= src.width &&
            static_cast<u64>(config.src_y1) <= src.height &&
            static_cast<u64>(config.dst_x1) <= regs.dst.width &&
            static_cast<u64>(config.dst_y1) <= regs.dst.height;
        // Multiply-free 32-bit allocation boundary: (width * height * bpp)
        // may itself overflow u64 for malicious guest surface dimensions.
        // The pinned sw_blitter allocates exactly this many decoded bytes.
        const auto valid_byte_count = [](s32 x0, s32 x1, s32 y0, s32 y1, u64 bpp) {
            if (x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0 ||
                bpp == 0 || bpp > 0xffffffffULL)
                return false;
            const u64 width = static_cast<u64>(x1) - static_cast<u64>(x0);
            const u64 height = static_cast<u64>(y1) - static_cast<u64>(y0);
            const u64 max = 0xffffffffULL;
            return width <= max / bpp &&
                height <= max / (width * bpp);
        };
        const bool copy_sizes_valid = rect_valid && copy_storage_valid &&
            valid_byte_count(config.src_x0, config.src_x1,
                             config.src_y0, config.src_y1, src_bpp) &&
            valid_byte_count(config.dst_x0, config.dst_x1,
                             config.dst_y0, config.dst_y1, dst_bpp);
        if (!copy_sizes_valid) {
            static std::atomic<unsigned> rect_reports{0};
            const unsigned count = EdenGpuBoundedSample(rect_reports);
            if (count < 8)
                LOG_CRITICAL(Debug,
                    "EDEN_GPU_FERMI2D_SOFTWARE_BOUNDS_INVALID src={}..{},{}..{} "
                    "dst={}..{},{}..{} sample={}",
                    config.src_x0, config.src_x1, config.src_y0, config.src_y1,
                    config.dst_x0, config.dst_x1, config.dst_y0, config.dst_y1,
                    count + 1);
            AssertFailSoftImpl();
            return;
        }
        // 3D z=0 uses the original block-depth swizzle. Nonzero layers
        // are supported ONLY in pitch-linear images, with explicit
        // overflow-checked pitch*height*layer addressing. Both routes
        // use the software decoder and avoid unknown GPU acceleration.
        Surface dst = regs.dst;
        if (pitch_layer_copy) {
            const auto select_pitch_layer = [](Surface& surface) {
                const u64 plane_bytes = static_cast<u64>(surface.pitch) * surface.height;
                const u64 last = ~static_cast<u64>(0);
                const u64 offset = plane_bytes * surface.layer;
                if (plane_bytes == 0 || surface.layer >= surface.depth ||
                    (surface.layer != 0 && offset / surface.layer != plane_bytes) ||
                    offset > last - surface.Address()) {
                    return false;
                }
                const u64 address = surface.Address() + offset;
                surface.addr_upper = static_cast<u32>(address >> 32);
                surface.addr_lower = static_cast<u32>(address);
                surface.layer = 0;
                return true;
            };
            if (!select_pitch_layer(src) || !select_pitch_layer(dst)) {
                LOG_CRITICAL(Debug, "EDEN_GPU_FERMI2D_PITCH_LAYER_INVALID");
                AssertFailSoftImpl();
                return;
            }
        }
        // Temporary descriptors only: guest originals are untouched.
        // depth=1 means one subrectangle is copied; block_depth remains
        // unchanged for the block-linear z=0 path.
        src.depth = 1;
        dst.depth = 1;
        static std::atomic<unsigned> software_reports{0};
        const unsigned report = EdenGpuBoundedSample(software_reports);
        if (report < 8) {
            LOG_INFO(HW_GPU,
                "EDEN_GPU_FERMI2D_SOFTWARE mode={} src_depth={} dst_depth={} "
                "src_layer={} dst_layer={} src_addr={:#x} dst_addr={:#x} sample={}",
                pitch_layer_copy ? "pitch_layer" : "z0", regs.src.depth, regs.dst.depth,
                regs.src.layer, regs.dst.layer, src.Address(), dst.Address(), report + 1);
        }
        sw_blitter->Blit(src, dst, config);
    } else if (!rasterizer->AccelerateSurfaceCopy(src, regs.dst, config)) {
        sw_blitter->Blit(src, regs.dst, config);
    }
}

} // namespace Tegra::Engines
