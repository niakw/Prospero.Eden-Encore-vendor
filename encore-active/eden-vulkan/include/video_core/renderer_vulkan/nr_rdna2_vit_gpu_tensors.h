// SPDX-License-Identifier: MIT
// VMA-owned FP32 scratch + Q/K/V/context storage for one ViT attention.
#pragma once
#include "rdna2_nr_vit_gpu_tensors_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_qkv_attention_link.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace Vulkan {
class Device;
class MemoryAllocator;
class NrRdna2VitGpuTensors final {
public:
    NrRdna2VitGpuTensors(const Device&,MemoryAllocator&);
    ~NrRdna2VitGpuTensors();
    [[nodiscard]] bool Initialize(std::uint32_t tokens);
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] NrRdna2VitQkvAttentionBuffers Views(
        VkDescriptorBufferInfo external_gpu_input) const noexcept;
    [[nodiscard]] VkDescriptorBufferInfo Output() const noexcept {
        return Info(4u);
    }
private:
    [[nodiscard]] VkDescriptorBufferInfo Info(std::size_t index) const noexcept;
    const Device& device;
    MemoryAllocator& allocator;
    std::array<vk::Buffer,5> buffers{};
    std::array<VkDeviceSize,5> sizes{};
    bool ready{};
};
}
