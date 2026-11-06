// SPDX-License-Identifier: MIT
// Common 8x8 window -> 4x4-padded raster byte permutation, reused at
// Split-Swin FinalHead [1024] -> ViT input [1024]. No new shader.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_raster_layout.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2SplitToVitBridge final {
public:
    NrRdna2SplitToVitBridge(const Device&,Scheduler&);
    ~NrRdna2SplitToVitBridge();
    [[nodiscard]] bool Initialize(
        std::uint32_t windows_x,std::uint32_t windows_y,
        VkDescriptorBufferInfo final_head_window_tokens,
        VkDescriptorBufferInfo vit_raster_tokens);
    [[nodiscard]] bool Record();
private:
    const Device& device;
    NrRdna2FSwinRasterLayout transposer;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    Eden::Graphics::NeuralRDNA2::FSwinRasterLayout shape{};
    Eden::Graphics::NeuralRDNA2::FSwinRasterLayoutStorage storage{};
    bool ready{};
};
}
