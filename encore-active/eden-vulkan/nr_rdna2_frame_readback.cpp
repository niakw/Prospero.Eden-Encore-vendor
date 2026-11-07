// SPDX-License-Identifier: MIT
// Synchronous execution / actual Vulkan image readback for the R09 frame.
// This is deliberately separate from renderer presentation and never runs
// automatically when a game enables another upscaler.
#include "video_core/renderer_vulkan/nr_rdna2_frame_readback.h"
#include "video_core/renderer_vulkan/nr_rdna2_frame_session.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <cstdint>
#include <utility>

namespace Vulkan {
bool ExecuteR09FrameAndReadback(
    NrRdna2FrameSession& frame,
    MemoryAllocator& allocator,
    Scheduler& scheduler,
    VkImage target_image,
    VkImageView target_view,
    VkExtent2D target_extent,
    NrRdna2FrameGpuReadback& evidence) {
    evidence = {};
    constexpr std::uint32_t width = 1920u, height = 1080u;
    constexpr std::uint64_t byte_count =
        std::uint64_t(width) * height * 4u * sizeof(float);
    if (!frame.Ready() || frame.SchedulerIdentity() != &scheduler ||
        target_image == VK_NULL_HANDLE || target_view == VK_NULL_HANDLE ||
        target_extent.width != width || target_extent.height != height) {
        return false;
    }
    // Stage allocation happens BEFORE Record, so a failure does not strand
    // partially enqueued neural work on the scheduler.
    auto staging = CreateWrappedBuffer(
        allocator, byte_count, MemoryUsage::Download);
    if (!staging) {
        return false;
    }
    const bool graph_recorded = frame.Record();
    evidence.enqueued_callbacks = frame.EnqueuedSchedulerCallbacks();
    evidence.record_phase = std::string(frame.LastRecordPhaseName());
    evidence.recorded = graph_recorded;
    if (!graph_recorded || frame.ResultView() != target_view) {
        // A failed Record can already have queued real GPU commands.
        // Retire them before the caller destroys model buffers or images.
        if (evidence.enqueued_callbacks != 0u) {
            scheduler.RequestOutsideRenderPassOperationContext();
            scheduler.Finish();
            evidence.gpu_completed = true;
        }
        return false;
    }
    scheduler.RequestOutsideRenderPassOperationContext();
    const VkBuffer mapped_buffer = *staging;
    scheduler.Record([target_image, mapped_buffer, byte_count](
                         vk::CommandBuffer cmd) {
        // The real target is GENERAL after NrRdna2NoHistoryPresent::Record.
        // This helper orders producer writes, image copy and layout restore.
        DownloadColorImage(cmd, target_image, mapped_buffer,
                           VkExtent3D{1920u, 1080u, 1u});
        const VkBufferMemoryBarrier to_host{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = mapped_buffer,
            .offset = 0u,
            .size = byte_count,
        };
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT, 0u, to_host);
    });
    // Unlike a queued scheduler callback count, Finish is a real GPU
    // submission and timeline wait, before any CPU-visible pixel read.
    scheduler.Finish();
    evidence.gpu_completed = true;
    staging.Invalidate();
    const auto mapped = staging.Mapped();
    if (mapped.size() < byte_count) {
        return false;
    }
    evidence.rgba32f_le.assign(mapped.begin(),
                              mapped.begin() + byte_count);
    evidence.readback_completed = true;
    return true;
}
} // namespace Vulkan
