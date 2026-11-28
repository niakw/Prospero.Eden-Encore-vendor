// SPDX-License-Identifier: MIT
// Learned original/compact PREBLOCK BODY: prepared FP16[16] -> input-lift
// E4M3FN[32] -> native C32 FSwin -> full-resolution E4M3FN[32] skip.
// NOTE: This does NOT implement the model's FP16 preblock 2x pooling.
// It must NOT be used as a full U-Net entry or a runnable NR presenter.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"
#include "video_core/renderer_vulkan/nr_rdna2_image_feature_stage.h"
#include "video_core/renderer_vulkan/nr_rdna2_preblock_pool.h"
#include "rdna2_nr_preblock_boundary_policy.h"
#include <array>
#include <cstdint>
#include <string_view>
namespace Vulkan {
class Scheduler;
class NrRdna2UNetDownSequence;
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
    // Complete feature-level preblock through the genuine pre-FP8
    // raw C32 values and native 2x2 FP16 -> FP8 pooling. Does not
    // synthesize image/HDR guides or complete the remaining U-Net.
    [[nodiscard]] bool PrepareWithPool(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view native_plan,
        VkDescriptorBufferInfo prepared_fp16_16,
        VkDescriptorBufferInfo lifted_fp8_c32,
        VkDescriptorBufferInfo fullres_skip_fp8_c32,
        VkDescriptorBufferInfo raw_fullres_fp16_c32,
        VkDescriptorBufferInfo pooled_halfres_fp8_c32);
    [[nodiscard]] bool Record();
    // Enqueue this preblock and its prepared U-Net C32->C512 descend
    // exactly once, in dependency order on a shared Vulkan scheduler.
    // Refuses an unrelated input buffer (even when byte sizes match).
    [[nodiscard]] bool RecordWithDownSequence(NrRdna2UNetDownSequence&);
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    // Literal-only diagnostic of the first rejected preparation boundary.
    // No private model bytes, image pixels or tensor identifiers.
    [[nodiscard]] std::string_view LastPreparePhaseName() const noexcept {
        return prepare_phase;
    }
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept{
        return &scheduler;
    }
    // Precomputed FP16 input identity and canonical raster geometry.
    // These are prepared storage descriptions, never completed GPU values.
    [[nodiscard]] VkDescriptorBufferInfo PreparedInputFeatures() const noexcept {
        return ready?prepared_input:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] std::array<std::uint32_t,2> FullRasterGeometry() const noexcept {
        return ready?full_geometry:std::array<std::uint32_t,2>{};
    }
    // Descriptor for binding dependent stages BEFORE GPU submission.
    // This is only prepared storage, NOT completed GPU output.
    [[nodiscard]] VkDescriptorBufferInfo PreparedHalfC32() const noexcept {
        return ready&&has_pool?pool.Result():VkDescriptorBufferInfo{};
    }
    // The half-res first U-Net level only after successful Record.
    [[nodiscard]] VkDescriptorBufferInfo ResultHalfC32() const noexcept {
        return recorded&&has_pool?pool.Result():VkDescriptorBufferInfo{};
    }
    // Prepared full-resolution skip storage, safe to bind before GPU work.
    // Its pixels have not been computed until successful Record.
    [[nodiscard]] VkDescriptorBufferInfo PreparedFullSkip() const noexcept{
        return ready?body.ResultRaster():VkDescriptorBufferInfo{};
    }
    // The full-res preblock skip (not the half-res U-Net input).
    [[nodiscard]] VkDescriptorBufferInfo ResultFullSkip() const noexcept{
        return recorded?body.ResultRaster():VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    NrRdna2ImageFeatureStage lift;
    NrRdna2FSwinChain body;
    NrRdna2PreblockPool pool;
    VkDescriptorBufferInfo prepared_input{};
    std::string_view prepare_phase{"unstarted"};
    std::array<std::uint32_t,2> full_geometry{};
    bool attempted{},ready{},record_attempted{},recorded{},has_pool{};
};
} // namespace Vulkan
