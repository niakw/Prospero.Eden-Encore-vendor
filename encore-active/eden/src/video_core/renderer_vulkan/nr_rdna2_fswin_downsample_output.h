// SPDX-License-Identifier: MIT
// Terminal learned FSwin _ds adapter. Consumes a completed C-channel
// window tensor, writes a 2C-channel half-resolution E4M3FN feature raster.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_downsample.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_downsample_weight_loader.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <cstdint>
#include <string_view>
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
class NrRdna2FSwinDownsampleOutput final {
public:
    NrRdna2FSwinDownsampleOutput(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2FSwinDownsampleOutput();
    NrRdna2FSwinDownsampleOutput(const NrRdna2FSwinDownsampleOutput&)=delete;
    NrRdna2FSwinDownsampleOutput& operator=(
        const NrRdna2FSwinDownsampleOutput&)=delete;
    // The model supplies <layer>.resample.bin [2C,C] E4M3FN.
    // Source and destination are independent STORAGE buffer descriptors,
    // offset=0, and live until all recorded commands complete.
    // Failed initialization requires constructing a new adapter.
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view prefix,
        VkDescriptorBufferInfo windows_source,
        VkDescriptorBufferInfo feature_destination,
        std::uint32_t width,std::uint32_t height,
        std::uint32_t channels,std::int32_t shift_x,
        std::int32_t shift_y);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] VkDescriptorBufferInfo Output() const noexcept{
        return ready?destination:VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    NrRdna2FSwinDownsample shader;
    NrRdna2GpuWeights weights;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    VkDescriptorBufferInfo destination{};
    Eden::Graphics::NeuralRDNA2::FSwinDownsample request{};
    Eden::Graphics::NeuralRDNA2::FSwinDownsampleStorage storage{};
    bool ready{};
};
} // namespace Vulkan
