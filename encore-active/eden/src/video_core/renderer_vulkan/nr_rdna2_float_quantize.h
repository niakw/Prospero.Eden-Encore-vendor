// SPDX-License-Identifier: MIT
#pragma once
// GPU-only FP32->E4M3FN bridge for linear RDNA2 projection outputs.
#include "rdna2_nr_float_quantize_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2FloatQuantize final {
public:
    NrRdna2FloatQuantize(const Device&,Scheduler&);
    ~NrRdna2FloatQuantize();
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,VkDescriptorBufferInfo input,
        VkDescriptorBufferInfo output,
        const Eden::Graphics::NeuralRDNA2::FloatQuantize& request);
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
