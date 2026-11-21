// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include "common/logging.h"
#include "video_core/host1x/syncpoint_manager.h"

namespace Tegra::Host1x {
SyncpointManager::ActionHandle SyncpointManager::RegisterAction(
    std::atomic<u32>& syncpoint, std::list<RegisteredAction>& action_storage, u32 expected_value,
    std::function<void()>&& action) {
    if (syncpoint.load(std::memory_order_acquire) >= expected_value) {
        action();
        return {};
    }
    {
        std::scoped_lock lk(guard);
        if (syncpoint.load(std::memory_order_relaxed) < expected_value) {
            auto it = action_storage.begin();
            while (it != action_storage.end() && it->expected_value < expected_value) ++it;
            do { ++last_action_id; } while (last_action_id == 0);
            return action_storage.emplace(it, expected_value, last_action_id, std::move(action))->id;
        }
    }
    action();
    return {};
}

void SyncpointManager::DeregisterAction(std::list<RegisteredAction>& action_storage,
                                        const ActionHandle& handle) {
    if (handle == 0) return;
    const auto matches = [&handle](const RegisteredAction& action) { return action.id == handle; };
    std::unique_lock lk(guard);
    if (const auto it = std::ranges::find_if(action_storage, matches); it != action_storage.end()) {
        action_storage.erase(it);
        return;
    }
    while (true) {
        const DueActions* running_in{};
        for (DueActions* due : due_actions) {
            if (const auto it = std::ranges::find_if(due->pending, matches);
                it != due->pending.end()) {
                due->pending.erase(it);
                return;
            }
            if (due->running == handle) running_in = due;
        }
        if (running_in == nullptr || running_in->thread == std::this_thread::get_id()) return;
        action_done_cv.wait(lk);
    }
}

void SyncpointManager::DeregisterGuestAction(u32 id, const ActionHandle& handle) {
    if (id < NUM_MAX_SYNCPOINTS) DeregisterAction(guest_action_storage[id], handle);
}
void SyncpointManager::DeregisterHostAction(u32 id, const ActionHandle& handle) {
    if (id < NUM_MAX_SYNCPOINTS) DeregisterAction(host_action_storage[id], handle);
}
void SyncpointManager::IncrementGuest(u32 id) {
    if (id >= NUM_MAX_SYNCPOINTS) {
        LOG_ERROR(HW_GPU, "Ignoring increment of invalid syncpoint {:#x}", id);
        return;
    }
    Increment(syncpoints_guest[id], wait_guest_cv, guest_action_storage[id], id);
}
void SyncpointManager::SetGuestMirror(GuestMirror mirror) {
    std::scoped_lock lk(guard);
    guest_mirror = std::move(mirror);
}
void SyncpointManager::IncrementHost(u32 id) {
    if (id >= NUM_MAX_SYNCPOINTS) return;
    Increment(syncpoints_host[id], wait_host_cv, host_action_storage[id]);
}
void SyncpointManager::WaitGuest(u32 id, u32 expected) {
    if (id >= NUM_MAX_SYNCPOINTS) return;
    Wait(syncpoints_guest[id], wait_guest_cv, expected);
}
void SyncpointManager::WaitHost(u32 id, u32 expected) {
    if (id >= NUM_MAX_SYNCPOINTS) return;
    Wait(syncpoints_host[id], wait_host_cv, expected);
}

void SyncpointManager::WaitHost(u32 id, u32 expected, std::stop_token stop) {
    if (id >= NUM_MAX_SYNCPOINTS || stop.stop_requested()) return;
    // std::condition_variable cannot wait directly on a stop_token.
    // Register a wakeup for this worker only. The callback takes the SAME
    // mutex as the predicate/wait transition: without it, a stop between
    // predicate evaluation and wait() can be lost and deadlock Shutdown().
    std::stop_callback on_stop(stop, [this] {
        std::scoped_lock lock(guard);
        wait_host_cv.notify_all();
    });
    std::unique_lock lock(guard);
    wait_host_cv.wait(lock, [this, id, expected, &stop] {
        return stop.stop_requested() ||
               syncpoints_host[id].load(std::memory_order_acquire) >= expected;
    });
}

void SyncpointManager::Increment(std::atomic<u32>& syncpoint, std::condition_variable& wait_cv,
                                 std::list<RegisteredAction>& action_storage,
                                 std::optional<u32> mirrored_id) {
    std::unique_lock lk(guard);
    if (mirrored_id && guest_mirror) {
        guest_mirror(*mirrored_id, syncpoint.load(std::memory_order_relaxed) + 1);
    }
    const u32 new_value{syncpoint.fetch_add(1, std::memory_order_acq_rel) + 1};
    wait_cv.notify_all();
    auto end = action_storage.begin();
    while (end != action_storage.end() && end->expected_value <= new_value) ++end;
    if (end == action_storage.begin()) return;

    DueActions due;
    due.pending.splice(due.pending.end(), action_storage, action_storage.begin(), end);
    due_actions.push_back(&due);
    while (!due.pending.empty()) {
        std::function<void()> action{std::move(due.pending.front().action)};
        due.running = due.pending.front().id;
        due.pending.pop_front();
        lk.unlock();
        action();
        action = nullptr;
        lk.lock();
        due.running = 0;
        action_done_cv.notify_all();
    }
    std::erase(due_actions, &due);
}

void SyncpointManager::Wait(std::atomic<u32>& syncpoint, std::condition_variable& wait_cv,
                            u32 expected) {
    const auto pred = [&] { return syncpoint.load(std::memory_order_acquire) >= expected; };
    if (pred()) return;
    std::unique_lock lk(guard);
    wait_cv.wait(lk, pred);
}
} // namespace Tegra::Host1x
