// SPDX-License-Identifier: MIT
// RDNA2 FSwin dense stages. The caller sequences stage 0,1,2, QKV, attention, 3.
#pragma once
#include "rdna2_nr_fswin_dense_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2FSwinDense final {
public:
    NrRdna2FSwinDense(const Device& device, Scheduler& scheduler);
    ~NrRdna2FSwinDense();
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::FSwinDense& request,
        const Eden::Graphics::NeuralRDNA2::FSwinDenseStorage& storage);
    [[nodiscard]] VkDescriptorSetLayout DescriptorLayout() const noexcept {
        return *descriptor_layout;
    }
private:
    Scheduler& scheduler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::PipelineLayout layout;
    vk::ShaderModule shader;
    vk::Pipeline pipeline;
};
} // namespace Vulkan
