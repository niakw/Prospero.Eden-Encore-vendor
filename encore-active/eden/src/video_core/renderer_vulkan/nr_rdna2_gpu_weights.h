// SPDX-License-Identifier: MIT
// Immutable host-visible Vulkan storage buffer for user-supplied model weights.
// Create it once, retain it until every scheduler submission using it is done.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class MemoryAllocator;
class NrRdna2GpuWeights final {
public:
    NrRdna2GpuWeights(const Device& device, MemoryAllocator& allocator);
    ~NrRdna2GpuWeights();
    NrRdna2GpuWeights(const NrRdna2GpuWeights&)=delete;
    NrRdna2GpuWeights& operator=(const NrRdna2GpuWeights&)=delete;
    // Caller owns the staging bytes until this returns. No async CPU read.
    // May propagate Vulkan allocation exceptions to the embedding renderer.
    [[nodiscard]] bool Initialize(std::span<const std::uint8_t> cpu_bytes);
    [[nodiscard]] VkBuffer Handle() const noexcept {
        return buffer?*buffer:VK_NULL_HANDLE;
    }
    [[nodiscard]] VkDeviceSize Size() const noexcept{return bytes;}
    [[nodiscard]] VkDescriptorBufferInfo Descriptor() const noexcept {
        return VkDescriptorBufferInfo{.buffer=Handle(),.offset=0,.range=bytes};
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    vk::Buffer buffer;
    VkDeviceSize bytes{};
};
} // namespace Vulkan
