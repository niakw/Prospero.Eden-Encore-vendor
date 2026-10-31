// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N3v: bounded, opt-in trace of SVC calls to Encore's *real* kernel dispatcher.
// Registration occurs only after NS2 ExeFS has created a real KProcess.
// No synthetic service results, guest intervention or change to SVC handlers.
#include <atomic>
#include <cstdint>
#include <optional>

namespace Eden::NS2::SvcTrace {
constexpr std::uint32_t MaxSamples = 32;
// One active experimental process; subsequent loads replace this identity.
// PID is monotonically assigned by the kernel during a run; zero is invalid.
inline std::atomic<std::uint64_t> active_pid{0};
inline std::atomic<std::uint32_t> observations{0};

inline void Register(std::uint64_t pid) noexcept {
    observations.store(0, std::memory_order_relaxed);
    active_pid.store(pid, std::memory_order_release);
}


// Clear only the failing process: never discard a newer registered PID.
inline void Unregister(std::uint64_t pid) noexcept {
    if (!pid) return;
    active_pid.compare_exchange_strong(pid, 0, std::memory_order_acq_rel,
                                       std::memory_order_relaxed);
}

[[nodiscard]] inline std::optional<std::uint32_t> Next(std::uint64_t pid) noexcept {
    if (pid == 0 || active_pid.load(std::memory_order_acquire) != pid)
        return std::nullopt;
    std::uint32_t current = observations.load(std::memory_order_relaxed);
    while (current < MaxSamples) {
        if (observations.compare_exchange_weak(current, current + 1,
                                               std::memory_order_acq_rel,
                                               std::memory_order_relaxed)) {
            return current + 1;
        }
    }
    return std::nullopt;
}
} // namespace Eden::NS2::SvcTrace
