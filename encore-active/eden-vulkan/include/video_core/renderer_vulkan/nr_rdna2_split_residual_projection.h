// SPDX-License-Identifier: MIT
// Real GPU Split-Swin residual projection: FP8 GEMM->FP32 + FP16 gain*FP8
// -> E4M3FN. Reuses ViT residual kernel; no duplicate projection logic.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_projection.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_epilogue.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_residual_weight_loader.h"
#include <memory>
#include <string_view>
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
class NrRdna2SplitResidualProjection final {
public:
    NrRdna2SplitResidualProjection(
        const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2SplitResidualProjection();
    NrRdna2SplitResidualProjection(const NrRdna2SplitResidualProjection&)=delete;
    NrRdna2SplitResidualProjection& operator=(const NrRdna2SplitResidualProjection&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::string_view prefix,std::uint32_t tokens,
        VkDescriptorBufferInfo activations,VkDescriptorBufferInfo residual,
        VkDescriptorBufferInfo fp32_scratch,VkDescriptorBufferInfo output);
    [[nodiscard]] bool Record();
private:
    const Device& device;
    MemoryAllocator& allocator;
    NrRdna2Projection gemm;
    NrRdna2VitEpilogue epilogue;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    std::unique_ptr<NrRdna2GpuWeights> matrix,gain;
    Eden::Graphics::NeuralRDNA2::Projection projection_shape{};
    Eden::Graphics::NeuralRDNA2::Storage projection_storage{};
    Eden::Graphics::NeuralRDNA2::VitResidual residual_shape{};
    Eden::Graphics::NeuralRDNA2::VitEpilogueStorage residual_storage{};
    VkDescriptorBufferInfo scratch_info{},residual_info{},gain_info{},out_info{};
    bool ready{};
};
} // namespace Vulkan
