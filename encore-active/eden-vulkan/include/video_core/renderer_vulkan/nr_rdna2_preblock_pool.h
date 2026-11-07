// SPDX-License-Identifier: MIT
// Standalone FP16 C32 full-resolution -> E4M3 C32 half-resolution pooling.
// The input MUST be raw FP16 output of preblock Swin, not its FP8 skip.
// This stage does not bypass unimplemented raw-output extraction.
#pragma once
#include "rdna2_nr_preblock_pool_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2PreblockPool final {
public:
    NrRdna2PreblockPool(const Device&,Scheduler&);
    ~NrRdna2PreblockPool();
    NrRdna2PreblockPool(const NrRdna2PreblockPool&)=delete;
    NrRdna2PreblockPool& operator=(const NrRdna2PreblockPool&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::PreblockPoolShape&,
        VkDescriptorBufferInfo raw_fp16_full_c32,
        VkDescriptorBufferInfo pooled_fp8_half_c32);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?output:VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    Scheduler& scheduler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::PipelineLayout layout;
    vk::ShaderModule shader;
    vk::Pipeline pipeline;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    Eden::Graphics::NeuralRDNA2::PreblockPoolShape shape{};
    Eden::Graphics::NeuralRDNA2::PreblockPoolStorage storage{};
    VkDescriptorBufferInfo output{};
    bool ready{},record_attempted{};
};
} // namespace Vulkan
