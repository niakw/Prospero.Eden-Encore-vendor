// SPDX-License-Identifier: MIT
// Prepare 16 FP16 no-history feature lanes from an already normalized
// display proxy. Ownership of source/output VkBuffers remains external.
#pragma once
#include "rdna2_nr_proxy_feature_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2ProxyFeatureStage final {
public:
    NrRdna2ProxyFeatureStage(const Device&,Scheduler&);
    ~NrRdna2ProxyFeatureStage();
    NrRdna2ProxyFeatureStage(const NrRdna2ProxyFeatureStage&)=delete;
    NrRdna2ProxyFeatureStage& operator=(const NrRdna2ProxyFeatureStage&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralProxyFeatures&,
        VkDescriptorBufferInfo normalized_proxy,
        VkDescriptorBufferInfo half_feature_output);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    [[nodiscard]] Eden::Graphics::NeuralRDNA2::NeuralProxyFeatures
    Geometry() const noexcept {return params;}
    [[nodiscard]] VkDescriptorBufferInfo Input() const noexcept {
        return ready?source:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?result:VkDescriptorBufferInfo{};
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
    VkDescriptorBufferInfo source{},result{};
    Eden::Graphics::NeuralRDNA2::NeuralProxyFeatures params{};
    Eden::Graphics::NeuralRDNA2::NeuralProxyStorage capacity{};
    bool ready{};
};
} // namespace Vulkan
