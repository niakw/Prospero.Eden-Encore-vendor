// SPDX-License-Identifier: GPL-3.0-or-later
// R24 — restricted forced-MSAA shadow framebuffer, native Vulkan work.
// This does not provide a menu option or change guest texture ImageInfo.
// Must be paired with a matching multisample graphics pipeline at draw time.
#include <array>
#include "video_core/renderer_vulkan/msaa_color_shadow_pass.h"
#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {

MsaaColorClearShadowPass::MsaaColorClearShadowPass(
    const Device& device_, Scheduler& scheduler_, MemoryAllocator& allocator_,
    RenderPassCache& render_pass_cache_)
    : device(device_), scheduler(scheduler_), allocator(allocator_),
      render_pass_cache(render_pass_cache_) {}

MsaaColorClearShadowPass::~MsaaColorClearShadowPass() {
    // VkFramebuffer/View/Image are destroyed after the body below.
    scheduler.Finish();
}

void MsaaColorClearShadowPass::Reset() {
    scheduler.Finish();
    framebuffer = {};
    msaa_view = {};
    msaa_image = {};
    last_guest_image = VK_NULL_HANDLE;
    last_guest_view = VK_NULL_HANDLE;
    render_pass = VK_NULL_HANDLE;
    last_extent = {};
    last_format = VK_FORMAT_UNDEFINED;
    last_samples = VK_SAMPLE_COUNT_1_BIT;
}

std::optional<MsaaColorClearShadowPass::Prepared>
MsaaColorClearShadowPass::TryPrepare(
    const Target& target, Eden::Graphics::MsaaForce::Requested requested,
    const Eden::Graphics::MsaaForce::GuestPass& guest_pass,
    const Eden::Graphics::MsaaForce::Limits& verified_limits) {
    using namespace Eden::Graphics::MsaaForce;
    if (requested==Requested::Auto) {
        last_decision=Decision::NativeAuto;
        return std::nullopt;
    }
    if (!target.clear_entire_target || guest_pass.uses_depth ||
        guest_pass.render_target_count != 1 || target.layers != 1 ||
        guest_pass.multisample_guest_reads ||
        guest_pass.storage_or_alias_attachments) {
        last_decision=Decision::UnsupportedMultisampleRead;
        return std::nullopt;
    }
    if (!target.guest_image || !target.guest_view ||
        target.vk_format==VK_FORMAT_UNDEFINED ||
        target.guest_format==VideoCore::Surface::PixelFormat::Invalid ||
        target.extent.width != guest_pass.width ||
        target.extent.height != guest_pass.height) {
        last_decision=Decision::UnsupportedFrameFormat;
        return std::nullopt;
    }
    const VkSampleCountFlagBits samples{
        static_cast<VkSampleCountFlagBits>(Bit(requested))};
    constexpr VkImageUsageFlags shadow_usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    VkImageFormatProperties format_limits{};
    // The global framebuffer sample mask is not a per-format/usage guarantee.
    // Vulkan must explicitly admit the same transient image we will allocate.
    const bool exact_format_supported = device.GetPhysical().GetImageFormatProperties(
        target.vk_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        shadow_usage, 0, format_limits);
    const VkSampleCountFlags supported{
        device.GetPhysical().GetProperties().limits.framebufferColorSampleCounts};
    if (!exact_format_supported || !(format_limits.sampleCounts & samples) ||
        target.extent.width > format_limits.maxExtent.width ||
        target.extent.height > format_limits.maxExtent.height ||
        format_limits.maxArrayLayers < 1 ||
        !(supported & samples) ||
        !device.IsFormatSupported(target.vk_format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                                  FormatType::Optimal) ||
        MaxwellToVK::SurfaceFormat(device,FormatType::Optimal,true,target.guest_format).format !=
            target.vk_format) {
        last_decision=Decision::UnsupportedFrameFormat;
        return std::nullopt;
    }
    // This class supplies only colour shadow + colour resolve and preserves
    // native guest target memory/layout. No depth or guest image aliasing.
    const Implementation available{
        .native_shadow_color=true,
        .native_shadow_depth=false,
        .resolve_color_to_guest=true,
        .resolve_depth_to_guest=false,
        .guest_layout_preserved=true,
        .sampled_guest_reads_coherent=true,
    };
    const Eden::Graphics::MsaaForce::Result candidate=Evaluate(requested,guest_pass,verified_limits,available);
    last_decision=candidate.decision;
    if (candidate.decision!=Decision::CandidateOnly)
        return std::nullopt;

    const bool recreate=!msaa_image || last_guest_image!=target.guest_image ||
        last_guest_view!=target.guest_view ||
        last_extent.width!=target.extent.width ||
        last_extent.height!=target.extent.height ||
        last_format!=target.vk_format || last_samples!=samples;
    if (recreate) {
        Reset(); // finish queued GPU use before replacing owned framebuffers
        try {
            msaa_image=allocator.CreateImage(VkImageCreateInfo{
                .sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext=nullptr,
                .flags=0,
                .imageType=VK_IMAGE_TYPE_2D,
                .format=target.vk_format,
                .extent={target.extent.width,target.extent.height,1},
                .mipLevels=1,
                .arrayLayers=1,
                .samples=samples,
                .tiling=VK_IMAGE_TILING_OPTIMAL,
                .usage=shadow_usage,
                .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount=0,
                .pQueueFamilyIndices=nullptr,
                .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,
            });
            msaa_view=device.GetLogical().CreateImageView(VkImageViewCreateInfo{
                .sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext=nullptr,
                .flags=0,
                .image=*msaa_image,
                .viewType=VK_IMAGE_VIEW_TYPE_2D,
                .format=target.vk_format,
                .components={},
                .subresourceRange={
                    .aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel=0,.levelCount=1,
                    .baseArrayLayer=0,.layerCount=1,
                },
            });
            RenderPassKey key{};
            key.color_formats.fill(VideoCore::Surface::PixelFormat::Invalid);
            key.color_formats[0]=target.guest_format;
            key.depth_format=VideoCore::Surface::PixelFormat::Invalid;
            key.samples=samples;
            key.resolve_color=true;
            key.resolve_depth_stencil=false;
            key.color_clear_mask=1;
            key.color_discard_mask=1; // MSAA shadow is transient; only 1x resolve is stored.
            render_pass=render_pass_cache.Get(key);
            const std::array<VkImageView,2> attachments{
                *msaa_view,target.guest_view};
            framebuffer=device.GetLogical().CreateFramebuffer(VkFramebufferCreateInfo{
                .sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext=nullptr,
                .flags=0,
                .renderPass=render_pass,
                .attachmentCount=static_cast<u32>(attachments.size()),
                .pAttachments=attachments.data(),
                .width=target.extent.width,.height=target.extent.height,
                .layers=1,
            });
        } catch (const vk::Exception&) {
            Reset();
            last_decision=Decision::UnsupportedFrameFormat;
            return std::nullopt;
        }
        last_guest_image=target.guest_image;
        last_guest_view=target.guest_view;
        last_extent=target.extent;
        last_format=target.vk_format;
        last_samples=samples;
    }
    // The caller has proved a full clear, so old shadow data is discarded.
    // The actual render pass uses LOAD_OP_CLEAR for the multisample image and
    // an implicit native Vulkan colour resolve into the original guest view.
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([image=*msaa_image](vk::CommandBuffer cmd) {
        const VkImageMemoryBarrier barrier{
            .sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext=nullptr,
            .srcAccessMask=0,
            .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout=VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=image,
            .subresourceRange={
                .aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel=0,.levelCount=1,
                .baseArrayLayer=0,.layerCount=1,
            },
        };
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            0,{}, {}, barrier);
    });
    return Prepared{render_pass,*framebuffer,target.extent,samples};
}
} // namespace Vulkan
