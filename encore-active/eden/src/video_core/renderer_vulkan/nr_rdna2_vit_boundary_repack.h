// SPDX-License-Identifier: MIT
// GPU-only native FinalHead 4x4-tile ↔ padded ViT 1D raster bridge.
#pragma once
#include "rdna2_nr_vit_boundary_repack_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2VitBoundaryRepack final {
public:
    NrRdna2VitBoundaryRepack(const Device&,Scheduler&);
    ~NrRdna2VitBoundaryRepack();
    [[nodiscard]] bool Record(
        VkDescriptorSet, VkDescriptorBufferInfo input,
        VkDescriptorBufferInfo output,
        const Eden::Graphics::NeuralRDNA2::VitBoundaryRepack&);
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
}
