// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>

#include "audio_core/adsp/adsp.h"
#include "audio_core/audio_core.h"
#include "audio_core/renderer/system_manager.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/core_timing.h"

namespace AudioCore::Renderer {

SystemManager::SystemManager(Core::System& core_)
    : core{core_}, audio_renderer{core.AudioCore().ADSP().AudioRenderer()} {}

SystemManager::~SystemManager() {
    Stop();
}

void SystemManager::StartRendererUnsafe() {
    if (!renderer_started) {
        audio_renderer.Start();
        renderer_started = true;
    }
}

void SystemManager::StopRendererUnsafe() {
    if (renderer_started) {
        audio_renderer.Stop();
        renderer_started = false;
    }
}

void SystemManager::InitializeUnsafe() {
    if (!active) {
        StartRendererUnsafe();
        active = true;
        thread = std::jthread([this](std::stop_token stop_token) {
            Common::SetCurrentThreadName("AudioRenderSystemManager");
            Common::SetCurrentThreadPriority(Common::ThreadPriority::High);
            while (active && !stop_token.stop_requested()) {
                std::scoped_lock render_cycle{render_cycle_mutex};
                {
                    std::scoped_lock l{mutex1};
                    for (auto system : systems) {
                        system->SendCommandToDsp();
                    }
                }
                audio_renderer.Signal();
                audio_renderer.Wait();
            }
        });
    }
}

void SystemManager::StopUnsafe() {
    if (active) {
        active = false;
        thread.request_stop();
        thread.join();
    }
    StopRendererUnsafe();
}

void SystemManager::Stop() {
    std::scoped_lock l2{mutex2};
    StopUnsafe();
}

void SystemManager::ExecuteManual(System& system) {
    // Block automatic renderer lifetime transitions while this explicit frame is in flight.
    std::scoped_lock l2{mutex2};
    StartRendererUnsafe();

    std::scoped_lock render_cycle{render_cycle_mutex};
    system.SendCommandToDsp();
    audio_renderer.Signal();
    audio_renderer.Wait();
}

bool SystemManager::Add(System& system_) {
    std::scoped_lock l2{mutex2};
    if (systems.size() + 1 > MaxRendererSessions) {
        LOG_ERROR(Service_Audio, "Maximum AudioRenderer Systems active, cannot add more!");
        return false;
    }

    bool start_worker{};
    {
        std::scoped_lock l{mutex1};
        start_worker = systems.empty();
        systems.push_back(&system_);
    }
    if (start_worker) {
        InitializeUnsafe();
    }
    return true;
}

bool SystemManager::Remove(System& system_) {
    std::scoped_lock l2{mutex2};
    {
        std::scoped_lock l{mutex1};
        if (systems.remove(&system_) == 0) {
            LOG_ERROR(Service_Audio, "Failed to remove a render system, it was not found in the list!");
            return false;
        }
    }

    if (systems.empty()) {
        StopUnsafe();
    }
    return true;
}

} // namespace AudioCore::Renderer
