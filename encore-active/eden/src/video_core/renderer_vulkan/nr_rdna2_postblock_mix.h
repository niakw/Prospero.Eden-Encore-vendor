// SPDX-License-Identifier: MIT
// Full-res C32 postblock prelude: nearest 2x of low raster,
// learned per-channel main and preblock-skip gains, FP16 rounding,
// then FP8 publication for the trained postblock Swin body.
// No compositing, RGB output or renderer hook is implied.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_postblock_mix_weight_loader.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "rdna2_nr_postblock_mix_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <memory>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2PostblockMix final {
public:
    NrRdna2PostblockMix(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2PostblockMix();
    NrRdna2PostblockMix(const NrRdna2PostblockMix&)=delete;
    NrRdna2PostblockMix& operator=(const NrRdna2PostblockMix&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::uint32_t post_block,
        const Eden::Graphics::NeuralRDNA2::PostblockMixShape&,
        VkDescriptorBufferInfo low_c32,
        VkDescriptorBufferInfo preblock_skip_c32,
        VkDescriptorBufferInfo full_c32);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept{
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?destination:VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::PipelineLayout layout;
    vk::ShaderModule shader;
    vk::Pipeline pipeline;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    std::unique_ptr<NrRdna2GpuWeights> gains;
    Eden::Graphics::NeuralRDNA2::PostblockMixShape shape{};
    Eden::Graphics::NeuralRDNA2::PostblockMixCapacity capacity{};
    VkDescriptorBufferInfo destination{};
    bool ready{},record_attempted{};
};
} // namespace Vulkan
