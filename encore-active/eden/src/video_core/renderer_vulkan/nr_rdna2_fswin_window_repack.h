// SPDX-License-Identifier: MIT
#pragma once
#include "rdna2_nr_fswin_window_repack_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2FSwinWindowRepack final {
public:
    NrRdna2FSwinWindowRepack(const Device& device,Scheduler& scheduler);
    ~NrRdna2FSwinWindowRepack();
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::FSwinWindowRepack& plan,
        const Eden::Graphics::NeuralRDNA2::FSwinWindowRepackStorage& storage);
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
