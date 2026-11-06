// SPDX-License-Identifier: MIT
// Reorder FinalHead's 4x4 tile-major FP8[1024] into ViT raster-major FP8.
// This cannot use the FSwin window->raster shader: the index is different.
#pragma once
#include "rdna2_nr_split_to_vit_raster_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2SplitToVitBridge final {
public:
    NrRdna2SplitToVitBridge(const Device&,Scheduler&);
    ~NrRdna2SplitToVitBridge();
    [[nodiscard]] bool Initialize(
        std::uint32_t width,std::uint32_t height,
        VkDescriptorBufferInfo final_head_tile_tokens,
        VkDescriptorBufferInfo vit_raster_tokens);
    [[nodiscard]] bool Record();
private:
    const Device& device;
    Scheduler& scheduler;
    vk::DescriptorSetLayout layout;
    vk::PipelineLayout pipeline_layout;
    vk::ShaderModule shader;
    vk::Pipeline pipeline;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    Eden::Graphics::NeuralRDNA2::SplitToVitRaster shape{};
    Eden::Graphics::NeuralRDNA2::SplitToVitRasterStorage storage{};
    bool ready{};
};
}
