// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>
#include <array>
#include <chrono>
#include <cstdio>
#include "dev_vulkan.h"
#include <mutex>
#include <thread>
#include <utility>

#include <fmt/format.h>

#include "video_core/renderer_vulkan/vk_query_cache.h"

#include "common/settings.h"
#include "common/thread.h"
#include "performance.h"
#ifdef PS5_NATIVE
#include "gpu_failure.h"
#endif
#include "video_core/gpu_logging/gpu_logging.h"
#include "video_core/renderer_vulkan/vk_command_pool.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

namespace {
// Development GPU execution time (dev_vulkan.h gpu_time): timestamps at the start and end
// of every scheduler command buffer, summed once the GPU has run them and printed every
// 5 s as EDEN_GPU_TIME. Worker thread only.
struct GpuTimeProbe {
    static constexpr u32 SLOTS = 512;
    vk::QueryPool pool;
    PFN_vkCmdWriteTimestamp write{};
    double period_ns{};
    u64 next{}, first_pending{};
    u64 session{};  // reset before first command of a new VkDevice/title
    u32 current{};
    u64 busy_ns{}, max_ns{}, submissions{};
    std::chrono::steady_clock::time_point report{};
    bool failed{};
} gpu_time;

void GpuTimeBegin(const Device& device, vk::CommandBuffer cmdbuf) {
    if (!::Eden::DevVulkan::gpu_time) return;
    const u64 session = ::Eden::DevVulkan::gpu_time_session.load(std::memory_order_acquire);
    if (gpu_time.session != session) {
        // Previous VkDevice teardown implicitly destroyed its query pool.
        // The process-global probe must discard that device's handle, counters
        // and failures before any query on the next title can be recorded.
        gpu_time = GpuTimeProbe{};
        gpu_time.session = session;
    }
    if (gpu_time.failed) return;
    if (!gpu_time.write) {
        const auto& dld = device.GetDispatchLoader();
        gpu_time.write = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
            dld.vkGetDeviceProcAddr(*device.GetLogical(), "vkCmdWriteTimestamp"));
        gpu_time.period_ns = device.GetPhysical().GetProperties().limits.timestampPeriod;
        if (!gpu_time.write || gpu_time.period_ns <= 0) { gpu_time.failed = true; return; }
        gpu_time.pool = device.GetLogical().CreateQueryPool({
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = GpuTimeProbe::SLOTS * 2,
            .pipelineStatistics = 0,
        });
        gpu_time.report = std::chrono::steady_clock::now();
    }
    if (gpu_time.next - gpu_time.first_pending >= GpuTimeProbe::SLOTS) {
        gpu_time.failed = true; // results stopped arriving; never overwrite a pending slot
        std::printf("EDEN_GPU_TIME failed=ring\n");
        return;
    }
    gpu_time.current = static_cast<u32>(gpu_time.next++ % GpuTimeProbe::SLOTS);
    cmdbuf.ResetQueryPool(*gpu_time.pool, gpu_time.current * 2, 2);
    gpu_time.write(*cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, *gpu_time.pool, gpu_time.current * 2);
}

void GpuTimeEnd(vk::CommandBuffer cmdbuf) {
    if (!::Eden::DevVulkan::gpu_time || gpu_time.failed || !gpu_time.write) return;
    gpu_time.write(*cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, *gpu_time.pool, gpu_time.current * 2 + 1);
}

void GpuTimeCollect(const Device& device) {
    if (!::Eden::DevVulkan::gpu_time || gpu_time.failed || !gpu_time.write) return;
#ifdef PS5_NATIVE
    // The optional GPU-time probe previously issued one driver query per
    // completed command buffer. Fetch up to 32 consecutive timestamp pairs
    // in one nonblocking call, stopping at the first unavailable pair.
    // Never cross the 512-slot ring boundary or consume the newest submit.
    constexpr u32 kQueryBatch = 32;
    std::array<u64, kQueryBatch * 4> samples{};
    while (gpu_time.first_pending + 1 < gpu_time.next) {
        const u32 slot = static_cast<u32>(gpu_time.first_pending % GpuTimeProbe::SLOTS);
        const u32 candidates = static_cast<u32>((std::min)(
            u64{kQueryBatch}, gpu_time.next - gpu_time.first_pending - 1));
        const u32 count = (std::min)(candidates, GpuTimeProbe::SLOTS - slot);
        // Unavailable results are undefined; clear availability words before
        // each poll so a reused batch cannot masquerade as completed.
        samples.fill(0);
        const VkResult result = device.GetLogical().GetQueryResults(
            *gpu_time.pool, slot * 2, count * 2, count * 4 * sizeof(u64),
            samples.data(), 2 * sizeof(u64),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result != VK_SUCCESS && result != VK_NOT_READY) break;
        u32 completed = 0;
        for (; completed < count; ++completed) {
            const u64* const pair = samples.data() + completed * 4;
            if (pair[1] == 0 || pair[3] == 0) break;
            const u64 ns = pair[2] > pair[0]
                               ? static_cast<u64>(double(pair[2] - pair[0]) * gpu_time.period_ns) : 0;
            gpu_time.busy_ns += ns;
            gpu_time.max_ns = (std::max)(gpu_time.max_ns, ns);
            ++gpu_time.submissions;
            ++gpu_time.first_pending;
        }
        if (completed != count) break;
    }
#else
    while (gpu_time.first_pending + 1 < gpu_time.next) {
        const u32 slot = static_cast<u32>(gpu_time.first_pending % GpuTimeProbe::SLOTS);
        std::array<u64, 2> stamps{};
        if (device.GetLogical().GetQueryResults(*gpu_time.pool, slot * 2, 2, sizeof(stamps), stamps.data(),
                                               sizeof(u64), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) break;
        const u64 ns = stamps[1] > stamps[0]
                           ? static_cast<u64>(double(stamps[1] - stamps[0]) * gpu_time.period_ns) : 0;
        gpu_time.busy_ns += ns;
        gpu_time.max_ns = (std::max)(gpu_time.max_ns, ns);
        ++gpu_time.submissions;
        ++gpu_time.first_pending;
    }
#endif
    const auto now = std::chrono::steady_clock::now();
    const auto wall = std::chrono::duration_cast<std::chrono::nanoseconds>(now - gpu_time.report).count();
    if (wall >= 5'000'000'000) {
        std::printf("EDEN_GPU_TIME wall_ms=%.1f busy_ms=%.1f submissions=%llu max_ms=%.2f\n", wall / 1e6,
                    gpu_time.busy_ns / 1e6, static_cast<unsigned long long>(gpu_time.submissions),
                    gpu_time.max_ns / 1e6);
        gpu_time.busy_ns = gpu_time.max_ns = gpu_time.submissions = 0;
        gpu_time.report = now;
    }
}

// VkQueryPool is a device child. It must be destroyed explicitly while the
// owner VkDevice remains live (not forgotten when the next title begins).
void GpuTimeDestroy(const Device& device) {
    if (!gpu_time.pool) return;
    const auto& dld = device.GetDispatchLoader();
    const VkDevice handle = *device.GetLogical();
    (void)dld.vkDeviceWaitIdle(handle);
    dld.vkDestroyQueryPool(handle, *gpu_time.pool, nullptr);
    gpu_time = GpuTimeProbe{};
}
} // namespace

void Scheduler::CommandChunk::ExecuteAll(vk::CommandBuffer cmdbuf,
                                         vk::CommandBuffer upload_cmdbuf) {
    auto command = first;
    while (command != nullptr) {
        auto next = command->GetNext();
        command->Execute(cmdbuf, upload_cmdbuf);
        command->~Command();
        command = next;
    }
    submit = false;
    command_offset = 0;
    first = nullptr;
    last = nullptr;
}

Scheduler::Scheduler(const Device& device_, StateTracker& state_tracker_)
    : device{device_}, state_tracker{state_tracker_},
      master_semaphore{std::make_unique<MasterSemaphore>(device)},
      command_pool{std::make_unique<CommandPool>(*master_semaphore, device)} {

    AcquireNewChunk();
    AllocateWorkerCommandBuffer();
    worker_thread = std::jthread([this](std::stop_token token) {
#ifdef PS5_NATIVE
        try {
            WorkerThread(token);
        } catch (...) {
            // Background setup failures must also reach native GPU observers.
            ::Eden::RecordGpuFailure(std::current_exception());
            event_cv.notify_all();
        }
#else
        WorkerThread(token);
#endif
    });
}

Scheduler::~Scheduler() {
    // The worker writes gpu_time.pool. Do not test that field until the
    // worker has stopped: the old conditional check raced with its first
    // CreateQueryPool on a quick title exit, risking use-after-free.
    worker_thread.request_stop();
    event_cv.notify_all();
    if (worker_thread.joinable()) worker_thread.join();
    if (gpu_time.pool) GpuTimeDestroy(device);
}

u64 Scheduler::Flush(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    // When flushing, we only send data to the worker thread; no waiting is necessary.
    const u64 signal_value = SubmitExecution(signal_semaphore, wait_semaphore);
    if (::Eden::DevVulkan::sync_submissions) {
        Wait(signal_value);
    }
    AllocateNewContext();
    return signal_value;
}

void Scheduler::Finish(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    // When finishing, we need to wait for the submission to have executed on the device.
    const u64 presubmit_tick = CurrentTick();
    SubmitExecution(signal_semaphore, wait_semaphore);
    Wait(presubmit_tick);
    AllocateNewContext();
}

void Scheduler::WaitWorker() {
    DispatchWork();

    // Ensure the queue is drained.
    {
        std::unique_lock ql{queue_mutex};
        ++queue_drain_waiters;
        event_cv.wait(ql, [this] {
#ifdef PS5_NATIVE
            return work_queue.empty() || static_cast<bool>(::Eden::PeekGpuFailure());
#else
            return work_queue.empty();
#endif
        });
        --queue_drain_waiters;
#ifdef PS5_NATIVE
        if (auto failure = ::Eden::PeekGpuFailure())
            std::rethrow_exception(failure);
#endif
    }

    // Now wait for execution to finish.
    std::scoped_lock el{execution_mutex};
#ifdef PS5_NATIVE
    // In-flight work publishes errors before releasing execution_mutex.
    if (auto failure = ::Eden::PeekGpuFailure())
        std::rethrow_exception(failure);
#endif
}

void Scheduler::DispatchWork() {
    if (chunk && !chunk->Empty()) {
        {
            std::scoped_lock ql{queue_mutex};
            work_queue.push(std::move(chunk));
        }
        event_cv.notify_all();
        AcquireNewChunk();
    }
}

void Scheduler::BeginRenderPassImpl(const Framebuffer* framebuffer, VkRenderPass renderpass,
                                    const VkClearValue* clear_values, u32 clear_value_count) {
    const VkFramebuffer framebuffer_handle = framebuffer->Handle();
    const VkExtent2D render_area = framebuffer->RenderArea();
    state.renderpass = renderpass;
    state.framebuffer = framebuffer_handle;
    state.render_area = render_area;

    if (GPU::Logging::IsActive() && Settings::values.gpu_log_vulkan_calls.GetValue()) {
        const std::string render_pass_info =
            fmt::format("renderArea={}x{}, numImages={}", render_area.width, render_area.height,
                        framebuffer->NumImages());
        GPU::Logging::GPULogger::GetInstance().LogRenderPassBegin(render_pass_info);
    }

    std::array<VkClearValue, 9> values{};
    // The Vulkan pointer below owns exactly these nine entries. Never publish
    // the caller's unchecked clearValueCount after clipping the copy.
    const u32 bounded_clear_value_count =
        clear_values ? (std::min)(clear_value_count, static_cast<u32>(values.size())) : 0;
    for (u32 i = 0; i < bounded_clear_value_count; ++i) {
        values[i] = clear_values[i];
    }
    Record([renderpass, framebuffer_handle, render_area, values, bounded_clear_value_count](
               vk::CommandBuffer cmdbuf) {
        const VkRenderPassBeginInfo renderpass_bi{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = renderpass,
            .framebuffer = framebuffer_handle,
            .renderArea =
                {
                    .offset = {.x = 0, .y = 0},
                    .extent = render_area,
                },
            .clearValueCount = bounded_clear_value_count,
            .pClearValues = bounded_clear_value_count != 0 ? values.data() : nullptr,
        };
        cmdbuf.BeginRenderPass(renderpass_bi, VK_SUBPASS_CONTENTS_INLINE);
    });
    num_renderpass_images = framebuffer->NumImages();
    renderpass_images = framebuffer->Images();
    renderpass_image_ranges = framebuffer->ImageRanges();
    framebuffer->MarkResolveShadowsUpToDate();
}

void Scheduler::RealizeDeferredClear() {
    if (deferred_clear.framebuffer == nullptr) {
        return;
    }
    const DeferredClear dc = deferred_clear;
    deferred_clear = {};

    std::array<VkClearValue, 9> clear_values{};
    u32 count = 0;
    const RenderPassKey& base = dc.framebuffer->RenderPassKeyBase();
    for (u32 slot = 0; slot < 8; ++slot) {
        if (base.color_formats[slot] == VideoCore::Surface::PixelFormat::Invalid) {
            continue;
        }
        clear_values[count++] = dc.color_values[slot];
    }
    if (base.depth_format != VideoCore::Surface::PixelFormat::Invalid) {
        clear_values[count++] = dc.depth_stencil_value;
    }
    const u32 color_discard_mask =
        dc.framebuffer->DiscardsMsaaColor() ? dc.color_clear_mask : 0u;
    const bool depth_stencil_discard =
        dc.depth_stencil && dc.framebuffer->DiscardsMsaaDepthStencil();
    const VkRenderPass renderpass = dc.framebuffer->RenderPassVariant(
        dc.color_clear_mask, dc.depth_stencil, color_discard_mask, depth_stencil_discard);
    EndRenderPass();
    BeginRenderPassImpl(dc.framebuffer, renderpass, clear_values.data(), count);
}

bool Scheduler::DeferColorClear(const Framebuffer* framebuffer, u32 rt_slot,
                                const VkClearValue& value) {
    if (IsRenderPassActive()) {
        return false;
    }
    if (deferred_clear.framebuffer != nullptr && deferred_clear.framebuffer != framebuffer) {
        RealizeDeferredClear();
        EndRenderPass();
    }
    deferred_clear.framebuffer = framebuffer;
    deferred_clear.color_clear_mask |= 1u << rt_slot;
    deferred_clear.color_values[rt_slot] = value;
    return true;
}

bool Scheduler::DeferDepthStencilClear(const Framebuffer* framebuffer, const VkClearValue& value) {
    if (IsRenderPassActive()) {
        return false;
    }
    if (deferred_clear.framebuffer != nullptr && deferred_clear.framebuffer != framebuffer) {
        RealizeDeferredClear();
        EndRenderPass();
    }
    deferred_clear.framebuffer = framebuffer;
    deferred_clear.depth_stencil = true;
    deferred_clear.depth_stencil_value = value;
    return true;
}

void Scheduler::FlushDeferredClear() {
    if (deferred_clear.framebuffer == nullptr) {
        return;
    }
    RealizeDeferredClear();
    EndRenderPass();
}

void Scheduler::RequestRenderpass(const Framebuffer* framebuffer) {
    if (deferred_clear.framebuffer == framebuffer) {
        RealizeDeferredClear();
        return;
    }
    const VkRenderPass renderpass = framebuffer->RenderPass();
    const VkFramebuffer framebuffer_handle = framebuffer->Handle();
    const VkExtent2D render_area = framebuffer->RenderArea();
    if (renderpass == state.renderpass && framebuffer_handle == state.framebuffer &&
        render_area.width == state.render_area.width &&
        render_area.height == state.render_area.height) {
        return;
    }
    // Ends any active pass and realizes a deferred clear
    EndRenderPass();
    BeginRenderPassImpl(framebuffer, renderpass, nullptr, 0);
}

void Scheduler::RequestOutsideRenderPassOperationContext() {
    EndRenderPass();
}

bool Scheduler::UpdateGraphicsPipeline(GraphicsPipeline* pipeline, bool optimized) {
    if (state.graphics_pipeline == pipeline) {
        if (pipeline && pipeline->UsesExtendedDynamicState() &&
            state.needs_state_enable_refresh) {
            state_tracker.InvalidateStateEnableFlag();
            state.needs_state_enable_refresh = false;
        }
        // A completed optimized variant shares its cache key and owner pointer.
        // Rebind once after publication even if the same shader keeps drawing.
        const bool variant_changed = state.graphics_pipeline_optimized != optimized;
        state.graphics_pipeline_optimized = optimized;
        return variant_changed;
    }

    state.graphics_pipeline = pipeline;
    state.graphics_pipeline_optimized = optimized;

    if (!pipeline) {
        return true;
    }

    if (!pipeline->UsesExtendedDynamicState()) {
        state.needs_state_enable_refresh = true;
    } else if (state.needs_state_enable_refresh) {
        state_tracker.InvalidateStateEnableFlag();
        state.needs_state_enable_refresh = false;
    }

    return true;
}

bool Scheduler::UpdateRescaling(bool is_rescaling) {
    if (state.rescaling_defined && is_rescaling == state.is_rescaling) {
        return false;
    }
    state.rescaling_defined = true;
    state.is_rescaling = is_rescaling;
    return true;
}

bool Scheduler::UpdateDescriptorBufferChunk(u32 descriptor_chunk) {
    if (state.descriptor_buffer_bound && descriptor_chunk == state.descriptor_buffer_chunk) {
        return false;
    }
    state.descriptor_buffer_bound = true;
    state.descriptor_buffer_chunk = descriptor_chunk;
    return true;
}

void Scheduler::WorkerThread(std::stop_token stop_token) {
    Common::SetCurrentThreadName("VulkanWorker");
    Common::SetCurrentThreadPriority(Common::ThreadPriority::Critical);
    Common::SetCurrentThreadToPerformanceCores();
#ifdef PS5_NATIVE
    ::Eden::Performance::ProbeSchedulerOnce("VulkanWorker");
#endif

    const auto TryPopQueue{[this](auto& work) -> bool {
        if (work_queue.empty()) {
            return false;
        }

        work = std::move(work_queue.front());
        work_queue.pop();
        // The consumer normally has no producer waiting for an empty queue.
        // Avoid broadcasting on every chunk when it cannot wake anyone.
        // queue_mutex synchronizes this check with WaitWorker's registration.
        if (work_queue.empty() && queue_drain_waiters != 0)
            event_cv.notify_all();
        return true;
    }};

    while (!stop_token.stop_requested()) {
        std::unique_ptr<CommandChunk> work;

        {
            std::unique_lock lk{queue_mutex};

            // Wait for work.
            event_cv.wait(lk, stop_token, [&] { return TryPopQueue(work); });

            // If we've been asked to stop, we're done.
            if (stop_token.stop_requested()) {
                return;
            }

            // Exchange lock ownership so that we take the execution lock before
            // the queue lock goes out of scope. This allows us to force execution
            // to complete in the next step.
            void(std::exchange(lk, std::unique_lock{execution_mutex}));

            // Perform the work, tracking whether the chunk was a submission
            // before executing.
#ifdef PS5_NATIVE
            try {
#endif
                const bool has_submit = work->HasSubmit();
                work->ExecuteAll(current_cmdbuf, current_upload_cmdbuf);

                // If the chunk was a submission, reallocate the command buffer.
                if (has_submit) {
                    AllocateWorkerCommandBuffer();
                }
#ifdef PS5_NATIVE
            } catch (...) {
                // Publish failure before releasing execution_mutex. A
                // WaitWorker drain can never mistake this chunk for success.
                ::Eden::RecordGpuFailure(std::current_exception());
                event_cv.notify_all();
                return;
            }
#endif
        }

        {
            std::scoped_lock rl{reserve_mutex};

            // Recycle the chunk back to the reserve.
            chunk_reserve.emplace_back(std::move(work));
        }
    }
}

void Scheduler::AllocateWorkerCommandBuffer() {
    current_cmdbuf = vk::CommandBuffer(command_pool->Commit(), device.GetDispatchLoader());
    current_cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });
    GpuTimeBegin(device, current_cmdbuf);
    current_upload_cmdbuf = vk::CommandBuffer(command_pool->Commit(), device.GetDispatchLoader());
    current_upload_cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });
}

u64 Scheduler::SubmitExecution(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    EndPendingOperations();
    InvalidateState();

    const u64 signal_value = master_semaphore->NextTick();
    RecordWithUploadBuffer([signal_semaphore, wait_semaphore, signal_value,
                            this](vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf) {
        static constexpr VkMemoryBarrier WRITE_BARRIER{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        };
        upload_cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, WRITE_BARRIER);
        upload_cmdbuf.End();
        GpuTimeEnd(cmdbuf);
        cmdbuf.End();

        if (on_submit) {
            on_submit();
        }

        std::scoped_lock lock{submit_mutex};
        switch (const VkResult result = master_semaphore->SubmitQueue(
                    cmdbuf, upload_cmdbuf, signal_semaphore, wait_semaphore, signal_value)) {
        case VK_SUCCESS:
            // Log successful queue submission
            if (GPU::Logging::IsActive() &&
                Settings::values.gpu_log_vulkan_calls.GetValue()) {
                GPU::Logging::GPULogger::GetInstance().LogVulkanCall(
                    "vkQueueSubmit", "", VK_SUCCESS);
            }
            break;
        case VK_ERROR_DEVICE_LOST:
            device.ReportLoss();
            [[fallthrough]];
        default:
            vk::Check(result);
            break;
        }
        GpuTimeCollect(device);
    });
    chunk->MarkSubmit();
    DispatchWork();
    return signal_value;
}

void Scheduler::AllocateNewContext() {
    // Enable counters once again. These are disabled when a command buffer is finished.
}

void Scheduler::InvalidateState() {
    state.graphics_pipeline = nullptr;
    state.graphics_pipeline_optimized = false;
    state.rescaling_defined = false;
    state.descriptor_buffer_bound = false;
    state_tracker.InvalidateCommandBufferState();
}

void Scheduler::EndPendingOperations() {
    query_cache->CounterReset(VideoCommon::QueryType::ZPassPixelCount64);
    EndRenderPass();
}

void Scheduler::EndRenderPass()
    {
        RealizeDeferredClear();
        if (!state.renderpass) {
            return;
        }

        query_cache->CounterClose(VideoCommon::QueryType::StreamingByteCount);

        // Log render pass end
        if (GPU::Logging::IsActive() &&
            Settings::values.gpu_log_vulkan_calls.GetValue()) {
            GPU::Logging::GPULogger::GetInstance().LogRenderPassEnd();
        }

        query_cache->CounterEnable(VideoCommon::QueryType::ZPassPixelCount64, false);
        query_cache->NotifySegment(false);

        Record([num_images = num_renderpass_images,
                       images = renderpass_images,
                       ranges = renderpass_image_ranges,
                       has_transform_feedback = device.IsExtTransformFeedbackSupported()](
                          vk::CommandBuffer cmdbuf) {
            std::array<VkImageMemoryBarrier, 9> barriers;
            for (size_t i = 0; i < num_images; ++i) {
                const VkImageSubresourceRange& range = ranges[i];
                const bool is_color = (range.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0;
                const bool is_depth_stencil = (range.aspectMask
                                              & (VK_IMAGE_ASPECT_DEPTH_BIT
                                                 | VK_IMAGE_ASPECT_STENCIL_BIT)) !=0;

                VkAccessFlags src_access = 0;

                if (is_color)
                    src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                else if (is_depth_stencil)
                    src_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                else
                    src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                  | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                barriers[i] = VkImageMemoryBarrier{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .pNext = nullptr,
                        .srcAccessMask = src_access,
                        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                                         | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                                         | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                         | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                         | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = images[i],
                        .subresourceRange = range,
                };
            }
            cmdbuf.EndRenderPass();
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, vk::PIPELINE_STAGE_GRAPHICS_COMPUTE,
                                   0, nullptr, nullptr, vk::Span(barriers.data(), num_images));
            if (has_transform_feedback) {
                static constexpr VkMemoryBarrier XFB_OUTPUT_BARRIER{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
                    .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                };
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
                                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       0, XFB_OUTPUT_BARRIER);
            }
        });

        state.renderpass = VkRenderPass{};
        num_renderpass_images = 0;
    }


void Scheduler::AcquireNewChunk() {
    std::scoped_lock rl{reserve_mutex};

    if (chunk_reserve.empty()) {
        // If we don't have anything reserved, we need to make a new chunk.
        chunk = std::make_unique<CommandChunk>();
    } else {
        // Otherwise, we can just take from the reserve.
        chunk = std::move(chunk_reserve.back());
        chunk_reserve.pop_back();
    }
}

} // namespace Vulkan
