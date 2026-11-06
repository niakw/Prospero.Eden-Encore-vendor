// SPDX-License-Identifier: MIT
// RDNA2 noise-field primitive, independent of model weights and inference.
#pragma once
#include "rdna2_nr_noise_field_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2NoiseField final {
public:
    NrRdna2NoiseField(const Device& device, Scheduler& scheduler);
    ~NrRdna2NoiseField();
    // validated_set must remain live until the scheduler has submitted work.
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::NoiseField& request,
        std::uint64_t output_storage_bytes);
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
