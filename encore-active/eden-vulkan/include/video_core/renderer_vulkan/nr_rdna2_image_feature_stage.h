// SPDX-License-Identifier: MIT
// Prepared 16-channel feature lift/project only. No image RGB, HDR, or
// depth/motion semantics are assumed until the model's I/O is established.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_image_feature_weight_loader.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "rdna2_nr_image_feature_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <memory>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2ImageFeatureStage final {
public:
    NrRdna2ImageFeatureStage(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2ImageFeatureStage();
    NrRdna2ImageFeatureStage(const NrRdna2ImageFeatureStage&)=delete;
    NrRdna2ImageFeatureStage& operator=(const NrRdna2ImageFeatureStage&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        Eden::Graphics::NeuralRDNA2::ImageFeatureDirection,
        std::uint32_t post_block,
        const Eden::Graphics::NeuralRDNA2::ImageFeatureShape&,
        VkDescriptorBufferInfo input,VkDescriptorBufferInfo output);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept {return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo Input() const noexcept {
        return ready?source:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] Eden::Graphics::NeuralRDNA2::ImageFeatureShape
    Geometry() const noexcept {return shape;}
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?output:VkDescriptorBufferInfo{};
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
    std::unique_ptr<NrRdna2GpuWeights> weights;
    Eden::Graphics::NeuralRDNA2::ImageFeatureShape shape{};
    Eden::Graphics::NeuralRDNA2::ImageFeatureCapacity capacity{};
    VkDescriptorBufferInfo source{},output{};
    bool ready{};
};
} // namespace Vulkan
