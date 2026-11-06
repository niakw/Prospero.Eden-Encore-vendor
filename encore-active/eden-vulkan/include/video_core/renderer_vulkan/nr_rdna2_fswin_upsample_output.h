// SPDX-License-Identifier: MIT
// A complete Vulkan FSwin _upsample PROLOGUE:
// [half-res,2C] + [full-res,C] skip -> [full-res,C] E4M3FN raster.
// The caller retains all three activation buffers through GPU completion.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_upsample.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_upsample_weight_loader.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <cstdint>
#include <string_view>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2FSwinUpsampleOutput final {
public:
    NrRdna2FSwinUpsampleOutput(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2FSwinUpsampleOutput();
    NrRdna2FSwinUpsampleOutput(const NrRdna2FSwinUpsampleOutput&)=delete;
    NrRdna2FSwinUpsampleOutput&
        operator=(const NrRdna2FSwinUpsampleOutput&)=delete;
    // The model must contain a full resample.bin [C,2C] and
    // upsample_gain.bin [C] F16. No guessed/padded gains are permitted.
    // Failed initialization requires constructing a fresh adapter.
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view prefix,VkDescriptorBufferInfo low_raster,
        VkDescriptorBufferInfo skip_raster,VkDescriptorBufferInfo destination,
        std::uint32_t width,std::uint32_t height,std::uint32_t channels);
    // Record after the low-resolution and skip producers, on the same
    // scheduler, and before the destination FSwin InputView chain.
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept {return ready;}
    [[nodiscard]] VkDescriptorBufferInfo Output() const noexcept {
        return ready?destination:VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    NrRdna2FSwinUpsample shader;
    NrRdna2GpuWeights projection;
    NrRdna2GpuWeights gains;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    VkDescriptorBufferInfo destination{};
    Eden::Graphics::NeuralRDNA2::FSwinUpsample request{};
    Eden::Graphics::NeuralRDNA2::FSwinUpsampleStorage storage{};
    bool ready{};
};
} // namespace Vulkan
