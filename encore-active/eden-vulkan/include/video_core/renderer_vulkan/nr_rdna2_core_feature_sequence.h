// SPDX-License-Identifier: MIT
// R09: ordered feature-level neural graph, NOT an HDR/RGB frame presenter.
// The embedding renderer owns all stages, model weights, buffers and fences.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_preblock_run.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_bottleneck_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_up_sequence.h"
#include "video_core/renderer_vulkan/nr_rdna2_postblock_run.h"
#include <array>
#include <cstdint>
#include <string_view>
namespace Vulkan {
class NrRdna2CoreFeatureSequence final {
public:
    explicit NrRdna2CoreFeatureSequence(Scheduler& scheduler_);
    NrRdna2CoreFeatureSequence(const NrRdna2CoreFeatureSequence&)=delete;
    NrRdna2CoreFeatureSequence& operator=(const NrRdna2CoreFeatureSequence&)=delete;
    // All owners must be fresh and use the SAME scheduler. Preblock must
    // already be PrepareWithPool; down and bottleneck must be prepared.
    // The four up chains are prepared, but are NOT recorded yet.
    // This function consumes its sequence on the first valid attempt.
    // Failure at ANY point requires a fresh set of GPU stage owners.
    [[nodiscard]] bool Record(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view native_plan,
        const Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap& skip_graph,
        const Eden::Graphics::NeuralRDNA2::NativeSplitRoutes& split_routes,
        const Eden::Graphics::NeuralRDNA2::NativeVitRoute& vit_route,
        NrRdna2PreblockRun& preblock,
        NrRdna2UNetDownSequence& down,
        NrRdna2SplitSwinRun& first_split,
        NrRdna2NeuralBottleneck& bottleneck,
        NrRdna2UNetSkipCache& skips,
        NrRdna2UNetUpSequence& up,
        std::uint32_t split_result_block,
        std::array<NrRdna2FSwinChain*,4> up_chains,
        std::array<VkDescriptorBufferInfo,4> up_outputs,
        NrRdna2PostblockRun& postblock,
        VkDescriptorBufferInfo post_mixed,
        VkDescriptorBufferInfo post_swin,
        VkDescriptorBufferInfo post_raw_fp16,
        VkDescriptorBufferInfo post_fp16_features);
    [[nodiscard]] bool Recorded() const noexcept {return recorded;}
    // FP16 x16 FEATURES only; NOT a color image or swapchain output.
    [[nodiscard]] VkDescriptorBufferInfo ResultFeatures() const noexcept {
        return recorded?features:VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    NrRdna2UNetBottleneckLink low_link;
    VkDescriptorBufferInfo features{};
    bool attempted{},recorded{};
};
} // namespace Vulkan
