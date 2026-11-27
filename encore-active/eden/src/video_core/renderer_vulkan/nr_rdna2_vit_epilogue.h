// SPDX-License-Identifier: MIT
// Portable RDNA2 kernels for ViT expansion activation and FP16 residual
// epilogues. Descriptors are caller-owned; no complete network claim.
#pragma once
#include "rdna2_nr_vit_epilogue_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2VitEpilogue final {
public:
    NrRdna2VitEpilogue(const Device&,Scheduler&);
    ~NrRdna2VitEpilogue();
    [[nodiscard]] bool RecordActivation(
        VkDescriptorSet set,VkDescriptorBufferInfo source,
        VkDescriptorBufferInfo output,
        const Eden::Graphics::NeuralRDNA2::VitActivation& shape);
    [[nodiscard]] bool RecordResidual(
        VkDescriptorSet set,VkDescriptorBufferInfo source,
        VkDescriptorBufferInfo residual,VkDescriptorBufferInfo gain,
        VkDescriptorBufferInfo output,
        const Eden::Graphics::NeuralRDNA2::VitResidual& shape);
    [[nodiscard]] VkDescriptorSetLayout ActivationLayout() const noexcept{
        return *activation_layout;
    }
    [[nodiscard]] VkDescriptorSetLayout ResidualLayout() const noexcept{
        return *residual_layout;
    }
private:
    Scheduler& scheduler;
    vk::DescriptorSetLayout activation_layout,residual_layout;
    vk::PipelineLayout activation_pipeline_layout,residual_pipeline_layout;
    vk::ShaderModule activation_shader,residual_shader;
    vk::Pipeline activation_pipeline,residual_pipeline;
};
}
