// SPDX-License-Identifier: MIT
// Eden Encore: RDNA2-compatible, model-agnostic 1x1 neural projection.
// Not a complete DLSS Neural Rendering engine; no NVIDIA weights or DLL.
#pragma once

#include "rdna2_nr_projection_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
class Device;
class Scheduler;

// Caller owns the three storage buffers and the descriptor set for their
// entire GPU execution lifetime; a future verified graph planner will own
// their allocation/fences. This standalone class cannot be selected from
// the application UI until the rest of the neural network is available.
class NrRdna2Projection final {
public:
    explicit NrRdna2Projection(const Device& device, Scheduler& scheduler);
    ~NrRdna2Projection();

    [[nodiscard]] bool Record(VkDescriptorSet validated_descriptor_set,
                              const Eden::Graphics::NeuralRDNA2::Projection& shape,
                              const Eden::Graphics::NeuralRDNA2::Storage& buffers);

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
