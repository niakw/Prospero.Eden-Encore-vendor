// SPDX-License-Identifier: MIT
// R09: ordered feature-level neural graph, NOT an HDR/RGB frame presenter.
// The embedding renderer owns all stages, model weights, buffers and fences.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_preblock_run.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_bottleneck_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_up_sequence.h"
#include "video_core/renderer_vulkan/nr_rdna2_postblock_run.h"
#include "video_core/renderer_vulkan/nr_rdna2_proxy_feature_stage.h"
#include "video_core/renderer_vulkan/nr_rdna2_nohistory_compose.h"
#include "video_core/renderer_vulkan/nr_rdna2_hdr_proxy.h"
#include "video_core/renderer_vulkan/nr_rdna2_nohistory_present.h"
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
        VkDescriptorBufferInfo post_fp16_features,
        NrRdna2ProxyFeatureStage* optional_proxy=nullptr,
        NrRdna2NoHistoryCompose* optional_first_image=nullptr,
        NrRdna2HDRProxy* optional_hdr_input=nullptr,
        NrRdna2NoHistoryPresent* optional_present_image=nullptr);
    [[nodiscard]] bool Recorded() const noexcept {return recorded;}
    // FP16 x16 FEATURES only; NOT a color image or swapchain output.
    // Prepared sampled RGBA32F image view, usable by the same Vulkan
    // present scheduler after the command sequence has been enqueued.
    // Caller owns the image and transitions it to GENERAL before dispatch.
    [[nodiscard]] VkImageView ResultPresentView() const noexcept {
        return recorded?present_view:VK_NULL_HANDLE;
    }
    // Normalized RGBA32F first-frame raster, NOT HDR or PS5 presentable image.
    [[nodiscard]] VkDescriptorBufferInfo ResultFirstImage() const noexcept {
        return recorded?first_image:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo ResultFeatures() const noexcept {
        return recorded?features:VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    NrRdna2UNetBottleneckLink low_link;
    VkDescriptorBufferInfo features{},first_image{};
    VkImageView present_view{VK_NULL_HANDLE};
    bool attempted{},recorded{};
};
} // namespace Vulkan
