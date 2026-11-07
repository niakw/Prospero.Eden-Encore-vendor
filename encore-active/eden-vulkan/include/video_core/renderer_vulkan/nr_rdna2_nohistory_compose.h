// SPDX-License-Identifier: MIT
// First-frame LDR neural residual compositor; NOT HDR, temporal history or present.
#pragma once
#include "rdna2_nr_nohistory_compose_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2NoHistoryCompose final {
public:
    NrRdna2NoHistoryCompose(const Device&,Scheduler&);
    ~NrRdna2NoHistoryCompose();
    NrRdna2NoHistoryCompose(const NrRdna2NoHistoryCompose&)=delete;
    NrRdna2NoHistoryCompose& operator=(const NrRdna2NoHistoryCompose&)=delete;
    // All buffers are separate, 0-offset, in pixel-major raster order.
    // Proxy must already be normalized to display 0..1; never pass raw HDR.
    [[nodiscard]] bool Initialize(
        Eden::Graphics::NeuralRDNA2::NRNoHistoryComposeShape shape,
        VkDescriptorBufferInfo normalized_proxy_rgba32f,
        VkDescriptorBufferInfo final_fp16_16,
        VkDescriptorBufferInfo color_rgba32f);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {return &scheduler;}
    [[nodiscard]] VkDescriptorBufferInfo ProxyInput() const noexcept {
        return ready?proxy:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo FeaturesInput() const noexcept {
        return ready?features:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return recorded?result:VkDescriptorBufferInfo{};
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
    Eden::Graphics::NeuralRDNA2::NRNoHistoryComposeShape shape{};
    VkDescriptorBufferInfo proxy{},features{},result{};
    bool attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
