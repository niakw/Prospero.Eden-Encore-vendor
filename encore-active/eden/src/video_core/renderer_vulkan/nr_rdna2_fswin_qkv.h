// SPDX-License-Identifier: MIT
// Unfused upstream attention; separate RDNA2 QKV FP8 projection primitive.
#pragma once
#include "rdna2_nr_fswin_qkv_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2FSwinQkv final {
public:
    NrRdna2FSwinQkv(const Device& device,Scheduler& scheduler);
    ~NrRdna2FSwinQkv();
    // Each of the six descriptors must be live, validated and nonaliasing.
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::FSwinQkv& request,
        const Eden::Graphics::NeuralRDNA2::FSwinQkvStorage& storage);
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
