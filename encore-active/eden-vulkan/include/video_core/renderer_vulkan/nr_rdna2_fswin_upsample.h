// SPDX-License-Identifier: MIT
#pragma once
#include "rdna2_nr_fswin_upsample_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2FSwinUpsample final {
public:
    NrRdna2FSwinUpsample(const Device&,Scheduler&);
    ~NrRdna2FSwinUpsample();
    [[nodiscard]] bool Record(
        VkDescriptorSet set,
        const Eden::Graphics::NeuralRDNA2::FSwinUpsample& request,
        const Eden::Graphics::NeuralRDNA2::FSwinUpsampleStorage& storage);
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
