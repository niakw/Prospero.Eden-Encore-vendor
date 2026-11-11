// SPDX-License-Identifier: GPL-3.0-or-later
// Research-only GPU retirement ownership holder. NOT wired into Vulkan yet.
// A ticket is NOT proof of completion: the consumer MUST supply a monotonic
// completion snapshot from the real GPU timeline semaphore before collection.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

namespace Vulkan::NS2Research {
template <class Owner, std::size_t Capacity = 256>
class GpuRetirementHold final {
    static_assert(Capacity > 0);
    struct Entry {
        Owner owner;
        std::uint64_t submission_tick{};
    };
public:
    GpuRetirementHold() = default;
    GpuRetirementHold(const GpuRetirementHold&) = delete;
    GpuRetirementHold& operator=(const GpuRetirementHold&) = delete;

    // On failure owner remains with the caller: never accidentally destroy an
    // in-flight VkBuffer or staging allocation while falling back.
    [[nodiscard]] bool TryRetain(Owner&& owner, std::uint64_t submission_tick) {
        if (!submission_tick || entries_.size() >= Capacity ||
            (!entries_.empty() && submission_tick < entries_.back().submission_tick))
            return false;
        entries_.emplace_back(Entry{std::move(owner), submission_tick});
        return true;
    }
    // This argument must come from KnownGpuTick()/equivalent real completion,
    // never Scheduler::CurrentTick() or a frame counter.
    std::size_t RetireCompleted(std::uint64_t known_gpu_completed_tick) {
        if (!known_gpu_completed_tick) return 0;
        std::size_t released = 0;
        while (!entries_.empty() &&
               entries_.front().submission_tick <= known_gpu_completed_tick) {
            entries_.pop_front();
            ++released;
        }
        return released;
    }
    [[nodiscard]] std::size_t Outstanding() const noexcept { return entries_.size(); }
private:
    std::deque<Entry> entries_;
};
} // namespace Vulkan::NS2Research
