// SPDX-License-Identifier: MIT
// Runnable ViT QKV/attention subgraph from a GPU input and a local NRMODEL1.
// Neither complete ViT block nor complete DLSS-NR network.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_vit_gpu_tensors.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_qkv_attention_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include <cstdint>
#include <memory>
#include <string_view>
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
class NrRdna2VitQkvSubgraph final {
public:
    NrRdna2VitQkvSubgraph(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2VitQkvSubgraph();
    [[nodiscard]] bool Initialize(
        std::uint32_t tokens,
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view prefix,VkDescriptorBufferInfo gpu_input);
    [[nodiscard]] bool Record();
    [[nodiscard]] VkDescriptorBufferInfo Output() const noexcept{
        return tensors?tensors->Output():VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::unique_ptr<NrRdna2VitGpuTensors> tensors;
    std::unique_ptr<NrRdna2VitQkvAttentionLink> link;
};
}
