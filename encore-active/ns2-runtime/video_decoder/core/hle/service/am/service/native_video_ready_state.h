// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <mutex>
#include <utility>

namespace Service::AM::VideoDecoderProtocol {
// Shared lifetime control for an auto-clearing input-ready event. The
// CoreTiming callback captures this shared state, never a session pointer.
// The event may be closed only after Detach and UnscheduleEvent complete.
class NativeVideoReadyState {
public:
    void Attach(std::function<void()> pulse) {
        std::scoped_lock lock{m_mutex};
        m_pulse = std::move(pulse);
    }
    void Set(bool running, bool room) {
        std::scoped_lock lock{m_mutex};
        m_running = running;
        m_room = room;
    }
    void SetRoom(bool room) {
        std::scoped_lock lock{m_mutex};
        m_room = room;
    }
    void Notify() {
        std::scoped_lock lock{m_mutex};
        if (m_running && m_room && m_pulse) m_pulse();
    }
    void Detach() {
        std::scoped_lock lock{m_mutex};
        m_running = false;
        m_room = false;
        m_pulse = {};
    }
private:
    std::mutex m_mutex;
    bool m_running{};
    bool m_room{};
    std::function<void()> m_pulse;
};
} // namespace Service::AM::VideoDecoderProtocol
