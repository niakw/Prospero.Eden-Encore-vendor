// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <exception>
#include <thread>

namespace Eden::NativeRendererLifetime {
// One native GPU/renderer is bound per running title. Mapping invalidations
// borrow it, but service-owned page tables may outlive it at title shutdown.
inline constexpr std::uint32_t Retired = std::uint32_t{1} << 31;
inline std::atomic<std::uint32_t> state{Retired};

class MappingCallbackLease {
public:
    MappingCallbackLease() noexcept {
        auto current = state.load(std::memory_order_acquire);
        while (!(current & Retired)) {
            if (current == Retired - 1) std::terminate();
            if (state.compare_exchange_weak(current, current + 1,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
                held = true;
                break;
            }
        }
    }
    ~MappingCallbackLease() {
        if (held) state.fetch_sub(1, std::memory_order_release);
    }
    MappingCallbackLease(const MappingCallbackLease&) = delete;
    MappingCallbackLease& operator=(const MappingCallbackLease&) = delete;
    explicit operator bool() const noexcept { return held; }
private:
    bool held{};
};

inline void RetireMappingCallbacks() noexcept {
    state.fetch_or(Retired, std::memory_order_acq_rel);
    // No renderer/cache lock is held here. GPU worker was already joined.
    // Do not destroy the renderer while a mapping invalidation is in flight.
    while (state.load(std::memory_order_acquire) != Retired)
        std::this_thread::yield();
}
inline void ActivateMappingCallbacks() noexcept {
    auto expected = Retired;
    if (!state.compare_exchange_strong(expected, 0, std::memory_order_release,
                                       std::memory_order_relaxed))
        std::terminate();
}
} // namespace Eden::NativeRendererLifetime
