// SPDX-License-Identifier: MIT
// Anime4K 3D CNN v4 US, (c) 2019-2021 bloc97.
// Native Vulkan compute adaptation, (c) 2026 Eden Encore.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include "video_core/renderer_vulkan/present/anime4k3d.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/anime4k_3d_us_stage_0_comp_spv.h"
#include "video_core/host_shaders/anime4k_3d_us_stage_1_comp_spv.h"
#include "video_core/host_shaders/anime4k_3d_us_stage_2_comp_spv.h"
#include "video_core/host_shaders/anime4k_3d_us_stage_3_comp_spv.h"
#include "video_core/host_shaders/anime4k_soft_stage_0_comp_spv.h"
#include "video_core/host_shaders/anime4k_soft_stage_1_comp_spv.h"
#include "video_core/host_shaders/anime4k_soft_stage_2_comp_spv.h"
#include "video_core/host_shaders/anime4k_soft_stage_3_comp_spv.h"
#include "video_core/host_shaders/anime4k_denoise_stage_0_comp_spv.h"
#include "video_core/host_shaders/anime4k_denoise_stage_1_comp_spv.h"
#include "video_core/host_shaders/anime4k_denoise_stage_2_comp_spv.h"
#include "video_core/host_shaders/anime4k_denoise_stage_3_comp_spv.h"
#include "video_core/host_shaders/anime4k_denoise_stage_4_comp_spv.h"

namespace Vulkan {
namespace {
constexpr VkFormat kCnnFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr u64 kMaxCnnTransientBytes = 384ull << 20;
constexpr u32 kWorkgroupSize = 8;
[[nodiscard]] bool FullCrop(const Common::Rectangle<f32>& crop) noexcept {
    constexpr f32 eps = 0.0001f;
    return std::abs(crop.left) <= eps && std::abs(crop.top) <= eps &&
           std::abs(crop.right - 1.f) <= eps &&
           std::abs(crop.bottom - 1.f) <= eps;
}
[[nodiscard]] constexpr u32 Groups(u32 pixels) noexcept {
    return (pixels + kWorkgroupSize - 1u) / kWorkgroupSize;
}
constexpr VkMemoryBarrier kBetweenConvolutions{
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
};
} // namespace

Anime4K3D::Anime4K3D(
    const Device& device_, MemoryAllocator& memory_allocator_, Scheduler& scheduler_,
    size_t image_count_, VkExtent2D output_extent_, Profile profile_)
    : profile{profile_}, device{device_}, memory_allocator{memory_allocator_}, scheduler{scheduler_},
      image_count{image_count_}, output_extent{output_extent_} {
    constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    disabled = image_count == 0 || image_count > 8 ||
               output_extent.width == 0 || output_extent.height == 0 ||
               !device.IsFormatSupported(
                   kCnnFormat, needed | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
                   FormatType::Optimal);
    if (disabled) return;

    // All kernels were compiled on the build host from the exact MIT
    // convolution weights. No external proprietary model runtime on PS5.
    // Stage graphs are pinned to the actual upstream CNN weights. These
    // paths are NOT presets for a single shader. The restorative variant
    // must preserve the original source through its final residual pass.
    std::array<std::span<const u32>, 8> code{};
    switch (profile) {
    case Profile::Details:
        stage_count = 4;
        stages = {{
            {-1,-1,0}, {0,-1,1}, {1,-1,0}, {-1,0,3},
        }};
        code[0] = ANIME4K_3D_US_STAGE_0_COMP_SPV;
        code[1] = ANIME4K_3D_US_STAGE_1_COMP_SPV;
        code[2] = ANIME4K_3D_US_STAGE_2_COMP_SPV;
        code[3] = ANIME4K_3D_US_STAGE_3_COMP_SPV;
        break;
    case Profile::Soft:
        stage_count = 8;
        stages = {{
            {-1,-1,0}, {0,-1,1}, {1,-1,0}, {-1,0,2},
            {2,-1,0}, {0,-1,1}, {1,-1,0}, {2,0,3},
        }};
        code[0] = ANIME4K_SOFT_STAGE_0_COMP_SPV;
        code[1] = ANIME4K_SOFT_STAGE_1_COMP_SPV;
        code[2] = ANIME4K_SOFT_STAGE_2_COMP_SPV;
        code[3] = ANIME4K_SOFT_STAGE_3_COMP_SPV;
        code[4] = ANIME4K_3D_US_STAGE_0_COMP_SPV;
        code[5] = ANIME4K_3D_US_STAGE_1_COMP_SPV;
        code[6] = ANIME4K_3D_US_STAGE_2_COMP_SPV;
        code[7] = ANIME4K_3D_US_STAGE_3_COMP_SPV;
        break;
    case Profile::Denoise:
        stage_count = 5;
        stages = {{
            {-1,-1,0}, {0,-1,1}, {1,-1,0}, {0,-1,1},
            {-1,1,3},
        }};
        code[0] = ANIME4K_DENOISE_STAGE_0_COMP_SPV;
        code[1] = ANIME4K_DENOISE_STAGE_1_COMP_SPV;
        code[2] = ANIME4K_DENOISE_STAGE_2_COMP_SPV;
        code[3] = ANIME4K_DENOISE_STAGE_3_COMP_SPV;
        code[4] = ANIME4K_DENOISE_STAGE_4_COMP_SPV;
        break;
    }
    sampler = CreateBilinearSampler(device);
    descriptor_layouts[0] = CreateWrappedDescriptorSetLayout(
        device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, VK_SHADER_STAGE_COMPUTE_BIT);
    descriptor_layouts[1] = CreateWrappedDescriptorSetLayout(
        device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, VK_SHADER_STAGE_COMPUTE_BIT);
    pipeline_layouts[0] = CreateWrappedPipelineLayout(device, descriptor_layouts[0]);
    pipeline_layouts[1] = CreateWrappedPipelineLayout(device, descriptor_layouts[1]);
    u32 sampler_count = 0;
    for (u32 i = 0; i < stage_count; ++i) {
        sampler_count += stages[i].features < 0 ? 1u : 2u;
        shader_modules[i] = CreateWrappedShaderModule(device, code[i]);
        pipelines[i] = CreateWrappedComputePipeline(
            device, pipeline_layouts[stages[i].features < 0 ? 0 : 1],
            *shader_modules[i]);
    }
    descriptor_pool = CreateWrappedDescriptorPool(
        device, image_count * sampler_count, image_count * stage_count,
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE});
}

Anime4K3D::~Anime4K3D() {
    // Layer can be removed while the last accelerated frame is still
    // in flight. Its image views/descriptors must outlive the last GPU read.
    if (!frames.empty()) {
        scheduler.Finish();
    }
}

bool Anime4K3D::InitializeFrames(VkExtent2D extent) {
    if (disabled || extent.width == 0 || extent.height == 0 ||
        extent.width > 4096 || extent.height > 2160 ||
        extent.width * 2u != output_extent.width ||
        extent.height * 2u != output_extent.height) return false;
    if (input_extent.width != 0) {
        // Layer will be rebuilt by its owner on an output resize.
        // On guest-only dimension changes, never reuse stale descriptors.
        return extent.width == input_extent.width &&
               extent.height == input_extent.height;
    }
    const u64 pixels = static_cast<u64>(extent.width) * extent.height;
    // Details/Denoise need two lowres feature images + a 2x output:
    // 48 bytes per source pixel. Soft needs an extra restored original:
    // 56 bytes per pixel. All frames-in-flight share a strict 384MiB cap.
    const u32 scratch_count = profile == Profile::Soft ? 3u : 2u;
    const u64 required = pixels * (u64(scratch_count) * 8u + 32u) * image_count;
    if (required > kMaxCnnTransientBytes) return false;

    try {
        frames.resize(image_count);
        std::array<VkDescriptorSetLayout, 8> layouts{};
        for (u32 i = 0; i < stage_count; ++i) {
            layouts[i] = *descriptor_layouts[stages[i].features < 0 ? 0 : 1];
        }
        for (auto& frame : frames) {
            std::array<VkImage, 4> images{};
            for (u32 surface = 0; surface <= scratch_count; ++surface) {
                const u32 slot = surface == scratch_count ? 3 : surface;
                const VkExtent2D dimensions = slot == 3 ? output_extent : extent;
                frame.images[slot] = CreateWrappedImage(
                    memory_allocator, dimensions, kCnnFormat);
                frame.views[slot] = CreateWrappedImageView(
                    device, frame.images[slot], kCnnFormat);
                images[slot] = *frame.images[slot];
            }
            frame.sets = CreateWrappedDescriptorSets(
                descriptor_pool, std::span(layouts.data(), stage_count));
            scheduler.RequestOutsideRenderPassOperationContext();
            scheduler.Record([images](vk::CommandBuffer command) {
                for (const auto image : images) {
                    if (image) {
                        TransitionImageLayout(
                            command, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_UNDEFINED);
                    }
                }
            });
        }
    } catch (...) {
        // Preserve normal display if the driver rejects an allocation.
        // Retire any transitions before releasing partially allocated images.
        scheduler.Finish();
        frames.clear();
        disabled = true;
        return false;
    }
    input_extent = extent;
    return true;
}

std::optional<VkImageView> Anime4K3D::Draw(
    size_t image_index, VkImage source_image, VkImageView source_view,
    VkExtent2D extent, const Common::Rectangle<f32>& crop) {
    if (disabled || image_index >= image_count || !source_image || !source_view ||
        !FullCrop(crop) || !InitializeFrames(extent)) return std::nullopt;

    auto& frame = frames[image_index];
    // Maximum 8 stages: each has one storage image and either one or
    // two sampled inputs. Every write stays tied to its own frame slot.
    std::array<VkDescriptorImageInfo, 24> infos{};
    std::array<VkWriteDescriptorSet, 24> writes{};
    std::array<VkDescriptorSet, 8> sets{};
    std::array<VkPipeline, 8> native_pipelines{};
    std::array<VkPipelineLayout, 8> native_layouts{};
    u32 cursor = 0;
    for (u32 stage = 0; stage < stage_count; ++stage) {
        const StageIO& plan = stages[stage];
        const u32 binding_count = plan.features < 0 ? 2 : 3;
        const auto view_for = [&](int slot) -> VkImageView {
            return slot < 0 ? source_view : *frame.views[static_cast<u32>(slot)];
        };
        for (u32 binding = 0; binding < binding_count; ++binding) {
            const bool storage = binding == binding_count - 1;
            const int image_slot = storage ? plan.destination :
                                   binding == 1 ? plan.features : plan.source;
            auto& info = infos[cursor];
            info.sampler = storage ? VK_NULL_HANDLE : *sampler;
            info.imageView = view_for(image_slot);
            info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            writes[cursor] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = frame.sets[stage],
                .dstBinding = binding,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE :
                                            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &info,
            };
            ++cursor;
        }
        sets[stage] = frame.sets[stage];
        native_pipelines[stage] = *pipelines[stage];
        native_layouts[stage] = *pipeline_layouts[plan.features < 0 ? 0 : 1];
    }
    device.GetLogical().UpdateDescriptorSets(
        std::span(writes.data(), cursor), {});

    const VkExtent2D output = output_extent;
    const u32 passes = stage_count;
    const auto io = stages;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([source_image, native_pipelines, native_layouts, sets,
                      io, passes, extent, output](vk::CommandBuffer command) {
        TransitionImageLayout(command, source_image, VK_IMAGE_LAYOUT_GENERAL);
        for (u32 stage = 0; stage < passes; ++stage) {
            command.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, native_pipelines[stage]);
            command.BindDescriptorSets(
                VK_PIPELINE_BIND_POINT_COMPUTE, native_layouts[stage], 0, sets[stage], {});
            const VkExtent2D dimensions = io[stage].destination == 3 ? output : extent;
            command.Dispatch(Groups(dimensions.width), Groups(dimensions.height), 1);
            command.PipelineBarrier(
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, kBetweenConvolutions);
        }
        command.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, kBetweenConvolutions);
    });
    return *frame.views[3];
}
std::optional<Anime4K3D::OutputFrame> Anime4K3D::DrawFrame(
    size_t image_index, VkImage source_image, VkImageView source_view,
    VkExtent2D extent, const Common::Rectangle<f32>& crop) {
    // Preserve the existing real neural x2 graph/guards. Draw() records
    // all native compute stages and refuses incompatible source extents,
    // crops, image slots and GPU budgets before returning a view.
    const auto view=Draw(image_index,source_image,source_view,extent,crop);
    if(!view || image_index>=frames.size())
        return std::nullopt;
    const auto& owner=frames[image_index];
    if(!owner.images[3] || !owner.views[3] ||
       *owner.views[3]!=*view || !*owner.images[3] ||
       !output_extent.width || !output_extent.height ||
       extent.width*2u!=output_extent.width ||
       extent.height*2u!=output_extent.height)
        return std::nullopt;
    // VkImage and VkImageView are borrowed, not transferred. The
    // caller MUST keep this Anime4K3D instance and frame slot live until
    // the dependent spatial filter's scheduler tick has retired.
    return OutputFrame{*owner.images[3],*view,output_extent};
}

} // namespace Vulkan
