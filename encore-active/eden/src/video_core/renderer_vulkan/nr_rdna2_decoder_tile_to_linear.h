// SPDX-License-Identifier: MIT
// Convert native ViT's 4x4 tiled token-major C1024 into linear GEMM input.
#pragma once
#include "rdna2_nr_decoder_tile_to_linear_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2DecoderTileToLinear final {
public:
    NrRdna2DecoderTileToLinear(const Device&,Scheduler&);
    ~NrRdna2DecoderTileToLinear();
    NrRdna2DecoderTileToLinear(const NrRdna2DecoderTileToLinear&)=delete;
    NrRdna2DecoderTileToLinear& operator=(const NrRdna2DecoderTileToLinear&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::DecoderTileToLinear&,
        VkDescriptorBufferInfo input_tiles,
        VkDescriptorBufferInfo linear_output);
    [[nodiscard]] bool Record();
private:
    const Device& device;
    Scheduler& scheduler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::PipelineLayout layout;
    vk::ShaderModule shader;
    vk::Pipeline pipeline;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    Eden::Graphics::NeuralRDNA2::DecoderTileToLinear shape{};
    Eden::Graphics::NeuralRDNA2::DecoderTileToLinearStorage storage{};
    bool ready{};
};
} // namespace Vulkan
