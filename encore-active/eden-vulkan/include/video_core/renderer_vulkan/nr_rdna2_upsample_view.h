// SPDX-License-Identifier: MIT
// Portable RDNA2 upsample-view conversion, derived from the original
// MIT DLSSNR-AMD layout. Not a complete neural inference engine.
#pragma once
#include "rdna2_nr_upsample_view_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
class Device;
class Scheduler;

class NrRdna2UpsampleView final {
public:
    NrRdna2UpsampleView(const Device& device, Scheduler& scheduler);
    ~NrRdna2UpsampleView();
    // Caller supplies a live mutable storage-buffer descriptor set
    // bound to the same arena used for src/dst offsets.
    [[nodiscard]] bool Record(
        VkDescriptorSet validated_set,
        const Eden::Graphics::NeuralRDNA2::UpsampleView& request,
        std::uint64_t arena_bytes);

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
