// SPDX-License-Identifier: MIT
// Learned original/compact PREBLOCK BODY: prepared FP16[16] -> input-lift
// E4M3FN[32] -> native C32 FSwin -> full-resolution E4M3FN[32] skip.
// NOTE: This does NOT implement the model's FP16 preblock 2x pooling.
// It must NOT be used as a full U-Net entry or a runnable NR presenter.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"
#include "video_core/renderer_vulkan/nr_rdna2_image_feature_stage.h"
#include "rdna2_nr_preblock_boundary_policy.h"
#include <string_view>
namespace Vulkan {
class Scheduler;
class NrRdna2PreblockRun final {
public:
    NrRdna2PreblockRun(const Device&,MemoryAllocator&,Scheduler&);
    NrRdna2PreblockRun(const NrRdna2PreblockRun&)=delete;
    NrRdna2PreblockRun& operator=(const NrRdna2PreblockRun&)=delete;
    // Assumes strictly prepared FULL-RASTER 16-channel FP16 features.
    // Cannot replace the yet-unimplemented HDR/game-feature producer.
    // No commands are recorded until Prepare succeeds for both stages.
    // Failed Prepare consumes this instance: reconstruct before retry.
    [[nodiscard]] bool Prepare(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view native_plan,
        VkDescriptorBufferInfo prepared_fp16_16,
        VkDescriptorBufferInfo lifted_fp8_c32,
        VkDescriptorBufferInfo fullres_skip_fp8_c32);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept{
        return &scheduler;
    }
    // The full-res preblock skip (not the half-res U-Net input).
    [[nodiscard]] VkDescriptorBufferInfo ResultFullSkip() const noexcept{
        return recorded?body.ResultRaster():VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    NrRdna2ImageFeatureStage lift;
    NrRdna2FSwinChain body;
    bool attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
