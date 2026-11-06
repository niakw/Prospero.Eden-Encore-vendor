// SPDX-License-Identifier: GPL-3.0-or-later
// Maintained native PS5 Vulkan FSR 3.1 scheduler adapter.
// There is deliberately NO automatic call from PresentManager here: the
// colour-only screen image does not contain real guest motion/depth/jitter.
#include <algorithm>
#include <cmath>
#include <new>
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <type_traits>
#include "fsr31_vram_budget.h"
#include "fsr31_native_resource_contract.h"
#include "video_core/renderer_vulkan/fsr31_native_bridge.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/blit_image.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_device.h"
namespace Eden::Graphics {
namespace {
template <typename T>
std::uint64_t HandleIdentity(T handle) noexcept {
    if constexpr (std::is_pointer_v<T>) {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}
} // namespace

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
    if (backend_disabled) {
        last_rejection = Temporal::Rejection::NativeBackendFailure;
        ResetHistory();
        return std::nullopt;
    }
    if (frame.color==VK_NULL_HANDLE || frame.depth==VK_NULL_HANDLE ||
        frame.motion==VK_NULL_HANDLE) {
        last_rejection = Temporal::Rejection::MissingResource;
        ResetHistory();
        return std::nullopt;
    }
    // The continuity check validates declared guest provenance. This additional
    // check verifies that the images and views we actually submit to Vulkan
    // are exactly the authenticated guest resources (not unrelated handles).
    last_rejection = Temporal::ValidateNativeResources(
        frame.metadata, Temporal::NativeResources{
            .color={HandleIdentity(frame.color_image),HandleIdentity(frame.color)},
            .depth={HandleIdentity(frame.depth_image),HandleIdentity(frame.depth)},
            .motion={HandleIdentity(frame.motion_image),HandleIdentity(frame.motion)},
        });
    if (last_rejection != Temporal::Rejection::None) {
        ResetHistory();
        return std::nullopt;
    }
    if ((frame.color_format!=VK_FORMAT_R16G16B16A16_SFLOAT &&
         frame.color_format!=VK_FORMAT_B10G11R11_UFLOAT_PACK32) ||
        frame.depth_format!=VK_FORMAT_D32_SFLOAT ||
        frame.motion_format!=VK_FORMAT_R16G16_SFLOAT) {
        last_rejection=Temporal::Rejection::UnsupportedInputFormat;
        ResetHistory();
        return std::nullopt;
    }
    // Shader reads must actually be legal for each input format on this
    // Vulkan device. A matching enum alone does not prove sampled-image support.
    constexpr auto sampled = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (!device.IsFormatSupported(frame.color_format, sampled, Vulkan::FormatType::Optimal) ||
        !device.IsFormatSupported(frame.depth_format, sampled, Vulkan::FormatType::Optimal) ||
        !device.IsFormatSupported(frame.motion_format, sampled, Vulkan::FormatType::Optimal)) {
        last_rejection=Temporal::Rejection::UnsupportedInputFormat;
        ResetHistory();
        return std::nullopt;
    }
    const auto& input=frame.metadata;
    // Preallocation check runs only for a new temporal history or whenever
    // the renderer changes resolution. Avoid querying memory each frame.
    if (!upscaler || input.render_width!=last_render_width ||
        input.render_height!=last_render_height ||
        input.output_width!=last_output_width ||
        input.output_height!=last_output_height) {
        const auto estimate=Fsr31Budget::Evaluate(
            {.render_width=input.render_width,.render_height=input.render_height,
             .output_width=input.output_width,.output_height=input.output_height},
            {.device_local_bytes=device.GetDeviceLocalMemory(),
             .used_bytes=device.CanReportMemoryUsage()?device.GetDeviceMemoryUsage():0,
             .usage_is_known=device.CanReportMemoryUsage()});
        if (estimate.decision!=Fsr31Budget::Decision::Allowed) {
            last_rejection=Temporal::Rejection::InsufficientMemoryBudget;
            ResetHistory();
            return std::nullopt;
        }
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
        try {
            upscaler = std::make_unique<Vulkan::Fsr3Upscaler>(
                device,scheduler,memory_allocator,staging_pool,descriptor_pool,
                descriptor_queue);
            needs_reset = true;
        } catch (const Vulkan::vk::Exception& failure) {
            if (failure.GetResult() == VK_ERROR_DEVICE_LOST) throw;
            backend_disabled = true;
            last_rejection = Temporal::Rejection::NativeBackendFailure;
            ResetHistory();
            return std::nullopt;
        } catch (const std::bad_alloc&) {
            backend_disabled = true;
            last_rejection = Temporal::Rejection::NativeBackendFailure;
            ResetHistory();
            return std::nullopt;
        }
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
    // All work is recorded against the same guest GPU scheduler and
    // must execute outside any existing rasterization pass.
    scheduler.RequestOutsideRenderPassOperationContext();
    // Caller retains ownership of the original VkImageViews and must keep
    // them resident until this scheduler's GPU tick has completed.
    try {
        upscaler->Upscale(native);
    } catch (const Vulkan::vk::Exception& failure) {
        if (failure.GetResult() == VK_ERROR_DEVICE_LOST) throw;
        // Vulkan may already have queued earlier compute passes. Drain them
        // before dropping the history resources they still reference.
        scheduler.Finish();
        upscaler.reset();
        backend_disabled = true;
        last_rejection = Temporal::Rejection::NativeBackendFailure;
        ResetHistory();
        return std::nullopt;
    } catch (const std::bad_alloc&) {
        scheduler.Finish();
        upscaler.reset();
        backend_disabled = true;
        last_rejection = Temporal::Rejection::NativeBackendFailure;
        ResetHistory();
        return std::nullopt;
    }
    last_render_width=input.render_width;
    last_render_height=input.render_height;
    last_output_width=input.output_width;
    last_output_height=input.output_height;
    needs_reset=false;
    return Fsr31NativeOutput{upscaler->OutputImage(),upscaler->OutputView()};
}

bool Fsr31NativeBridge::TryRecordToGuest(
    const Fsr31NativeFrame& frame, const Vulkan::Framebuffer* destination,
    Vulkan::BlitImageHelper& blit) {
    using VideoCore::Surface::PixelFormat;
    if (!destination) {
        last_rejection = Temporal::Rejection::MissingResource;
        ResetHistory();
        return false;
    }
    // The present blit is only defined for the single-colour DLSS-like
    // guest output. Never write into a depth/stencil or multi-RT pass.
    const auto& pass = destination->RenderPassKeyBase();
    const bool color_only =
        pass.color_formats[0] != PixelFormat::Invalid &&
        !VideoCore::Surface::IsPixelFormatInteger(pass.color_formats[0]) &&
        pass.depth_format == PixelFormat::Invalid &&
        std::all_of(pass.color_formats.begin() + 1, pass.color_formats.end(),
                    [](PixelFormat format) { return format == PixelFormat::Invalid; });
    // The destination must be one complete, single-layer guest colour view.
    // Reject layered/mipmapped targets before scheduling any temporal work.
    const VkImageSubresourceRange range = destination->ColorAttachmentRange(0);
    if (!color_only || destination->NumColorBuffers() != 1 ||
        destination->NumImages() != 1 ||
        !destination->ColorAttachmentImage(0) ||
        !destination->ColorAttachmentView(0) ||
        destination->ColorAttachmentImage(0) != destination->Images()[0] ||
        range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        range.baseMipLevel != 0 || range.levelCount != 1 ||
        range.baseArrayLayer != 0 || range.layerCount != 1) {
        last_rejection = Temporal::Rejection::InvalidResolution;
        ResetHistory();
        return false;
    }
    const VkExtent2D extent = destination->RenderArea();
    const auto& destination_images = destination->Images();
    // Conservatively reject aliasing through ANY physical guest attachment,
    // not only the first displayed colour view. The guest texture cache owns
    // layouts, backing storage and synchronization for these images.
    for (std::uint32_t i = 0; i < destination->NumImages(); ++i) {
        const Temporal::Rejection result = Temporal::ValidateNativeDestination(
            frame.metadata,
            Temporal::NativeDestination{
                .image=HandleIdentity(destination_images[i]),
                .width=extent.width, .height=extent.height,
                .samples=static_cast<std::uint32_t>(pass.samples),
                .single_color_no_depth=color_only,
            });
        if (result != Temporal::Rejection::None) {
            last_rejection = result;
            ResetHistory();
            return false;
        }
    }
    if (!destination->NumImages()) {
        last_rejection = Temporal::Rejection::MissingResource;
        ResetHistory();
        return false;
    }
    const auto temporal_output = TryRecord(frame);
    if (!temporal_output) {
        return false;
    }
    // The compute output is in GENERAL after the FSR dispatch. The existing
    // Vulkan blit helper transitions it to SHADER_READ_ONLY_OPTIMAL and writes
    // into the guest target without changing any guest texture metadata.
    const VideoCommon::Region2D full{
        .start = {0, 0},
        .end = {static_cast<s32>(extent.width), static_cast<s32>(extent.height)},
    };
    blit.BlitColor(destination, temporal_output->view, temporal_output->image,
                   upscaler->PointSampler(), full, full,
                   VideoCommon::Extent3D{extent.width, extent.height, 1});
    // BlitImageHelper leaves the source in SHADER_READ_ONLY_OPTIMAL; restore
    // GENERAL before the next FSR compute submission (otherwise history
    // storage writes and layout tracking diverge on the second frame).
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([output = temporal_output->image](Vulkan::vk::CommandBuffer cmdbuf) {
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = output,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0, .levelCount = 1,
                .baseArrayLayer = 0, .layerCount = 1,
            },
        };
        cmdbuf.PipelineBarrier(
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, {}, {}, barrier);
    });
    return true;
}

} // namespace Eden::Graphics
