// SPDX-License-Identifier: MIT
// Executable *postblock feature* path: C32 2x learned skip mix ->
// full-res learned FSwin -> learned [16,32] output projection.
// The 16 feature channels are NOT composited RGB or a presentable frame.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_postblock_mix.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"
#include "video_core/renderer_vulkan/nr_rdna2_image_feature_stage.h"
#include <string_view>
namespace Vulkan {
class Scheduler;
class NrRdna2PostblockRun final {
public:
    NrRdna2PostblockRun(const Device&,MemoryAllocator&,Scheduler&);
    NrRdna2PostblockRun(const NrRdna2PostblockRun&)=delete;
    NrRdna2PostblockRun& operator=(const NrRdna2PostblockRun&)=delete;
    // All five buffers must be distinct, writable/readable STORAGE resources
    // owned by the embedding renderer until GPU completion. Model contains
    // complete unpacked-postblock gains, FSwin tensors and projection weights.
    // A failed Prepare consumes this instance; reconstruct before retry.
    [[nodiscard]] bool Prepare(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view native_plan,
        VkDescriptorBufferInfo low_c32,
        VkDescriptorBufferInfo preblock_skip_c32,
        VkDescriptorBufferInfo mixed_full_c32,
        VkDescriptorBufferInfo swin_full_c32,
        VkDescriptorBufferInfo projected_fp16_16);
    // Optional numerically faithful final projection: the last Swin
    // dense-3 value is published as RAW FP16 C32 in parallel with FP8.
    // The [16,32] learned projector reads that RAW input, not FP8.
    [[nodiscard]] bool PrepareWithRawProjection(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view native_plan,
        VkDescriptorBufferInfo low_c32,
        VkDescriptorBufferInfo preblock_skip_c32,
        VkDescriptorBufferInfo mixed_full_c32,
        VkDescriptorBufferInfo swin_full_c32,
        VkDescriptorBufferInfo raw_swin_fp16_c32,
        VkDescriptorBufferInfo projected_fp16_16);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo ResultFeatures() const noexcept{
        return recorded?projection.Result():VkDescriptorBufferInfo{};
    }
private:
    [[nodiscard]] bool PrepareImpl(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view native_plan,
        VkDescriptorBufferInfo low_c32,
        VkDescriptorBufferInfo preblock_skip_c32,
        VkDescriptorBufferInfo mixed_full_c32,
        VkDescriptorBufferInfo swin_full_c32,
        VkDescriptorBufferInfo raw_swin_fp16_c32,
        VkDescriptorBufferInfo projected_fp16_16);
    Scheduler& scheduler;
    NrRdna2PostblockMix mix;
    NrRdna2FSwinChain body;
    NrRdna2ImageFeatureStage projection;
    bool prepare_attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
