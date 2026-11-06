// SPDX-License-Identifier: MIT
#pragma once
#include "rdna2_nr_split_context_repack_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2SplitContextRepack final {
public:
    NrRdna2SplitContextRepack(const Device&,Scheduler&);
    ~NrRdna2SplitContextRepack();
    [[nodiscard]] bool Record(
        VkDescriptorSet,VkDescriptorBufferInfo input,
        VkDescriptorBufferInfo output,
        const Eden::Graphics::NeuralRDNA2::SplitContextRepack&);
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
