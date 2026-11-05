// SPDX-License-Identifier: GPL-3.0-or-later
// Maintained native PS5 Vulkan FSR 3.1 scheduler adapter.
// There is deliberately NO automatic call from PresentManager here: the
// colour-only screen image does not contain real guest motion/depth/jitter.
#include <cmath>
#include "video_core/renderer_vulkan/fsr31_native_bridge.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
namespace Eden::Graphics {

Fsr31NativeBridge::Fsr31NativeBridge(
    const Vulkan::Device& device_, Vulkan::Scheduler& scheduler_,
    Vulkan::MemoryAllocator& allocator_, Vulkan::StagingBufferPool& staging_,
    Vulkan::DescriptorPool& descriptors_, Vulkan::ComputePassDescriptorQueue& queue_)
    : device(device_), scheduler(scheduler_), memory_allocator(allocator_),
      staging_pool(staging_), descriptor_pool(descriptors_), descriptor_queue(queue_) {}

Fsr31NativeBridge::~Fsr31NativeBridge() {
    // The upscaler holds ping-pong images referenced by recorded GPU passes.
    scheduler.Finish();
    upscaler.reset();
}

void Fsr31NativeBridge::ResetHistory() noexcept {
    continuity.Reset();
    needs_reset = true;
}

std::optional<Fsr31NativeOutput> Fsr31NativeBridge::TryRecord(
    const Fsr31NativeFrame& frame) {
    last_rejection = continuity.Observe(frame.metadata);
    if (last_rejection != Temporal::Rejection::None) {
        needs_reset = true;
        return std::nullopt;
    }
    if (frame.color==VK_NULL_HANDLE || frame.depth==VK_NULL_HANDLE ||
        frame.motion==VK_NULL_HANDLE) {
        last_rejection = Temporal::Rejection::MissingResource;
        ResetHistory();
        return std::nullopt;
    }
    if (!std::isfinite(frame.sharpness) ||
        frame.sharpness < 0.0f || frame.sharpness > 1.0f) {
        last_rejection = Temporal::Rejection::InvalidSharpening;
        ResetHistory();
        return std::nullopt;
    }
    if (!Vulkan::Fsr3Upscaler::IsSupported(device)) {
        last_rejection = Temporal::Rejection::UnsupportedDevice;
        ResetHistory();
        return std::nullopt;
    }
    if (!upscaler) {
        upscaler = std::make_unique<Vulkan::Fsr3Upscaler>(
            device,scheduler,memory_allocator,staging_pool,descriptor_pool,
            descriptor_queue);
        needs_reset = true;
    }
    const auto& meta = frame.metadata;
    Vulkan::Fsr3Upscaler::Inputs native{
        .color=frame.color, .depth=frame.depth, .motion_vectors=frame.motion,
        .render_size={meta.render_width,meta.render_height},
        .upscale_size={meta.output_width,meta.output_height},
        .jitter=meta.jitter,
        .motion_vector_scale=meta.motion_scale,
        .device_to_view_depth=meta.depth_to_view,
        .tan_half_fov=meta.tan_half_fov,
        .frame_time_ms=meta.elapsed_ms,
        .reset=needs_reset,
        .sharpen=frame.sharpen,
        .sharpness=frame.sharpness,
    };
    // Caller owns guest images through GPU completion and must composite
    // OutputImage/View under correct VK_IMAGE_LAYOUT_GENERAL barriers.
    upscaler->Upscale(native);
    needs_reset=false;
    return Fsr31NativeOutput{upscaler->OutputImage(),upscaler->OutputView()};
}

} // namespace Eden::Graphics
