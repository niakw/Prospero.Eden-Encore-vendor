// SPDX-License-Identifier: MIT
// GPU-native ProjPool primary E4M3FN skip layout adapter for decoder.
#pragma once
#include "rdna2_nr_split_decoder_skip_repack_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2SplitDecoderSkipRepack final {
public:
    NrRdna2SplitDecoderSkipRepack(const Device&,Scheduler&);
    ~NrRdna2SplitDecoderSkipRepack();
    NrRdna2SplitDecoderSkipRepack(const NrRdna2SplitDecoderSkipRepack&)=delete;
    NrRdna2SplitDecoderSkipRepack& operator=(const NrRdna2SplitDecoderSkipRepack&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::SplitDecoderSkipRepack&,
        VkDescriptorBufferInfo source_window_major,
        VkDescriptorBufferInfo destination_decoder_tiles);
    [[nodiscard]] bool Record();
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?output:VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    Scheduler& scheduler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::PipelineLayout layout;
    vk::ShaderModule shader;
    vk::Pipeline pipeline;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    Eden::Graphics::NeuralRDNA2::SplitDecoderSkipRepack plan{};
    Eden::Graphics::NeuralRDNA2::SplitDecoderSkipStorage storage{};
    VkDescriptorBufferInfo output{};
    bool ready{};
};
} // namespace Vulkan
