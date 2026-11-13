// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <thread>
#include <utility>
#include <queue>

#include "common/alignment.h"
#include "common/common_types.h"
#include "common/polyfill_thread.h"
#include "common/settings.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace VideoCommon {
template <typename Trait>
class QueryCacheBase;
}

namespace Vulkan {

class CommandPool;
class Device;
class Framebuffer;
class GraphicsPipeline;
class StateTracker;

struct QueryCacheParams;

/// The scheduler abstracts command buffer and fence management with an interface that's able to do
/// OpenGL-like operations on Vulkan command buffers.
class Scheduler {
public:
    explicit Scheduler(const Device& device, StateTracker& state_tracker);
    ~Scheduler();

    /// Sends the current execution context to the GPU.
    u64 Flush(VkSemaphore signal_semaphore = {}, VkSemaphore wait_semaphore = {});

    /// Sends the current execution context to the GPU and waits for it to complete.
    void Finish(VkSemaphore signal_semaphore = {}, VkSemaphore wait_semaphore = {});

    // NS2 N72 BEGIN: route sparse page binds through the same submit mutex
    // used by the native Vulkan scheduler's ordinary graphics submissions.
#ifdef EDEN_NS2_SPARSE_QUEUE_BRIDGE
    [[nodiscard]] VkResult BindSparseImage(
        VkImage image, vk::Span<VkSparseImageMemoryBind> tiles,
        vk::Span<VkSparseMemoryBind> tails, VkFence completion,
        VkSemaphore wait = VK_NULL_HANDLE,
        VkSemaphore signal = VK_NULL_HANDLE);
#endif
    // NS2 N72 END


    /// Waits for the worker thread to finish executing everything. After this function returns it's
    /// safe to touch worker resources.
    void WaitWorker();

    /// Sends currently recorded work to the worker thread.
    void DispatchWork();
    // Monotonic native command-stream epoch on the GPU owner thread.
    // A clear-to-draw MSAA candidate is valid only while no other GPU
    // command was recorded. Prevents silently losing transfer/compute writes.
    [[nodiscard]] u64 RecordedCommandSerial() const noexcept {
        return recorded_command_serial;
    }


    /// Requests to begin a renderpass.
    void RequestRenderpass(const Framebuffer* framebuffer);

    // MSAA shadow pass: render to a separate multisampled colour image and
    // resolve to the original 1x guest image when the pass ends. The caller
    // MUST bind a graphics pipeline created with the matching sample count
    // and render-pass attachment topology; this method never changes guest
    // pipeline state and is NOT a general-purpose sample-count override.
    [[nodiscard]] bool RequestNativeColorResolveRenderpass(
        VkRenderPass render_pass, VkFramebuffer framebuffer, VkExtent2D extent,
        VkImage multisample_image, VkImage guest_resolve_image,
        VkClearValue guest_clear_color);
    // End only the native pass that owns the specified framebuffer. Never
    // close an unrelated guest render pass after texture cache invalidation.
    [[nodiscard]] bool FinishNativeColorResolveRenderpass(
        VkRenderPass render_pass, VkFramebuffer framebuffer);
    [[nodiscard]] VkImage NativeColorResolveGuestImage() const noexcept {
        return state.native_color_resolve && num_renderpass_images == 2
                   ? renderpass_images[1] : VK_NULL_HANDLE;
    }
    [[nodiscard]] bool HasNativeColorResolveRenderpass(
        VkRenderPass render_pass) const noexcept {
        return state.native_color_resolve && render_pass &&
               state.renderpass == render_pass;
    }
    [[nodiscard]] bool IsNativeColorResolveActive(
        VkRenderPass render_pass, VkFramebuffer framebuffer) const noexcept {
        return render_pass && framebuffer && state.renderpass == render_pass &&
               state.framebuffer == framebuffer;
    }

    /// Defers a full-attachment color clear so it becomes the next render pass.
    bool DeferColorClear(const Framebuffer* framebuffer, u32 rt_slot, const VkClearValue& value);

    /// Defers a full depth/stencil clear so it becomes the next render pass.
    bool DeferDepthStencilClear(const Framebuffer* framebuffer, const VkClearValue& value);

    /// Realizes any pending deferred clear before its framebuffer can be moved or freed.
    void FlushDeferredClear();

    /// Requests the current execution context to be able to execute operations only allowed outside
    /// of a renderpass.
    void RequestOutsideRenderPassOperationContext();

    /// Returns true when a render pass is currently active in the scheduler state.
    bool IsRenderPassActive() const {
        return state.renderpass != VK_NULL_HANDLE;
    }

    /// Update the pipeline to the current execution context.
    bool UpdateGraphicsPipeline(GraphicsPipeline* pipeline, bool optimized = false);

    /// Update the rescaling state. Returns true if the state has to be updated.
    bool UpdateRescaling(bool is_rescaling);

    /// Returns true when the descriptor buffer chunk has to be bound into the command buffer.
    bool UpdateDescriptorBufferChunk(u32 descriptor_chunk);

    /// Invalidates current command buffer state except for render passes
    void InvalidateState();

    /// Assigns the query cache.
    void SetQueryCache(VideoCommon::QueryCacheBase<QueryCacheParams>& query_cache_) {
        query_cache = &query_cache_;
    }

    // Rasterizer query cache is destroyed before the buffer cache. The latter
    // may issue a final Scheduler::Finish() during its own teardown. Release
    // this borrowed pointer once the rasterizer has drained its last queries.
    void ClearQueryCache() noexcept {
        query_cache = nullptr;
    }

    // Registers a callback to perform on queue submission.
    void RegisterOnSubmit(std::function<void()>&& func) {
        on_submit = std::move(func);
    }

#ifdef PS5_NATIVE
    // A command chunk can fill while Configure() holds both guest GPU-cache
    // locks. Send the previous command batch BEFORE those locks are acquired,
    // without waiting for GPU completion or modifying resource/fence state.
    // The normal Record fallback stays authoritative if a draw is unusually
    // command-heavy (>4 KiB of queued callbacks).
    bool PreflightDrawCommands() {
        constexpr size_t kDrawCommandReserve = 4096;
        if (!chunk || chunk->Empty() || chunk->HasReserve(kDrawCommandReserve))
            return false;
        DispatchWork();
        return true;
    }
#endif

    /// Send work to a separate thread.
    template <typename T>
        requires std::is_invocable_v<T, vk::CommandBuffer, vk::CommandBuffer>
    void RecordWithUploadBuffer(T&& command) {
#ifdef PS5_NATIVE
        // ConfigureDraw/Configure can memoize pipeline, rescaling and
        // descriptor bindings before enqueueing. If a chunk record or
        // rollover throws, those bindings were never accepted by the FIFO.
        // Invalidate ONLY the owner's cached command state; the current
        // renderpass and already-queued commands are left intact.
        try {
#endif
        ++recorded_command_serial;
        if (chunk->Record(command)) {
            return;
        }
        DispatchWork();
#ifdef PS5_NATIVE
        // Even after rollover the new chunk can refuse the command.
        // Never treat a missing draw/descriptor upload as successfully queued.
        if (!chunk->Record(command)) {
            throw std::bad_alloc{};
        }
#else
        (void)chunk->Record(command);
#endif
#ifdef PS5_NATIVE
        } catch (...) {
            InvalidateState();
            throw;
        }
#endif
    }

    template <typename T>
        requires std::is_invocable_v<T, vk::CommandBuffer>
    void Record(T&& c) {
        this->RecordWithUploadBuffer(
            [command = std::move(c)](vk::CommandBuffer cmdbuf, vk::CommandBuffer) {
                command(cmdbuf);
            });
    }

#if defined(PS5_NATIVE) && defined(EDEN_NS2_STAGE_BACKING)
    // An owner-thread observation of the *recording* context. This is NOT
    // a queue-generation identity, channel epoch, or GPU-lifetime lease.
    struct R2RecordingSnapshot {
        u64 submission_tick{};
        u64 command_serial{};
        u64 dispatched_chunk_generation{};
        VkRenderPass active_renderpass{};
        const Framebuffer* deferred_clear_owner{};
    };
    [[nodiscard]] R2RecordingSnapshot SnapshotR2Recording() const noexcept {
        return {CurrentTick(), recorded_command_serial, r2_dispatched_chunks,
                state.renderpass, deferred_clear.framebuffer};
    }
    [[nodiscard]] bool R2RecordingStillCurrent(const R2RecordingSnapshot& snapshot) const noexcept {
        return snapshot.submission_tick == CurrentTick() &&
               snapshot.command_serial == recorded_command_serial &&
               snapshot.dispatched_chunk_generation == r2_dispatched_chunks &&
               snapshot.active_renderpass == state.renderpass &&
               snapshot.deferred_clear_owner == deferred_clear.framebuffer;
    }
#endif

    /// Returns the current command buffer tick.
    [[nodiscard]] u64 CurrentTick() const noexcept {
        return master_semaphore->CurrentTick();
    }

    /// Returns true when a tick has been triggered by the GPU.
    [[nodiscard]] bool IsFree(u64 tick) const noexcept {
        return master_semaphore->IsFree(tick);
    }

    /// Waits for the given GPU tick, optionally pacing frames.
    void Wait(u64 tick, double target_fps = 0.0) {
        if (tick > 0) {
            if (tick >= master_semaphore->CurrentTick()) {
                Flush();
            }
            master_semaphore->Wait(tick);
        }
        if (Settings::values.use_speed_limit.GetValue() && target_fps > 0.0) {
            auto now = std::chrono::steady_clock::now();
            if (last_target_fps != target_fps) {
                frame_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / target_fps));
                max_frame_count = static_cast<int>(0.1 * target_fps);
                last_target_fps = target_fps;
                frame_counter = 0;
                start_time = now;
            }
            frame_counter++;
            auto target_time = start_time + frame_interval * frame_counter;
            if (target_time >= now) {
                constexpr auto spin_tail = std::chrono::milliseconds(1);
                auto sleep_time = target_time - now;
                if (sleep_time > spin_tail * 2) {
                    std::this_thread::sleep_for(sleep_time - spin_tail);
                }
                while (std::chrono::steady_clock::now() < target_time) {
                    std::this_thread::yield();
                }
            } else if (frame_counter > max_frame_count) {
                frame_counter = 0;
                start_time = now;
            }
        }
    }

    /// Returns the master timeline semaphore.
    [[nodiscard]] MasterSemaphore& GetMasterSemaphore() const noexcept {
        return *master_semaphore;
    }

    std::mutex submit_mutex;

private:
    class Command {
    public:
        virtual ~Command() = default;

        virtual void Execute(vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf) const = 0;

        Command* GetNext() const {
            return next;
        }

        void SetNext(Command* next_) {
            next = next_;
        }

    private:
        Command* next = nullptr;
    };

    template <typename T>
    class TypedCommand final : public Command {
    public:
        explicit TypedCommand(T&& command_) : command{std::move(command_)} {}
        ~TypedCommand() override = default;

        TypedCommand(TypedCommand&&) = delete;
        TypedCommand& operator=(TypedCommand&&) = delete;

        void Execute(vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf) const override {

            command(cmdbuf, upload_cmdbuf);
        }

    private:
        T command;
    };

    class CommandChunk final {
    public:
        void ExecuteAll(vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf);

        template <typename T>
        bool Record(T& command) {
            using FuncType = TypedCommand<T>;
            static_assert(sizeof(FuncType) < sizeof(data), "Lambda is too large");

            command_offset = Common::AlignUp(command_offset, alignof(FuncType));
            if (command_offset > sizeof(data) - sizeof(FuncType)) {
                return false;
            }
            Command* const current_last = last;
            last = new (data.data() + command_offset) FuncType(std::move(command));

            if (current_last) {
                current_last->SetNext(last);
            } else {
                first = last;
            }
            command_offset += sizeof(FuncType);
            return true;
        }

        void MarkSubmit() {
            submit = true;
        }

        bool Empty() const {
            return command_offset == 0;
        }

#ifdef PS5_NATIVE
        bool HasReserve(size_t bytes) const noexcept {
            // Allow for maximum command alignment after the current offset.
            const size_t needed = bytes + alignof(std::max_align_t);
            return needed <= data.size() && command_offset <= data.size() - needed;
        }
#endif

        bool HasSubmit() const {
            return submit;
        }

    private:
        Command* first = nullptr;
        Command* last = nullptr;

        size_t command_offset = 0;
        bool submit = false;
        alignas(std::max_align_t) std::array<u8, 0x8000> data{};
    };

    struct State {
        VkRenderPass renderpass{};
        VkFramebuffer framebuffer{};
        VkExtent2D render_area = {0, 0};
        bool native_color_resolve{};
        GraphicsPipeline* graphics_pipeline = nullptr;
        // GPU owner only: the optimized variant can become ready without
        // changing the logical pipeline pointer.
        bool graphics_pipeline_optimized = false;
        bool is_rescaling = false;
        bool rescaling_defined = false;
        bool needs_state_enable_refresh = false;
        u32 descriptor_buffer_chunk = 0;
        bool descriptor_buffer_bound = false;
    };

    struct DeferredClear {
        const Framebuffer* framebuffer = nullptr;
        u32 color_clear_mask = 0;
        std::array<VkClearValue, 8> color_values{};
        bool depth_stencil = false;
        VkClearValue depth_stencil_value{};
    };

    /// Begins a render pass for the given framebuffer, optionally with clear values.
    void BeginRenderPassImpl(const Framebuffer* framebuffer, VkRenderPass renderpass,
                             const VkClearValue* clear_values, u32 clear_value_count);

    /// If a deferred clear is pending.
    void RealizeDeferredClear();

    void WorkerThread(std::stop_token stop_token);

    void AllocateWorkerCommandBuffer();

    u64 SubmitExecution(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore);

    void AllocateNewContext();

    void EndPendingOperations();

    void EndRenderPass();

    void AcquireNewChunk();

    const Device& device;
    StateTracker& state_tracker;

    std::unique_ptr<MasterSemaphore> master_semaphore;
    std::unique_ptr<CommandPool> command_pool;

    VideoCommon::QueryCacheBase<QueryCacheParams>* query_cache = nullptr;

    vk::CommandBuffer current_cmdbuf;
    vk::CommandBuffer current_upload_cmdbuf;

    DeferredClear deferred_clear;

    std::unique_ptr<CommandChunk> chunk;
    std::function<void()> on_submit;

    State state;
    u64 recorded_command_serial{};
#if defined(PS5_NATIVE) && defined(EDEN_NS2_STAGE_BACKING)
    // Producer-thread command chunk rollover identity (NOT a GPU queue fence).
    u64 r2_dispatched_chunks{};
#endif

    u32 num_renderpass_images = 0;
    std::array<VkImage, 9> renderpass_images{};
    std::array<VkImageSubresourceRange, 9> renderpass_image_ranges{};

    std::queue<std::unique_ptr<CommandChunk>> work_queue;
    std::vector<std::unique_ptr<CommandChunk>> chunk_reserve;
    std::mutex execution_mutex;
    std::mutex reserve_mutex;
    std::mutex queue_mutex;
    std::condition_variable_any event_cv;
    // Protected by queue_mutex. Only a draining WaitWorker needs a wakeup
    // when the Vulkan worker consumes a queued command chunk.
    unsigned queue_drain_waiters{};
    // Protected by queue_mutex. The worker is executing a chunk (and thus
    // cannot be sleeping on event_cv) until its next queue acquisition.
    bool worker_in_flight{};
    std::jthread worker_thread;

    std::chrono::steady_clock::duration frame_interval{};
    std::chrono::steady_clock::time_point start_time{};
    double last_target_fps{};
    u64 max_frame_count{};
    u64 frame_counter{};
};

} // namespace Vulkan
