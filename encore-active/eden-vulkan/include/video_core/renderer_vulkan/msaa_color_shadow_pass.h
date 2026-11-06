// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// R24 — real Vulkan multisample *colour* framebuffer with an automatic resolve
// into the game's original single-sampled attachment. Not yet guest-wired.
// Use only for an explicitly known full-target clear. LOAD/partial clears,
// guest aliases, multisampled guest image views and depth are rejected.
#include <optional>
#include "msaa_force_policy.h"
#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
class RenderPassCache;

class MsaaColorClearShadowPass final {
public:
    struct Target {
        VkImage guest_image{VK_NULL_HANDLE};
        VkImageView guest_view{VK_NULL_HANDLE};
        VkExtent2D extent{};
        VkFormat vk_format{VK_FORMAT_UNDEFINED};
        VideoCore::Surface::PixelFormat guest_format{
            VideoCore::Surface::PixelFormat::Invalid};
        unsigned layers{1};
        bool clear_entire_target{}; // partial LOAD/clear cannot use this path
        bool has_verified_guest_clear_color{}; // VkAttachmentLoadOp=CLEAR is not implicit black
        VkClearValue guest_clear_color{}; // actual game's typed VkClearValue
    };
    struct Prepared {
        VkRenderPass render_pass{VK_NULL_HANDLE};
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        VkExtent2D extent{};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        VkClearValue first_pass_clear{}; // must be passed to vkCmdBeginRenderPass
        // Caller must record a native guest-compatible graphics pipeline
        // with this rasterizationSamples, and end its renderpass for resolve.
        // Nothing in this struct mutates any guest image metadata.
    };

    MsaaColorClearShadowPass(const Device&, Scheduler&, MemoryAllocator&,
                             RenderPassCache&);
    ~MsaaColorClearShadowPass();
    MsaaColorClearShadowPass(const MsaaColorClearShadowPass&) = delete;
    MsaaColorClearShadowPass& operator=(const MsaaColorClearShadowPass&) = delete;

    [[nodiscard]] std::optional<Prepared> TryPrepare(
        const Target& target, Eden::Graphics::MsaaForce::Requested samples,
        const Eden::Graphics::MsaaForce::GuestPass& guest_pass,
        const Eden::Graphics::MsaaForce::Limits& verified_limits);
    void Reset();
    [[nodiscard]] Eden::Graphics::MsaaForce::Decision LastDecision() const noexcept {
        return last_decision;
    }

private:
    const Device& device;
    Scheduler& scheduler;
    MemoryAllocator& allocator;
    RenderPassCache& render_pass_cache;
    vk::Image msaa_image;
    vk::ImageView msaa_view;
    vk::Framebuffer framebuffer;
    VkRenderPass render_pass{VK_NULL_HANDLE}; // owned by RenderPassCache
    VkImage last_guest_image{VK_NULL_HANDLE};
    VkImageView last_guest_view{VK_NULL_HANDLE};
    VkExtent2D last_extent{};
    VkFormat last_format{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits last_samples{VK_SAMPLE_COUNT_1_BIT};
    Eden::Graphics::MsaaForce::Decision last_decision{
        Eden::Graphics::MsaaForce::Decision::NativeAuto};
};
} // namespace Vulkan
