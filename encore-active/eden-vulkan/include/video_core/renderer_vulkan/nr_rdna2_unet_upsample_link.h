// SPDX-License-Identifier: MIT
// Native U-Net upward edge: exact prior block + recorded skip -> learned
// upsample prologue -> next FSwin section, including shifted input windows.
// This handles one edge; it is NOT a complete whole-model scheduler.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_upsample_output.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_cache.h"
#include "video_core/renderer_vulkan/nr_rdna2_native_plan_bridge.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"
#include <cstdint>
#include <string_view>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2UNetUpsampleLink final {
public:
    NrRdna2UNetUpsampleLink(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2UNetUpsampleLink();
    NrRdna2UNetUpsampleLink(const NrRdna2UNetUpsampleLink&)=delete;
    NrRdna2UNetUpsampleLink&
        operator=(const NrRdna2UNetUpsampleLink&)=delete;
    // The low tensor must come from the native upsample's input0 block.
    // Its half-resolution raster and the skip raster must be produced on
    // the SAME scheduler; cache.RecordProducer must have succeeded.
    // output_raster is a caller-owned E4M3FN full-resolution buffer,
    // not an RGBA image nor a PS5 swapchain surface.
    [[nodiscard]] bool Prepare(
        const Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap& graph,
        NrRdna2UNetSkipCache& skips,
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::uint32_t up_block,std::uint32_t low_source_block,
        std::string_view weight_prefix,
        VkDescriptorBufferInfo low_raster,
        VkDescriptorBufferInfo output_raster,
        NrRdna2FSwinChain& next_chain);
    // Caller binds any terminal raster output of next_chain first.
    // Submit after cache.RecordProducer on the same command queue.
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
private:
    NrRdna2FSwinUpsampleOutput stage;
    Scheduler& scheduler;
    NrRdna2FSwinChain* next{};
    bool ready{};
    bool recorded{};
};
} // namespace Vulkan
