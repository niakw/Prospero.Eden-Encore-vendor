// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/host1x/syncpoint_manager.h"

namespace Tegra {

namespace Host1x {

namespace {
// Each domain/id serializes cancellation against callback completion.
// Recursive: a running callback may cancel another pending action on its id.
// The shared action-list guard is never held while a callback executes.
std::array<std::recursive_mutex, 192>& GuestDispatchLocks() {
    static std::array<std::recursive_mutex, 192> locks{};
    return locks;
}

std::array<std::recursive_mutex, 192>& HostDispatchLocks() {
    static std::array<std::recursive_mutex, 192> locks{};
    return locks;
}
} // namespace

SyncpointManager::ActionHandle SyncpointManager::RegisterAction(
    std::atomic<u32>& syncpoint, std::list<RegisteredAction>& action_storage, u32 expected_value,
    std::function<void()>&& action) {
    if (syncpoint.load(std::memory_order_acquire) >= expected_value) {
        action();
        return {};
    }

    std::unique_lock lk(guard);
    if (syncpoint.load(std::memory_order_relaxed) >= expected_value) {
        // The action can register or cancel other Host1x work.
        // Release the shared lock before invoking an already-ready callback.
        lk.unlock();
        action();
        return {};
    }
    auto it = action_storage.begin();
    while (it != action_storage.end()) {
        if (it->expected_value >= expected_value) {
            break;
        }
        ++it;
    }
    return action_storage.emplace(it, expected_value, std::move(action));
}

void SyncpointManager::DeregisterAction(std::list<RegisteredAction>& action_storage,
                                        const ActionHandle& handle) {
    std::scoped_lock lk(guard);

    // We want to ensure the iterator still exists prior to erasing it
    // Otherwise, if an invalid iterator was passed in then it could lead to UB
    // It is important to avoid UB in that case since the deregister isn't called from a locked
    // context
    for (auto it = action_storage.begin(); it != action_storage.end(); it++) {
        if (it == handle) {
            action_storage.erase(it);
            return;
        }
    }
}

void SyncpointManager::DeregisterGuestAction(u32 syncpoint_id, const ActionHandle& handle) {
    // Return only after an action already claimed on this id has finished.
    // nvhost event teardown relies on this rendezvous for callback lifetime.
    std::scoped_lock dispatch_lock{GuestDispatchLocks()[syncpoint_id]};
    DeregisterAction(guest_action_storage[syncpoint_id], handle);
}

void SyncpointManager::DeregisterHostAction(u32 syncpoint_id, const ActionHandle& handle) {
    std::scoped_lock dispatch_lock{HostDispatchLocks()[syncpoint_id]};
    DeregisterAction(host_action_storage[syncpoint_id], handle);
}

void SyncpointManager::IncrementGuest(u32 syncpoint_id) {
    // The old global guard serialized callbacks for this id. Keep their
    // submission order without blocking independent guest syncpoints.
    std::scoped_lock dispatch_lock{GuestDispatchLocks()[syncpoint_id]};
    Increment(syncpoints_guest[syncpoint_id], wait_guest_cv, guest_action_storage[syncpoint_id]);
}

void SyncpointManager::IncrementHost(u32 syncpoint_id) {
    std::scoped_lock dispatch_lock{HostDispatchLocks()[syncpoint_id]};
    Increment(syncpoints_host[syncpoint_id], wait_host_cv, host_action_storage[syncpoint_id]);
}

void SyncpointManager::WaitGuest(u32 syncpoint_id, u32 expected_value) {
    Wait(syncpoints_guest[syncpoint_id], wait_guest_cv, expected_value);
}

void SyncpointManager::WaitHost(u32 syncpoint_id, u32 expected_value) {
    Wait(syncpoints_host[syncpoint_id], wait_host_cv, expected_value);
}

void SyncpointManager::Increment(std::atomic<u32>& syncpoint, std::condition_variable& wait_cv,
                                 std::list<RegisteredAction>& action_storage) {
    auto new_value{syncpoint.fetch_add(1, std::memory_order_acq_rel) + 1};

    // Claim ready callbacks atomically against deregistration without
    // allocating or executing arbitrary callbacks under the Host1x guard.
    std::list<RegisteredAction> ready_actions;
    {
        std::scoped_lock lk(guard);
        auto it = action_storage.begin();
        while (it != action_storage.end() && it->expected_value <= new_value) {
            ++it;
        }
        ready_actions.splice(ready_actions.end(), action_storage,
                             action_storage.begin(), it);
    }
    // Cancellation can prevent an action only before the claim above.
    // Execute the already-claimed batch without holding the global guard.
    for (auto& ready : ready_actions) {
        ready.action();
    }
    // Preserve the original contract: guest/host waiters are notified
    // only after all due callbacks have completed, never before dispatch.
    wait_cv.notify_all();
}

void SyncpointManager::Wait(std::atomic<u32>& syncpoint, std::condition_variable& wait_cv,
                            u32 expected_value) {
    const auto pred = [&]() { return syncpoint.load(std::memory_order_acquire) >= expected_value; };
    if (pred()) {
        return;
    }

    std::unique_lock lk(guard);
    wait_cv.wait(lk, pred);
}

} // namespace Host1x

} // namespace Tegra
