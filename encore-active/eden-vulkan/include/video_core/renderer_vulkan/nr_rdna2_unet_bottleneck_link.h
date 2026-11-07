// SPDX-License-Identifier: MIT
// R09 bridge from recorded U-Net C512 descent to the native bottleneck.
// Up path, learned pre/postblocks and PS5 presentation remain separate.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_unet_down_sequence.h"
#include "video_core/renderer_vulkan/nr_rdna2_neural_bottleneck.h"
#include "rdna2_nr_unet_bottleneck_link_policy.h"
namespace Vulkan {
class NrRdna2UNetBottleneckLink final {
public:
    explicit NrRdna2UNetBottleneckLink(Scheduler& s):scheduler{s}{}
    NrRdna2UNetBottleneckLink(const NrRdna2UNetBottleneckLink&)=delete;
    NrRdna2UNetBottleneckLink& operator=(const NrRdna2UNetBottleneckLink&)=delete;
    // Call after down.Record() enqueues all four producer/skip stages.
    // Caller owns stages, buffers, GPU fence and lifetime.
    [[nodiscard]] bool Bind(
        const Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap&,
        const Eden::Graphics::NeuralRDNA2::NativeSplitRoutes&,
        const Eden::Graphics::NeuralRDNA2::NativeVitRoute&,
        NrRdna2UNetDownSequence&,
        NrRdna2SplitSwinRun& first_split,
        NrRdna2NeuralBottleneck&);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return recorded?result:VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    NrRdna2NeuralBottleneck* core{};
    NrRdna2UNetDownSequence* producer{};
    VkDescriptorBufferInfo source{};
    VkDescriptorBufferInfo result{};
    bool bind_attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
