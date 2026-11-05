// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// PS5 native host bridge for the real AMD FSR3.1 temporal reconstruction.
// Not an automatic end-of-frame substitute: a verified GUEST producer must
// supply all three images, projection and history before dispatch is possible.
#include <memory>
#include <optional>
#include "fsr31_temporal_contract.h"
#include "video_core/renderer_vulkan/vk_fsr3_upscaler.h"
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
class StagingBufferPool;
class DescriptorPool;
}
namespace Eden::Graphics {
struct Fsr31NativeFrame {
    Temporal::Inputs metadata;
    VkImageView color{VK_NULL_HANDLE};
    VkImageView depth{VK_NULL_HANDLE};
    VkImageView motion{VK_NULL_HANDLE};
    // Real Vulkan views are handed to the bridge by the guest texture cache;
    // no format guessing based on size, handles or rendered frame pixels.
    VkFormat color_format{VK_FORMAT_UNDEFINED};
    VkFormat depth_format{VK_FORMAT_UNDEFINED};
    VkFormat motion_format{VK_FORMAT_UNDEFINED};
    float sharpness{0.5f};
    bool sharpen{true};
};
struct Fsr31NativeOutput {
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
};
class Fsr31NativeBridge final {
public:
    Fsr31NativeBridge(const Vulkan::Device& device,
                      Vulkan::Scheduler& scheduler,
                      Vulkan::MemoryAllocator& allocator,
                      Vulkan::StagingBufferPool& staging,
                      Vulkan::DescriptorPool& descriptors,
                      Vulkan::ComputePassDescriptorQueue& queue);
    ~Fsr31NativeBridge();
    Fsr31NativeBridge(const Fsr31NativeBridge&) = delete;
    Fsr31NativeBridge& operator=(const Fsr31NativeBridge&) = delete;
    [[nodiscard]] std::optional<Fsr31NativeOutput> TryRecord(const Fsr31NativeFrame& frame);
    [[nodiscard]] Temporal::Rejection LastRejection() const noexcept {
        return last_rejection;
    }
    void ResetHistory() noexcept;
private:
    const Vulkan::Device& device;
    Vulkan::Scheduler& scheduler;
    Vulkan::MemoryAllocator& memory_allocator;
    Vulkan::StagingBufferPool& staging_pool;
    Vulkan::DescriptorPool& descriptor_pool;
    Vulkan::ComputePassDescriptorQueue& descriptor_queue;
    Temporal::FrameContinuity continuity;
    std::unique_ptr<Vulkan::Fsr3Upscaler> upscaler;
    Temporal::Rejection last_rejection{Temporal::Rejection::MissingFrame};
    bool needs_reset{true};
    std::uint32_t last_render_width{},last_render_height{},
                  last_output_width{},last_output_height{};
};
} // namespace Eden::Graphics
