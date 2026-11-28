// SPDX-License-Identifier: MIT
// Unfused RDNA2 global ViT attention context pass, not DLSS integration.
#pragma once
#include "rdna2_nr_vit_attention_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2VitAttention final {
public:
    NrRdna2VitAttention(const Device& device,Scheduler& scheduler);
    ~NrRdna2VitAttention();
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::VitAttention& request,
        const Eden::Graphics::NeuralRDNA2::VitAttentionStorage& storage);
    [[nodiscard]] VkDescriptorSetLayout DescriptorLayout() const noexcept{
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
