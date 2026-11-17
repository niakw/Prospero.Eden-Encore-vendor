// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Bounded, opt-in trace of SVC calls to Encore's real NS2 kernel dispatcher.
// The legacy first-32 witness is retained, while a fixed recent ring is kept
// silently and dumped only on a fatal guest prefetch abort.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>

#ifdef PS5_NATIVE
extern "C" bool eden_native_detailed_logging() noexcept __attribute__((weak));
#endif

namespace Eden::NS2::SvcTrace {
constexpr std::uint32_t MaxSamples = 32;
constexpr std::uint32_t RecentSamples = 64;

inline std::atomic<std::uint64_t> active_pid{0};
inline std::atomic<std::uint32_t> observations{0};
inline std::atomic<std::uint64_t> recent_sequence{0};

struct RecentEntry {
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint32_t> imm{0};
    std::atomic<std::uint32_t> permitted{0};
    std::array<std::atomic<std::uint64_t>, 6> args{};
};
inline std::array<RecentEntry, RecentSamples> recent{};

[[nodiscard]] inline bool DetailedEnabled() noexcept {
#ifdef PS5_NATIVE
    return eden_native_detailed_logging && eden_native_detailed_logging();
#else
    return true;
#endif
}

inline void ClearRecent() noexcept {
    recent_sequence.store(0, std::memory_order_release);
    for (auto& entry : recent) {
        entry.sequence.store(0, std::memory_order_release);
    }
}

inline void Register(std::uint64_t pid) noexcept {
    observations.store(0, std::memory_order_relaxed);
    ClearRecent();
    active_pid.store(pid, std::memory_order_release);
}

inline void ResetForTitle() noexcept {
    active_pid.store(0, std::memory_order_release);
    observations.store(0, std::memory_order_relaxed);
    ClearRecent();
}

inline void Unregister(std::uint64_t pid) noexcept {
    if (!pid) return;
    active_pid.compare_exchange_strong(pid, 0, std::memory_order_acq_rel,
                                       std::memory_order_relaxed);
}

[[nodiscard]] inline bool IsActive(std::uint64_t pid) noexcept {
    return pid != 0 && active_pid.load(std::memory_order_acquire) == pid;
}

[[nodiscard]] inline std::optional<std::uint32_t> Next(std::uint64_t pid) noexcept {
    if (!IsActive(pid) || !DetailedEnabled())
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

inline void RecordRecent(std::uint64_t pid, std::uint32_t imm, bool permitted,
                         const std::array<std::uint64_t, 8>& svc_args) noexcept {
    if (!IsActive(pid) || !DetailedEnabled())
        return;
    const std::uint64_t seq = recent_sequence.fetch_add(1, std::memory_order_acq_rel) + 1;
    auto& entry = recent[(seq - 1) % RecentSamples];
    // All payload fields are atomic: a second core may wrap this slot after
    // 64 calls. Publish sequence last so a dump can reject a torn generation.
    entry.sequence.store(0, std::memory_order_release);
    entry.imm.store(imm, std::memory_order_relaxed);
    entry.permitted.store(permitted ? 1U : 0U, std::memory_order_relaxed);
    for (std::size_t i = 0; i < entry.args.size(); ++i)
        entry.args[i].store(svc_args[i], std::memory_order_relaxed);
    entry.sequence.store(seq, std::memory_order_release);
}

inline void DumpRecent(std::uint64_t pid, std::FILE* stream = stderr) noexcept {
    if (!stream || !IsActive(pid) || !DetailedEnabled())
        return;
    const std::uint64_t end = recent_sequence.load(std::memory_order_acquire);
    const std::uint64_t begin = end > RecentSamples ? end - RecentSamples + 1 : 1;
    std::fprintf(stream, "EDEN_NS2_RECENT_SVC_BEGIN pid=%llu first=%llu last=%llu capacity=%u\n",
                 static_cast<unsigned long long>(pid),
                 static_cast<unsigned long long>(begin),
                 static_cast<unsigned long long>(end), RecentSamples);
    for (std::uint64_t seq = begin; seq <= end; ++seq) {
        const auto& entry = recent[(seq - 1) % RecentSamples];
        if (entry.sequence.load(std::memory_order_acquire) != seq)
            continue;
        const auto imm = entry.imm.load(std::memory_order_relaxed);
        const auto permitted = entry.permitted.load(std::memory_order_relaxed);
        std::array<std::uint64_t, 6> args{};
        for (std::size_t i = 0; i < args.size(); ++i)
            args[i] = entry.args[i].load(std::memory_order_relaxed);
        if (entry.sequence.load(std::memory_order_acquire) != seq)
            continue;
        std::fprintf(stream,
            "EDEN_NS2_RECENT_SVC seq=%llu imm=0x%x permitted=%u "
            "x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx x5=0x%llx\n",
            static_cast<unsigned long long>(seq), imm, permitted,
            static_cast<unsigned long long>(args[0]),
            static_cast<unsigned long long>(args[1]),
            static_cast<unsigned long long>(args[2]),
            static_cast<unsigned long long>(args[3]),
            static_cast<unsigned long long>(args[4]),
            static_cast<unsigned long long>(args[5]));
    }
    std::fprintf(stream, "EDEN_NS2_RECENT_SVC_END pid=%llu\n",
                 static_cast<unsigned long long>(pid));
}
} // namespace Eden::NS2::SvcTrace
