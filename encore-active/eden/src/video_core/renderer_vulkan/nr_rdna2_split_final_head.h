// SPDX-License-Identifier: MIT
// Native Split-Swin16H FinalHead (no residual): 512->1024 E4M3FN.
// Caller owns pooled input, FP32 scratch and FP8 output and supplies the
// complete padded token extent; tail input must already be zero-filled.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_projection_quantized.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_final_head_weight_loader.h"
#include <memory>
#include <string_view>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2SplitFinalHead final {
public:
    NrRdna2SplitFinalHead(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2SplitFinalHead();
    NrRdna2SplitFinalHead(const NrRdna2SplitFinalHead&)=delete;
    NrRdna2SplitFinalHead& operator=(const NrRdna2SplitFinalHead&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view prefix,std::uint32_t tokens,
        VkDescriptorBufferInfo pooled_input,
        VkDescriptorBufferInfo fp32_scratch,
        VkDescriptorBufferInfo fp8_output);
    [[nodiscard]] bool Record();
private:
    const Device& device;
    MemoryAllocator& allocator;
    NrRdna2ProjectionQuantized kernels;
    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSets sets;
    std::unique_ptr<NrRdna2GpuWeights> weights;
    VkDescriptorBufferInfo input{},scratch{},output{};
    Eden::Graphics::NeuralRDNA2::Projection projection{};
    bool ready{};
};
} // namespace Vulkan
