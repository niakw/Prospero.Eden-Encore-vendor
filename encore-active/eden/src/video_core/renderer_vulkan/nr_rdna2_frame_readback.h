// SPDX-License-Identifier: MIT
// Real GPU readback of a fully recorded R09 session. This is not a test
// fixture or a substitute for an independent DLSS-NR image oracle.
#pragma once
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Vulkan {
class MemoryAllocator;
class Scheduler;
class NrRdna2FrameSession;

struct NrRdna2FrameGpuReadback {
    // A scheduler callback can contain multiple Vulkan commands; this is
    // intentionally NOT an "operations executed" or vkCmdDispatch count.
    std::uint64_t enqueued_callbacks{};
    std::string record_phase{};
    bool recorded{};
    bool gpu_completed{};
    bool readback_completed{};
    std::vector<std::uint8_t> rgba32f_le{};
};

// Caller supplies the SAME scheduler used to initialize frame, a live
// GENERAL-layout target image and the view previously passed to Initialize.
// Source image upload/creation is the embedding host's responsibility.
// This performs real GPU submission, waits for its completion and reads back
// a native 1920x1080 or research 1280x720 RGBA32F image; it does NOT
// fabricate per-plan-op evidence or an oracle.
// The owning frame and image must outlive this synchronous call.
[[nodiscard]] bool ExecuteR09FrameAndReadback(
    NrRdna2FrameSession& frame,
    MemoryAllocator& allocator,
    Scheduler& scheduler,
    VkImage target_image,
    VkImageView target_view,
    VkExtent2D target_extent,
    NrRdna2FrameGpuReadback& evidence);
} // namespace Vulkan
