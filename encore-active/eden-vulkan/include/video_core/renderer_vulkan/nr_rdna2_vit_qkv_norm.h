// SPDX-License-Identifier: MIT
#pragma once
#include "rdna2_nr_vit_qkv_norm_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2VitQkvNorm final {
public:
    NrRdna2VitQkvNorm(const Device&,Scheduler&);
    ~NrRdna2VitQkvNorm();
    [[nodiscard]] bool Record(
        VkDescriptorSet,
        const Eden::Graphics::NeuralRDNA2::VitQkvNorm&,
        const Eden::Graphics::NeuralRDNA2::VitQkvNormStorage&);
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
}
