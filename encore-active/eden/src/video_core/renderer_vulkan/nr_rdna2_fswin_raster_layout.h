// SPDX-License-Identifier: MIT
// Vulkan helper for E4M3FN feature raster <-> FSwin 8x8 window layout.
// No actual colour-image encoding is performed by this primitive.
#pragma once
#include "rdna2_nr_fswin_raster_layout_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2FSwinRasterLayout final {
public:
    NrRdna2FSwinRasterLayout(const Device& device,Scheduler& scheduler);
    ~NrRdna2FSwinRasterLayout();
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::FSwinRasterLayout& request,
        const Eden::Graphics::NeuralRDNA2::FSwinRasterLayoutStorage& storage);
    [[nodiscard]] VkDescriptorSetLayout DescriptorLayout() const noexcept {
        return *descriptor_layout;
    }
private:
    Scheduler& scheduler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::PipelineLayout layout;
    vk::ShaderModule shader;
    vk::Pipeline pipeline;
};
} // namespace Vulkan
