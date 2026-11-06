// SPDX-License-Identifier: MIT
// Portable RDNA2 grouped 3-stage FFWD, not complete inference.
#pragma once
#include "rdna2_nr_ffwd3_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2FFWD3 final {
public:
    NrRdna2FFWD3(const Device& device,Scheduler& scheduler);
    ~NrRdna2FFWD3();
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::FFWD3& request,
        const Eden::Graphics::NeuralRDNA2::FFWD3Storage& storage);
    [[nodiscard]] VkDescriptorSetLayout DescriptorLayout() const noexcept{
        return *descriptor_layout;
    }
private:
    Scheduler& scheduler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::ShaderModule shader;
    vk::PipelineLayout layout;
    vk::Pipeline pipeline;
};
} // namespace Vulkan
