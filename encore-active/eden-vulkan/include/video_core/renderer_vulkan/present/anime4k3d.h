// SPDX-License-Identifier: MIT
// Anime4K v4 3D x2 US weights and convolution (c) 2019-2021 bloc97.
// Native Vulkan compute integration (c) 2026 Eden Encore.
#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <vector>
#include "common/math_util.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {
class Device;
class Scheduler;

// Truly neural SPATIAL x2 upscaler. Not temporal FSR3, not frame-generation,
// no proprietary NVIDIA DLSS model, no downloaded ROM texture replacement.
// Four pinned Anime4K v4 3D CNN stages in Vulkan compute, source/MIT in vendor.
class Anime4K3D final {
public:
    Anime4K3D(const Device&, MemoryAllocator&, Scheduler&, size_t image_count,
              VkExtent2D output_extent);
    ~Anime4K3D();

    [[nodiscard]] std::optional<VkImageView> Draw(
        size_t image_index, VkImage source_image, VkImageView source_view,
        VkExtent2D input_extent, const Common::Rectangle<f32>& crop);

private:
    struct Frame {
        // Three Vulkan images: conv0/conv2 reuse slot 0; conv1 uses slot 1;
        // final 2x result uses slot 2. No in-place shader sampling.
        std::array<vk::Image, 3> images{};
        std::array<vk::ImageView, 3> views{};
        vk::DescriptorSets sets{};
        bool initialized{};
    };

    [[nodiscard]] bool InitializeFrames(VkExtent2D input_extent);

    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;
    size_t image_count;
    VkExtent2D output_extent{};
    VkExtent2D input_extent{};

    vk::Sampler sampler{};
    vk::DescriptorPool descriptor_pool{};
    std::array<vk::DescriptorSetLayout, 2> descriptor_layouts{};
    std::array<vk::PipelineLayout, 2> pipeline_layouts{};
    std::array<vk::ShaderModule, 4> shader_modules{};
    std::array<vk::Pipeline, 4> pipelines{};
    std::vector<Frame> frames{};
    bool disabled{};
};
} // namespace Vulkan
