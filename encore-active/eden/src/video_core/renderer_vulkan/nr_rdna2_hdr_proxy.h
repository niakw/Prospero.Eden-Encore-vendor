// SPDX-License-Identifier: MIT
// R09 HDR -> normalized sRGB feature proxy. Not a frame presenter.
#pragma once
#include "rdna2_nr_hdr_proxy_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2HDRProxy final {
public:
    NrRdna2HDRProxy(const Device&,Scheduler&);
    ~NrRdna2HDRProxy();
    NrRdna2HDRProxy(const NrRdna2HDRProxy&)=delete;
    NrRdna2HDRProxy& operator=(const NrRdna2HDRProxy&)=delete;
    [[nodiscard]] bool Initialize(
        Eden::Graphics::NeuralRDNA2::NRHDRProxyShape,
        VkDescriptorBufferInfo linear_hdr_rgba32f,
        VkDescriptorBufferInfo normalized_srgb_rgba32f);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept{return &scheduler;}
    [[nodiscard]] Eden::Graphics::NeuralRDNA2::NRHDRProxyShape
    Geometry() const noexcept {return shape;}
    [[nodiscard]] VkDescriptorBufferInfo HDRInput() const noexcept {
        return ready?source:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo ProxyOutput() const noexcept {
        return ready?output:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return recorded?output:VkDescriptorBufferInfo{};
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
    Eden::Graphics::NeuralRDNA2::NRHDRProxyShape shape{};
    VkDescriptorBufferInfo source{},output{};
    bool attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
