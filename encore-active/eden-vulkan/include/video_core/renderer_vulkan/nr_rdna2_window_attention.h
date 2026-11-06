// SPDX-License-Identifier: MIT
// RDNA2 64x32 unfused attention/context pass, not a DLSS feature.
#pragma once
#include "rdna2_nr_window_attention_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2WindowAttention final {
public:
    NrRdna2WindowAttention(const Device& device,Scheduler& scheduler);
    ~NrRdna2WindowAttention();
    // validated_set is nonaliasing, five live storage descriptors.
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::WindowAttention& request,
        const Eden::Graphics::NeuralRDNA2::AttentionStorage& storage);
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
