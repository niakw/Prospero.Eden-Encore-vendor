// SPDX-License-Identifier: MIT
// Portable GPU FP32->FP16 decoder projection epilogue, no float16 extension.
#pragma once
#include "rdna2_nr_decoder_projection_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2DecoderFp32ToFp16 final {
public:
    NrRdna2DecoderFp32ToFp16(const Device&,Scheduler&);
    ~NrRdna2DecoderFp32ToFp16();
    [[nodiscard]] bool Record(
        VkDescriptorSet,
        VkDescriptorBufferInfo fp32_source,
        VkDescriptorBufferInfo fp16_destination,
        const Eden::Graphics::NeuralRDNA2::DecoderFp32ToFp16&);
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
