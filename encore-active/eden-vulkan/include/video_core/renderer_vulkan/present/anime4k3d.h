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
// no proprietary external model and no downloaded ROM texture replacement.
// Four pinned Anime4K v4 3D CNN stages in Vulkan compute, source/MIT in vendor.
class Anime4K3D final {
public:
    enum class Profile : u8 {
        Details, // 3DGraphics CNN v4: 4 native convolution/reconstruction stages
        Soft,    // Restore Soft CNN v4: 4 stages, then 3DGraphics CNN x2
        Denoise, // Upscale+Denoise CNN v3.2: 5 native stages
    };
    Anime4K3D(const Device&, MemoryAllocator&, Scheduler&, size_t image_count,
              VkExtent2D output_extent, Profile profile = Profile::Details);
    ~Anime4K3D();

    [[nodiscard]] std::optional<VkImageView> Draw(
        size_t image_index, VkImage source_image, VkImageView source_view,
        VkExtent2D input_extent, const Common::Rectangle<f32>& crop);

    // Research-only, non-owning dual-handle result for feeding the NEXT
    // spatial Vulkan filter on the same queue. The next filter requires the
    // actual VkImage AND VkImageView; view-only handoff is insufficient.
    // Lifetime belongs exclusively to Anime4K3D until its slot GPU tick
    // has retired; do not destroy either object from the consumer.
    struct OutputFrame {
        VkImage image{};
        VkImageView view{};
        VkExtent2D extent{};
    };
    [[nodiscard]] std::optional<OutputFrame> DrawFrame(
        size_t image_index, VkImage source_image, VkImageView source_view,
        VkExtent2D input_extent, const Common::Rectangle<f32>& crop);

private:
    struct Frame {
        // Scratch 0/1 reused by convolutions; Soft also needs scratch 2
        // for restored input. Image 3 is always the separate x2 output.
        std::array<vk::Image, 4> images{};
        std::array<vk::ImageView, 4> views{};
        vk::DescriptorSets sets{};
        bool initialized{};
    };

    struct StageIO {
        int source{-1};   // -1: original input, otherwise a scratch image
        int features{-1}; // -1: no auxiliary sampled CNN feature image
        int destination{}; // 0/1/2 scratch, 3 output
    };
    [[nodiscard]] bool InitializeFrames(VkExtent2D input_extent);

    Profile profile;
    std::array<StageIO, 8> stages{};
    u32 stage_count{};
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
    std::array<vk::ShaderModule, 8> shader_modules{};
    std::array<vk::Pipeline, 8> pipelines{};
    std::vector<Frame> frames{};
    bool disabled{};
};
} // namespace Vulkan
