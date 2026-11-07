// SPDX-License-Identifier: MIT
// Model-independent Vulkan bottleneck: first Split-Swin -> ViT8 ->
// learned decoder + first-Split skip -> second Split-Swin.
// All four stages, buffers, model objects and Vulkan scheduler are owned
// by the embedding renderer. This is NOT the full 152/76-op renderer.
#pragma once
#include "rdna2_nr_bottleneck_route_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_swin_run.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_run.h"
#include "video_core/renderer_vulkan/nr_rdna2_native_decoder_link.h"
namespace Vulkan {
class Scheduler;
class NrRdna2NeuralBottleneck final {
public:
    explicit NrRdna2NeuralBottleneck(Scheduler& scheduler);
    NrRdna2NeuralBottleneck(const NrRdna2NeuralBottleneck&)=delete;
    NrRdna2NeuralBottleneck&
        operator=(const NrRdna2NeuralBottleneck&)=delete;
    // All stages MUST have been initialized from the same native
    // NRMODEL1 plan, with precise live VkBuffer identities.
    [[nodiscard]] bool Bind(
        const Eden::Graphics::NeuralRDNA2::NativeSplitRoutes&,
        const Eden::Graphics::NeuralRDNA2::NativeVitRoute&,
        NrRdna2SplitSwinRun& first,
        NrRdna2VitRun& vit,
        NrRdna2NativeDecoderLink& decoder,
        NrRdna2SplitSwinRun& second);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo PreparedInputRaster() const noexcept {
        return ready&&first?first->SourceRaster():VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo PreparedResult() const noexcept {
        return ready&&second?second->Result():VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return recorded&&second?second->Result():VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    NrRdna2SplitSwinRun* first{};
    NrRdna2VitRun* vit{};
    NrRdna2NativeDecoderLink* decoder{};
    NrRdna2SplitSwinRun* second{};
    bool ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
