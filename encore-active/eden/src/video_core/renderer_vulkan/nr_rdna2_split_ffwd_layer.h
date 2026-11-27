// SPDX-License-Identifier: MIT
// NRMODEL1-backed Split-Swin FFWD3 GPU stage; not the full Split-Swin block.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_ffwd3.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_ffwd_weight_loader.h"
#include <memory>
#include <string_view>
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
class NrRdna2SplitFfwdLayer final {
public:
    NrRdna2SplitFfwdLayer(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2SplitFfwdLayer();
    NrRdna2SplitFfwdLayer(const NrRdna2SplitFfwdLayer&)=delete;
    NrRdna2SplitFfwdLayer& operator=(const NrRdna2SplitFfwdLayer&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::string_view prefix,std::uint32_t tokens,
        VkDescriptorBufferInfo input,VkDescriptorBufferInfo output);
    [[nodiscard]] bool Record();
private:
    const Device& device;
    MemoryAllocator& allocator;
    NrRdna2FFWD3 kernel;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    std::unique_ptr<NrRdna2GpuWeights> weights1,weights2,weights3;
    Eden::Graphics::NeuralRDNA2::FFWD3 shape{};
    Eden::Graphics::NeuralRDNA2::FFWD3Storage storage{};
    bool ready{};
};
} // namespace Vulkan
