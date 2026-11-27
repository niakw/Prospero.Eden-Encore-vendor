// SPDX-License-Identifier: MIT
// Owned Vulkan tensor buffers for one FSwin layer (VMA allocations).
// Input is host writable; all intermediate/output buffers are GPU local.
// Caller must synchronize before modifying input or destroying resources.
#pragma once
#include "rdna2_nr_fswin_graph_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_descriptors.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace Vulkan {
class Device;
class MemoryAllocator;
class NrRdna2FSwinGpuTensors final {
public:
    NrRdna2FSwinGpuTensors(const Device& device,MemoryAllocator& allocator);
    ~NrRdna2FSwinGpuTensors();
    NrRdna2FSwinGpuTensors(const NrRdna2FSwinGpuTensors&)=delete;
    NrRdna2FSwinGpuTensors& operator=(const NrRdna2FSwinGpuTensors&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::FSwinGraph& plan);
#if defined(PS5_NATIVE)
    // Research-only overload. Scratch handles are BORROWED from the parent
    // chain; layer still owns its distinct mapped input and GPU output.
    [[nodiscard]] bool InitializeWithScratch(
        const Eden::Graphics::NeuralRDNA2::FSwinGraph& plan,
        const std::array<VkDescriptorBufferInfo,4>& shared_scratch);
#endif
    [[nodiscard]] bool UploadInput(std::span<const std::uint8_t> fp8_tokens);
    [[nodiscard]] VkBuffer InputBuffer() const noexcept{return Info(0).buffer;}
    [[nodiscard]] VkBuffer OutputBuffer() const noexcept{return Info(8).buffer;}
    [[nodiscard]] VkDeviceSize InputSize() const noexcept{return Info(0).range;}
    [[nodiscard]] VkDeviceSize OutputSize() const noexcept{return Info(8).range;}
    [[nodiscard]] FSwinGraphBuffers Views(
        VkDescriptorBufferInfo weight_buffer) const noexcept;
private:
    [[nodiscard]] VkDescriptorBufferInfo Info(std::size_t index) const noexcept;
    const Device& device;
    MemoryAllocator& allocator;
    std::array<vk::Buffer,9> buffers{};
    std::array<VkDeviceSize,9> sizes{};
#if defined(PS5_NATIVE)
    std::array<VkDescriptorBufferInfo,4> shared_scratch{};
    bool shared_scratch_active{};
#endif
    bool ready{};
};
} // namespace Vulkan
