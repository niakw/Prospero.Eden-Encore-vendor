// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "audio_core/renderer/system.h"

namespace Core {
namespace Timing {
struct EventType;
}
class System;
} // namespace Core

namespace AudioCore::ADSP {
class ADSP;
namespace AudioRenderer {
class AudioRenderer;
} // namespace AudioRenderer
} // namespace AudioCore::ADSP

namespace AudioCore::Renderer {

/**
 * Manages all audio renderers, responsible for triggering command list generation and signalling
 * the ADSP.
 */
class SystemManager {
public:
    explicit SystemManager(Core::System& core);
    ~SystemManager();

    /**
     * Initialize the system manager, called when any system is registered.
     *
     * @return True if successfully initialized, otherwise false.
     */
    void InitializeUnsafe();

    /**
     * Stop the system manager.
     */
    void Stop();

    /**
     * Add an audio render system to the manager.
     * The manager does not own the system, so do not free it without calling Remove.
     *
     * @param system - The system to add.
     * @return True if successfully added, otherwise false.
     */
    bool Add(System& system);

    /**
     * Remove an audio render system from the manager.
     *
     * @param system - The system to remove.
     * @return True if successfully removed, otherwise false.
     */
    bool Remove(System& system);

    /**
     * Execute exactly one manually-driven renderer frame.
     * Manual systems are deliberately absent from the automatic worker list.
     */
    void ExecuteManual(System& system);

private:
    void StartRendererUnsafe();
    void StopRendererUnsafe();
    void StopUnsafe();
    /// Core system
    Core::System& core;
    /// List of pointers to managed systems
    std::list<System*> systems{};
    /// Main worker thread for generating command lists
    std::jthread thread;
    /// Mutex for the systems
    std::mutex mutex1{};
    /// Mutex for adding/removing systems and ADSP renderer lifetime
    std::mutex mutex2{};
    /// Serializes complete SendCommand/Signal/Wait cycles across auto and manual renderers.
    std::mutex render_cycle_mutex{};
    /// Tracks the ADSP renderer lifetime independently from the automatic worker.
    bool renderer_started{};
    /// Is the system manager thread active?
    std::atomic<bool> active{};
    /// Reference to the ADSP's AudioRenderer for communication
    ::AudioCore::ADSP::AudioRenderer::AudioRenderer& audio_renderer;
};

} // namespace AudioCore::Renderer
