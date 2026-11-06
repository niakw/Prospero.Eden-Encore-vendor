// SPDX-License-Identifier: MIT
// RDNA2 scalar 2x neural decoder pass; not a complete DLSS NR engine.
#pragma once
#include "rdna2_nr_decoder_upsample_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2DecoderUpsample final {
public:
    NrRdna2DecoderUpsample(const Device& device, Scheduler& scheduler);
    ~NrRdna2DecoderUpsample();
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::DecoderUpsample& request,
        const Eden::Graphics::NeuralRDNA2::DecoderStorage& storage);
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
