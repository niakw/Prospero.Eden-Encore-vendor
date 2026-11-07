// SPDX-License-Identifier: MIT
// R09 four-stage native U-Net ascent: C512 -> C256 -> C128 -> C64 -> C32.
// Requires a separately produced C512 bottleneck raster and four completed
// skip-producer recordings. No HDR/postblock/PS5 presentation is implied.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_unet_upsample_link.h"
#include "rdna2_nr_unet_up_sequence_policy.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2UNetUpSequence final {
public:
    NrRdna2UNetUpSequence(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2UNetUpSequence();
    NrRdna2UNetUpSequence(const NrRdna2UNetUpSequence&)=delete;
    NrRdna2UNetUpSequence& operator=(const NrRdna2UNetUpSequence&)=delete;
    // Native plan rows and NRMODEL1 weights must match the FOUR up edges.
    // The first_low_block must be the native Split-Swin outview producer,
    // not an arbitrary buffer. Caller retains all buffer/model lifetimes.
    // Any preparation failure requires rebuilding all participating stages.
    [[nodiscard]] bool Prepare(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view native_plan,
        const Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap& graph,
        NrRdna2UNetSkipCache& skips,
        std::uint32_t first_low_block,
        VkDescriptorBufferInfo first_c512_raster,
        std::array<NrRdna2FSwinChain*,4> up_chains,
        std::array<VkDescriptorBufferInfo,4> output_rasters);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] VkDescriptorBufferInfo FinalC32Raster() const noexcept {
        return recorded?last:VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    std::array<std::unique_ptr<NrRdna2UNetUpsampleLink>,4> links{};
    NrRdna2UNetSkipCache* skips{};
    VkDescriptorBufferInfo last{};
    bool prepare_attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
