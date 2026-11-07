// SPDX-License-Identifier: MIT
// Publish the recorded first-frame neural RGBA32F raster to an image view.
// No PS5 swapchain/image layout ownership or hardware execution is implied.
#pragma once
#include "rdna2_nr_nohistory_present_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2NoHistoryPresent final {
public:
    NrRdna2NoHistoryPresent(const Device&,Scheduler&);
    ~NrRdna2NoHistoryPresent();
    NrRdna2NoHistoryPresent(const NrRdna2NoHistoryPresent&)=delete;
    NrRdna2NoHistoryPresent& operator=(const NrRdna2NoHistoryPresent&)=delete;
    // Target image MUST be VK_FORMAT_R32G32B32A32_SFLOAT with
    // VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT and GENERAL
    // layout, externally synchronized. Its physical extent is (H,W).
    // Caller checks exact format, usage, lifetime, and transitions.
    [[nodiscard]] bool Initialize(
        Eden::Graphics::NeuralRDNA2::NRNoHistoryPresentShape shape,
        VkDescriptorBufferInfo native_rgba32f,
        VkImageView output_image_view,
        VkFormat output_image_format,
        VkExtent2D output_extent);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept{
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo Input() const noexcept{
        return ready?source:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkImageView ResultView() const noexcept{
        return recorded?destination:VK_NULL_HANDLE;
    }
    [[nodiscard]] Eden::Graphics::NeuralRDNA2::NRNoHistoryPresentShape
    Geometry() const noexcept{return shape;}
private:
    const Device& device;
    Scheduler& scheduler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::PipelineLayout layout;
    vk::ShaderModule shader;
    vk::Pipeline pipeline;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    Eden::Graphics::NeuralRDNA2::NRNoHistoryPresentShape shape{};
    VkDescriptorBufferInfo source{};
    VkImageView destination{VK_NULL_HANDLE};
    bool attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
