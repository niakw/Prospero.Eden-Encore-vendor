namespace VideoCore { class RendererBase; }
namespace Eden { bool LoadingTick(VideoCore::RendererBase&, bool idle); }
#include "gpu_failure.h"
#include "crash_report.h"
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/frontend/graphics_context.h"
#include "video_core/control/scheduler.h"
#include "video_core/dma_pusher.h"
#include "video_core/gpu.h"
#include "video_core/gpu_thread.h"
#include "video_core/host1x/host1x.h"
#include "video_core/renderer_base.h"

namespace VideoCommon::GPUThread {

ThreadManager::ThreadManager(Core::System& system_)
    : system{system_}
{}

ThreadManager::~ThreadManager() { Stop(); }

void ThreadManager::RunWorker(VideoCore::RendererBase& renderer, Core::Frontend::GraphicsContext& context, Tegra::Control::Scheduler& scheduler) {
    const auto stop_token = stop_source.get_token();
        Common::SetCurrentThreadName("GPU");
        Common::SetCurrentThreadPriority(Common::ThreadPriority::Critical);
        Common::SetCurrentThreadToPerformanceCores();
        system.RegisterHostThread();

        auto current_context = context.Acquire();
        unsigned completed_gpu_commands = 0;
        unsigned dispatches = 0;
        CommandDataContainer next;
        while (!stop_token.stop_requested()) {
            if (Eden::LoadingTick(renderer, false)) {
                if (!state.queue.TryPop(next)) {
                    Eden::LoadingTick(renderer, true);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
            } else {
            state.queue.PopWait(next, stop_token);
            }
            if (stop_token.stop_requested()) {
                break;
            }
            if (auto* submit_list = std::get_if<SubmitListCommand>(&next.data)) {
                const auto count = ++dispatches;
                if (count <= 3) LOG_INFO(Render_OpenGL, "EDEN_GPU_DISPATCH_BEGIN count={}", count);
                scheduler.Push(system.GPU(), submit_list->channel, std::move(submit_list->entries));
                if (count <= 3) LOG_INFO(Render_OpenGL, "EDEN_GPU_DISPATCH_END count={}", count);
            } else if (std::holds_alternative<GPUTickCommand>(next.data)) {
                system.GPU().TickWork();
            } else if (const auto* flush = std::get_if<FlushRegionCommand>(&next.data)) {
                renderer.ReadRasterizer()->FlushRegion(flush->addr, flush->size);
            } else if (const auto* invalidate = std::get_if<InvalidateRegionCommand>(&next.data)) {
                renderer.ReadRasterizer()->OnCacheInvalidation(invalidate->addr, invalidate->size);
            } else {
                ASSERT(false);
            }
            // Release publishes completed GPU work to acquire-side fence readers.
            state.signaled_fence.store(next.fence, std::memory_order_release);
            if ((++completed_gpu_commands & 63u) == 0u)
                ::Eden::Crash::gpu_completed_commands.fetch_add(64, std::memory_order_relaxed);
            if (next.block) {
                // We have to lock the write_lock to ensure that the condition_variable wait not get a
                // race between the check and the lock itself.
                std::scoped_lock lk{state.write_lock};
                state.cv.notify_all();
            }
        }
}

void ThreadManager::SubmitList(s32 channel, Tegra::CommandList&& entries, bool is_async) {
    PushCommand(SubmitListCommand(channel, std::move(entries)), false, is_async);
}

void ThreadManager::FlushRegion(DAddr addr, u64 size, bool is_async) {
    if (!is_async) {
        // Always flush with synchronous GPU mode
        PushCommand(FlushRegionCommand(addr, size), false, is_async);
    }
}

void ThreadManager::TickGPU(bool is_async) {
    PushCommand(GPUTickCommand(), false, is_async);
}

void ThreadManager::InvalidateRegion(DAddr addr, u64 size) {
    rasterizer->OnCacheInvalidation(addr, size);
}

void ThreadManager::FlushAndInvalidateRegion(DAddr addr, u64 size, bool is_async) {
    if (Settings::IsGPULevelHigh()) {
        if (!is_async) {
            PushCommand(FlushRegionCommand(addr, size), false, is_async);
        } else {
            auto& gpu = system.GPU();
            const u64 fence = gpu.RequestFlush(addr, size);
            TickGPU(is_async);
            gpu.WaitForSyncOperation(fence);
        }
    }
    rasterizer->OnCacheInvalidation(addr, size);
}

u64 ThreadManager::PushCommand(CommandData&& command_data, bool block, bool is_async) {
    if (!is_async) {
        // In synchronous GPU mode, block the caller until the command has executed
        block = true;
    }

    std::unique_lock lk(state.write_lock);
    const u64 fence{++state.last_fence};
    bool pushed = false;
    if (!stop_source.stop_requested()) {
        pushed = state.queue.TryEmplace(std::move(command_data), fence, block);
        if (!pushed) {
        pushed = state.queue.EmplaceWaitWithStopToken(stop_source.get_token(),
            std::move(command_data), fence, block);
        }
    }
    if (!pushed) {
        if (stop_source.stop_requested()) {
            if (const auto failure = Eden::PeekGpuFailure()) {
                std::rethrow_exception(failure);
            }
        }
        return state.signaled_fence.load(std::memory_order_acquire);
    }

    if (block) {
        const bool completed = state.cv.wait(lk, stop_source.get_token(), [this, fence] {
            return fence <= state.signaled_fence.load(std::memory_order_acquire);
        });
        if (!completed) {
            if (const auto failure = Eden::PeekGpuFailure()) {
                std::rethrow_exception(failure);
            }
            return state.signaled_fence.load(std::memory_order_acquire);
        }
    }

    return fence;
}

} // namespace VideoCommon::GPUThread

// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-only ThreadManager lifecycle; RunWorker retains Eden's worker body.
#include <memory>
#include <cstdio>
#include <system_error>
#include "diagnostics.h"
#include "gpu_failure.h"

namespace VideoCommon::GPUThread {
void ThreadManager::StartThread(VideoCore::RendererBase& renderer,
        Core::Frontend::GraphicsContext& context, Tegra::Control::Scheduler& scheduler) {
    Stop();
    (void)Eden::TakeGpuFailure();
    stop_source = std::stop_source{};
    rasterizer = renderer.ReadRasterizer();
    const auto check = [](int result) {
        if (result) throw std::system_error(result, std::generic_category(), "PS5 GPU worker");
    };
    pthread_attr_t attributes;
    check(pthread_attr_init(&attributes));
    struct DestroyAttributes {
        pthread_attr_t* attributes;
        ~DestroyAttributes() {
            if (pthread_attr_destroy(attributes)) std::terminate();
        }
    } cleanup{&attributes};
    check(pthread_attr_setstacksize(&attributes, 1024 * 1024));
    struct StartData {
        ThreadManager* owner;
        VideoCore::RendererBase& renderer;
        Core::Frontend::GraphicsContext& context;
        Tegra::Control::Scheduler& scheduler;
    };
    auto data = std::make_unique<StartData>(StartData{this, renderer, context, scheduler});
    check(pthread_create(&thread, &attributes, [](void* opaque) noexcept -> void* {
        const std::unique_ptr<StartData> start{static_cast<StartData*>(opaque)};
        try {
            start->owner->RunWorker(start->renderer, start->context, start->scheduler);
        } catch (const std::exception& error) {
            Eden::Report("GPU worker", error.what());
            Eden::RecordGpuFailure(std::current_exception());
            start->owner->stop_source.request_stop();
        } catch (...) {
            Eden::Report("GPU worker", "Unknown exception");
            Eden::RecordGpuFailure(std::current_exception());
            start->owner->stop_source.request_stop();
        }
        return nullptr;
    }, data.get()));
    data.release();
    thread_joinable = true;
    std::puts("EDEN_GPU_WORKER_STACK bytes=1048576");
}

void ThreadManager::Stop() {
    stop_source.request_stop();
    if (thread_joinable) {
        if (pthread_join(thread, nullptr)) std::terminate();
        thread_joinable = false;
    }
    // The worker has exited: it is now safe to discard old queued commands.
    // Otherwise a later StartThread can execute stale work in a new GPU context.
    // The producer lock prevents a concurrent PushCommand from racing the drain.
    std::scoped_lock lock{state.write_lock};
    CommandDataContainer abandoned;
    while (state.queue.TryPop(abandoned)) {
    }
}
} // namespace VideoCommon::GPUThread
