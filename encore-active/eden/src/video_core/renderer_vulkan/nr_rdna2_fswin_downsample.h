// SPDX-License-Identifier: MIT
#pragma once
#include "rdna2_nr_fswin_downsample_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2FSwinDownsample final {
public:
    NrRdna2FSwinDownsample(const Device&,Scheduler&);
    ~NrRdna2FSwinDownsample();
    [[nodiscard]] bool Record(
        VkDescriptorSet set,
        const Eden::Graphics::NeuralRDNA2::FSwinDownsample& request,
        const Eden::Graphics::NeuralRDNA2::FSwinDownsampleStorage& storage);
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
