// SPDX-License-Identifier: MIT
// R09: ordered C32 -> C64 -> C128 -> C256 native U-Net down path.
// Consumes a HALF-RESOLUTION FP8 C32 raster supplied by the preblock.
// The preblock, Split/ViT bottleneck, up path and presenter are separate.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_level_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_cache.h"
#include "rdna2_nr_unet_down_sequence_policy.h"
#include <array>
#include <cstdint>
namespace Vulkan {
class Scheduler;
class NrRdna2UNetDownSequence final {
public:
    explicit NrRdna2UNetDownSequence(Scheduler&);
    NrRdna2UNetDownSequence(const NrRdna2UNetDownSequence&)=delete;
    NrRdna2UNetDownSequence&
        operator=(const NrRdna2UNetDownSequence&)=delete;
    // The four FSwin chains and the skip cache must already be initialized
    // for the SAME native model, remain live until GPU work has completed,
    // and be fresh (not recorded/bound by another owner).
    // down_rasters[0..2] feed the next level; [3] is the C512
    // low-resolution output destined for the Split-Swin bottleneck.
    // On any failure reconstruct all participating objects before retry.
    [[nodiscard]] bool Prepare(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        const Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap& graph,
        NrRdna2UNetSkipCache& cache,
        std::array<NrRdna2FSwinChain*,4> levels,
        VkDescriptorBufferInfo first_c32_raster,
        std::array<VkDescriptorBufferInfo,4> down_rasters);
    // Enqueue exactly once on one Vulkan scheduler: four FSwin sections,
    // four skip exports and four learned downsample epilogues.
    [[nodiscard]] bool Record();
    // Prepared input may be read for dependency identity checks before
    // any GPU submission. It is not evidence that a producer finished.
    [[nodiscard]] VkDescriptorBufferInfo PreparedInput() const noexcept {
        return ready?first_input:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    // Prepared C512 output identity before any down dispatch. Consumers
    // may bind resources but MUST NOT read pixels until recorded.
    [[nodiscard]] VkDescriptorBufferInfo PreparedLowRaster() const noexcept {
        return ready?low:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo LowRaster() const noexcept {
        return recorded?low:VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    std::array<NrRdna2FSwinLevelLink,3> links{};
    std::array<NrRdna2FSwinChain*,4> stages{};
    std::array<std::uint32_t,4> down_blocks{};
    NrRdna2UNetSkipCache* skips{};
    VkDescriptorBufferInfo first_input{};
    VkDescriptorBufferInfo low{};
    bool prepare_attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
