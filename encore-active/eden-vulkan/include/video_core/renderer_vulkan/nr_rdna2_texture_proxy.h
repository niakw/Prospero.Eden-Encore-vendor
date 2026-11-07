// SPDX-License-Identifier: MIT
// Copy a real sampled guest image into the first-frame neural proxy,
// with explicit source color mode and transpose. Not a frame presenter.
#pragma once
#include "rdna2_nr_texture_proxy_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2TextureProxy final {
public:
    NrRdna2TextureProxy(const Device&,Scheduler&);
    ~NrRdna2TextureProxy();
    NrRdna2TextureProxy(const NrRdna2TextureProxy&)=delete;
    NrRdna2TextureProxy& operator=(const NrRdna2TextureProxy&)=delete;
    // Input VkImage must already be shader-readable in GENERAL layout.
    // Source VkImageView and VkSampler remain live through submission.
    // Unorm8 = already display normalized; Float16/32 = linear HDR.
    [[nodiscard]] bool Initialize(
        Eden::Graphics::NeuralRDNA2::NRTextureProxyShape shape,
        VkImageView source_image_view,VkSampler source_sampler,
        VkFormat source_format,VkExtent2D physical_extent,
        VkDescriptorBufferInfo normalized_native_rgba32f);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept{
        return &scheduler;
    }
    [[nodiscard]] Eden::Graphics::NeuralRDNA2::NRTextureProxyShape
    Geometry() const noexcept{return shape;}
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept{
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
    Eden::Graphics::NeuralRDNA2::NRTextureProxyShape shape{};
    VkDescriptorBufferInfo output{};
    bool attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
