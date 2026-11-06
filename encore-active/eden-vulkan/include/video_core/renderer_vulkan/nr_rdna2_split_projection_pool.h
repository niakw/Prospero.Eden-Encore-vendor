// SPDX-License-Identifier: MIT
#pragma once
#include "rdna2_nr_split_projection_pool_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2SplitProjectionPool final {
public:
    NrRdna2SplitProjectionPool(const Device&,Scheduler&);
    ~NrRdna2SplitProjectionPool();
    [[nodiscard]] bool Record(
        VkDescriptorSet,VkDescriptorBufferInfo scratch,
        VkDescriptorBufferInfo residual,VkDescriptorBufferInfo gains,
        VkDescriptorBufferInfo output,
        const Eden::Graphics::NeuralRDNA2::SplitProjectionPool&);
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
