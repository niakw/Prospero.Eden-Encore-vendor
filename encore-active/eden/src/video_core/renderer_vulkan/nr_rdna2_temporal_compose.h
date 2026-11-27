// SPDX-License-Identifier: MIT
// Actual temporal R09 output from a learned four-channel head.
// Caller owns all buffers, motion evidence, history parity and fences.
#pragma once
#include "rdna2_nr_temporal_compose_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2TemporalCompose final {
public:
    NrRdna2TemporalCompose(const Device&,Scheduler&);
    ~NrRdna2TemporalCompose();
    NrRdna2TemporalCompose(const NrRdna2TemporalCompose&)=delete;
    NrRdna2TemporalCompose& operator=(const NrRdna2TemporalCompose&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::uint32_t post_block,
        Eden::Graphics::NeuralRDNA2::NRTemporalComposeShape shape,
        VkDescriptorBufferInfo current_proxy,
        VkDescriptorBufferInfo learned_head,
        VkDescriptorBufferInfo actual_motion,
        VkDescriptorBufferInfo previous_history,
        VkDescriptorBufferInfo color_output,
        VkDescriptorBufferInfo next_history);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept {return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo ProxyInput() const noexcept {
        return ready?proxy:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo FeaturesInput() const noexcept {
        return ready?features:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] Eden::Graphics::NeuralRDNA2::NRTemporalComposeShape
    Geometry() const noexcept{return shape;}
    [[nodiscard]] VkDescriptorBufferInfo PreparedOutput() const noexcept {
        return ready?result:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return recorded?result:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo NextHistory() const noexcept {
        return recorded?next:VkDescriptorBufferInfo{};
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
    Eden::Graphics::NeuralRDNA2::NRTemporalComposeShape shape{};
    VkDescriptorBufferInfo proxy{},features{},result{},next{};
    bool attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
