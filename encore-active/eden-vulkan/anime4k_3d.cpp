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
    size_t image_count_, VkExtent2D output_extent_)
    : device{device_}, memory_allocator{memory_allocator_}, scheduler{scheduler_},
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
    // convolution weights. No shader compiler or NVIDIA runtime on PS5.
    const std::array<std::span<const u32>, 4> code{
        ANIME4K_3D_US_STAGE_0_COMP_SPV, ANIME4K_3D_US_STAGE_1_COMP_SPV,
        ANIME4K_3D_US_STAGE_2_COMP_SPV, ANIME4K_3D_US_STAGE_3_COMP_SPV,
    };
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
    for (size_t i = 0; i < code.size(); ++i) {
        shader_modules[i] = CreateWrappedShaderModule(device, code[i]);
        pipelines[i] = CreateWrappedComputePipeline(
            device, pipeline_layouts[i == 3 ? 1 : 0], *shader_modules[i]);
    }
    // 5 combined samplers + 4 storage images per frame, four sets each.
    descriptor_pool = CreateWrappedDescriptorPool(
        device, image_count * 5, image_count * 4,
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
    // Two 16-bit RGBA convolution surfaces at input size, one x2 output.
    // 2*8 + 4*8 bytes/pixel = 48 bytes per input pixel/per frame.
    const u64 required = pixels * 48u * image_count;
    if (required > kMaxCnnTransientBytes) return false;

    try {
        frames.resize(image_count);
        const std::array<VkDescriptorSetLayout, 4> layouts{
            *descriptor_layouts[0], *descriptor_layouts[0],
            *descriptor_layouts[0], *descriptor_layouts[1]};
        for (auto& frame : frames) {
            for (u32 surface = 0; surface < 3; ++surface) {
                const VkExtent2D dimensions = surface == 2 ? output_extent : extent;
                frame.images[surface] = CreateWrappedImage(
                    memory_allocator, dimensions, kCnnFormat);
                frame.views[surface] = CreateWrappedImageView(
                    device, frame.images[surface], kCnnFormat);
            }
            frame.sets = CreateWrappedDescriptorSets(descriptor_pool, layouts);
            std::array<VkImage, 3> images{
                *frame.images[0], *frame.images[1], *frame.images[2]};
            scheduler.RequestOutsideRenderPassOperationContext();
            scheduler.Record([images](vk::CommandBuffer command) {
                for (const auto image : images) {
                    TransitionImageLayout(
                        command, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_UNDEFINED);
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
    const std::array<VkImageView, 4> input_views{
        source_view, *frame.views[0], *frame.views[1], source_view};
    const std::array<VkImageView, 4> destination_views{
        *frame.views[0], *frame.views[1], *frame.views[0], *frame.views[2]};
    const std::array<VkImage, 4> destination_images{
        *frame.images[0], *frame.images[1], *frame.images[0], *frame.images[2]};

    // One descriptor set per stage AND frame-in-flight, synchronized by
    // Layer::resource_ticks. Descriptor writes happen outside GPU callback.
    std::array<VkDescriptorImageInfo, 9> infos{};
    std::array<VkWriteDescriptorSet, 9> writes{};
    u32 cursor = 0;
    for (u32 stage = 0; stage != 4; ++stage) {
        const u32 binding_count = stage == 3 ? 3 : 2;
        for (u32 binding = 0; binding < binding_count; ++binding) {
            auto& info = infos[cursor];
            const bool storage = binding == binding_count - 1;
            info.sampler = storage ? VK_NULL_HANDLE : *sampler;
            info.imageView = storage ? destination_views[stage] :
                (stage == 3 && binding == 1 ? *frame.views[0] : input_views[stage]);
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
    }
    device.GetLogical().UpdateDescriptorSets(std::span(writes.data(), cursor), {});

    const std::array<VkDescriptorSet, 4> sets{
        frame.sets[0], frame.sets[1], frame.sets[2], frame.sets[3]};
    const std::array<VkPipeline, 4> native_pipelines{
        *pipelines[0], *pipelines[1], *pipelines[2], *pipelines[3]};
    const std::array<VkPipelineLayout, 4> native_layouts{
        *pipeline_layouts[0], *pipeline_layouts[0],
        *pipeline_layouts[0], *pipeline_layouts[1]};
    const VkExtent2D output = output_extent;

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([source_image, destination_images, native_pipelines,
                      native_layouts, sets, extent, output](vk::CommandBuffer command) {
        TransitionImageLayout(command, source_image, VK_IMAGE_LAYOUT_GENERAL);
        for (u32 stage = 0; stage != 4; ++stage) {
            command.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, native_pipelines[stage]);
            command.BindDescriptorSets(
                VK_PIPELINE_BIND_POINT_COMPUTE, native_layouts[stage], 0, sets[stage], {});
            const VkExtent2D dimensions = stage == 3 ? output : extent;
            command.Dispatch(Groups(dimensions.width), Groups(dimensions.height), 1);
            command.PipelineBarrier(
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, kBetweenConvolutions);
        }
        // The present pass reads this output using a sampled descriptor.
        command.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, kBetweenConvolutions);
    });
    return *frame.views[2];
}
} // namespace Vulkan
