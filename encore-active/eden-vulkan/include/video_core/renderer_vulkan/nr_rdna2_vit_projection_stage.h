// SPDX-License-Identifier: MIT
// One real Vit FFN Exp/Contract or residual Proj GPU stage. One FP32
// transient reused sequentially in token batches; no host readback.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_projection.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_epilogue.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_projection_layer_weight_loader.h"
#include "rdna2_nr_vit_projection_stage_policy.h"
#include <memory>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2VitProjectionStage final {
public:
    NrRdna2VitProjectionStage(
        const Device&,MemoryAllocator&,Scheduler&,
        Eden::Graphics::NeuralRDNA2::VitProjectionKind);
    ~NrRdna2VitProjectionStage();
    NrRdna2VitProjectionStage(const NrRdna2VitProjectionStage&)=delete;
    NrRdna2VitProjectionStage& operator=(const NrRdna2VitProjectionStage&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::uint32_t model_block,std::uint32_t tokens,
        VkDescriptorBufferInfo input,VkDescriptorBufferInfo scratch,
        VkDescriptorBufferInfo output,VkDescriptorBufferInfo residual={});
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept {return ready;}
private:
    const Device& device;
    MemoryAllocator& allocator;
    Eden::Graphics::NeuralRDNA2::VitProjectionKind kind;
    NrRdna2Projection gemm;
    NrRdna2VitEpilogue epilogue;
    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSets descriptor_sets;
    std::unique_ptr<NrRdna2GpuWeights> weights,gain;
    VkDescriptorBufferInfo scratch_info{},residual_info{},output_info{},gain_info{};
    Eden::Graphics::NeuralRDNA2::VitProjectionStageBatches batches{};
    bool ready{};
};
} // namespace Vulkan
