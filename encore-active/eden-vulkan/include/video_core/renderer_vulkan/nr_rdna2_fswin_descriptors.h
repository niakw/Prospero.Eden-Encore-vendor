// SPDX-License-Identifier: MIT
// Own the six Vulkan storage descriptor sets for one FSwin layer graph.
// The caller must own input/output tensor buffers and weight buffer until
// all GPU commands referencing these sets are completed.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_graph.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
struct FSwinGraphBuffers {
    VkDescriptorBufferInfo input{};
    VkDescriptorBufferInfo expanded{};
    VkDescriptorBufferInfo middle{}; // unused for C=32
    VkDescriptorBufferInfo contracted{};
    VkDescriptorBufferInfo query{};
    VkDescriptorBufferInfo key{};
    VkDescriptorBufferInfo value{};
    VkDescriptorBufferInfo context{};
    VkDescriptorBufferInfo output{};
    VkDescriptorBufferInfo weights{};
};
class NrRdna2FSwinDescriptors final {
public:
    explicit NrRdna2FSwinDescriptors(const Device& device);
    ~NrRdna2FSwinDescriptors();
    NrRdna2FSwinDescriptors(const NrRdna2FSwinDescriptors&)=delete;
    NrRdna2FSwinDescriptors& operator=(const NrRdna2FSwinDescriptors&)=delete;

    [[nodiscard]] bool Initialize(
        const NrRdna2FSwinGraph& kernels,
        const Eden::Graphics::NeuralRDNA2::FSwinGraph& plan,
        const FSwinGraphBuffers& buffers);
    [[nodiscard]] const FSwinGraphDescriptorSets& Sets() const noexcept {
        return assigned;
    }
private:
    const Device& device;
    vk::DescriptorPool pool;
    vk::DescriptorSets allocations;
    FSwinGraphDescriptorSets assigned{};
};
} // namespace Vulkan
