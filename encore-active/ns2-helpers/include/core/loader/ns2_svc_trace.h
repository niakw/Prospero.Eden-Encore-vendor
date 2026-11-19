// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Bounded, opt-in trace of SVC calls to Encore's real NS2 kernel dispatcher.
// The legacy first-32 witness is retained, while a fixed recent ring is kept
// silently and dumped only on a fatal guest prefetch abort.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    // Published only after the selected SVC returns to the real dispatcher.
    // The fatal dumper never treats an entry-only record as a successful SVC.
    std::atomic<std::uint64_t> outcome_sequence{0};
    std::atomic<std::uint64_t> result_x0{0};
    std::atomic<std::uint64_t> value_x1{0};
    std::atomic<std::uint64_t> svc_sp{0};
    std::atomic<std::uint64_t> svc_pc{0};
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
        entry.outcome_sequence.store(0, std::memory_order_release);
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

// Only boot-relevant SVC families are retained without verbose logging.
// This is a bounded in-memory witness, not an IPC trace or a blocking logger.
[[nodiscard]] constexpr bool IsHangBoundarySvc(std::uint32_t imm) noexcept {
    switch (imm) {
    case 0x06: // QueryMemory
    case 0x18: // WaitSynchronization
    case 0x1c: // WaitProcessWideKeyAtomic
    case 0x1f: // ConnectToNamedPort
    case 0x20: // SendSyncRequestLight
    case 0x21: // SendSyncRequest
    case 0x22: // SendSyncRequestWithUserBuffer
    case 0x29: // GetInfo
    case 0x34: // WaitForAddress
    case 0x43: // ReplyAndReceive
    case 0x80: // NS2 GPU-access memory attribute SVC
        return true;
    default:
        return false;
    }
}

inline std::uint64_t RecordRecent(std::uint64_t pid, std::uint32_t imm, bool permitted,
                         const std::array<std::uint64_t, 8>& svc_args) noexcept {
    // Keep the last SVC/IPC/wait boundary even when detailed logs are OFF.
    // This never performs I/O or guest-memory access in the SVC dispatcher.
    if (!IsActive(pid) || (!IsHangBoundarySvc(imm) && !DetailedEnabled()))
        return 0;
    const std::uint64_t seq = recent_sequence.fetch_add(1, std::memory_order_acq_rel) + 1;
    auto& entry = recent[(seq - 1) % RecentSamples];
    // All payload fields are atomic: a second core may wrap this slot after
    // 64 calls. Publish sequence last so a dump can reject a torn generation.
    entry.sequence.store(0, std::memory_order_release);
    entry.outcome_sequence.store(0, std::memory_order_release);
    entry.imm.store(imm, std::memory_order_relaxed);
    entry.permitted.store(permitted ? 1U : 0U, std::memory_order_relaxed);
    for (std::size_t i = 0; i < entry.args.size(); ++i)
        entry.args[i].store(svc_args[i], std::memory_order_relaxed);
    entry.sequence.store(seq, std::memory_order_release);
    return seq;
}

// Finish only the selected SVC calls. Never dereference
// guest pointers or format/log in the SVC hot path. The pair of sequence
// checks rejects a slot that was recycled while the SVC was executing.
inline void RecordOutcome(std::uint64_t pid, std::uint64_t seq,
                          std::uint64_t result, std::uint64_t value,
                          std::uint64_t sp, std::uint64_t pc) noexcept {
    if (!seq || !IsActive(pid)) return;
    auto& entry = recent[(seq - 1) % RecentSamples];
    if (entry.sequence.load(std::memory_order_acquire) != seq) return;
    entry.outcome_sequence.store(0, std::memory_order_release);
    entry.result_x0.store(result, std::memory_order_relaxed);
    entry.value_x1.store(value, std::memory_order_relaxed);
    entry.svc_sp.store(sp, std::memory_order_relaxed);
    entry.svc_pc.store(pc, std::memory_order_relaxed);
    if (entry.sequence.load(std::memory_order_acquire) == seq)
        entry.outcome_sequence.store(seq, std::memory_order_release);
}

// Error-only string snapshot. Called ONLY on an actual guest prefetch fault,
// before the process unload resets its ring. It is *not* a verbose logger.
// The caller saves this short summary through the independent crash-only
// application sink, so stderr=/dev/null cannot erase it.
inline std::size_t FormatCriticalRecent(std::uint64_t pid, char* output,
                                        std::size_t capacity) noexcept {
    if (!output || capacity < 2) return 0;
    output[0] = '\0';
    if (!IsActive(pid)) return 0;
    const std::uint64_t end = recent_sequence.load(std::memory_order_acquire);
    const std::uint64_t first = end > RecentSamples ? end - RecentSamples + 1 : 1;
    std::size_t used = 0;
    unsigned count = 0;
    for (std::uint64_t seq = end; seq >= first && seq != 0 && count < 2; --seq) {
        const auto& entry = recent[(seq - 1) % RecentSamples];
        if (entry.sequence.load(std::memory_order_acquire) != seq) continue;
        const auto imm = entry.imm.load(std::memory_order_relaxed);
        if (!IsHangBoundarySvc(imm)) continue;
        const bool completed = entry.outcome_sequence.load(std::memory_order_acquire) == seq;
        const auto result = entry.result_x0.load(std::memory_order_relaxed);
        const auto value = entry.value_x1.load(std::memory_order_relaxed);
        const auto sp = entry.svc_sp.load(std::memory_order_relaxed);
        const auto pc = entry.svc_pc.load(std::memory_order_relaxed);
        if (entry.sequence.load(std::memory_order_acquire) != seq) continue;
        char part[176];
        const int length = std::snprintf(part, sizeof(part),
            "svc=0x%x %s r=0x%llx v=0x%llx sp=0x%llx pc=0x%llx;",
            imm, completed ? "done" : "pending",
            static_cast<unsigned long long>(completed ? result : 0),
            static_cast<unsigned long long>(completed ? value : 0),
            static_cast<unsigned long long>(completed ? sp : 0),
            static_cast<unsigned long long>(completed ? pc : 0));
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(part) ||
            used + static_cast<std::size_t>(length) >= capacity) break;
        std::memcpy(output + used, part, static_cast<std::size_t>(length));
        used += static_cast<std::size_t>(length);
        output[used] = '\0';
        ++count;
    }
    return used;
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
            "x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx x5=0x%llx",
            static_cast<unsigned long long>(seq), imm, permitted,
            static_cast<unsigned long long>(args[0]),
            static_cast<unsigned long long>(args[1]),
            static_cast<unsigned long long>(args[2]),
            static_cast<unsigned long long>(args[3]),
            static_cast<unsigned long long>(args[4]),
            static_cast<unsigned long long>(args[5]));
        if (IsHangBoundarySvc(imm)) {
            const bool completed = entry.outcome_sequence.load(std::memory_order_acquire) == seq;
            if (completed) {
                const auto result = entry.result_x0.load(std::memory_order_relaxed);
                const auto value = entry.value_x1.load(std::memory_order_relaxed);
                const auto sp = entry.svc_sp.load(std::memory_order_relaxed);
                const auto pc = entry.svc_pc.load(std::memory_order_relaxed);
                if (entry.outcome_sequence.load(std::memory_order_acquire) == seq &&
                    entry.sequence.load(std::memory_order_acquire) == seq) {
                    std::fprintf(stream,
                        " outcome=completed result=0x%llx value=0x%llx svc_sp=0x%llx svc_pc=0x%llx",
                        static_cast<unsigned long long>(result),
                        static_cast<unsigned long long>(value),
                        static_cast<unsigned long long>(sp),
                        static_cast<unsigned long long>(pc));
                } else {
                    std::fprintf(stream, " outcome=pending");
                }
            } else {
                std::fprintf(stream, " outcome=pending");
            }
        }
        std::fputc('\n', stream);
    }
    std::fprintf(stream, "EDEN_NS2_RECENT_SVC_END pid=%llu\n",
                 static_cast<unsigned long long>(pid));
}
} // namespace Eden::NS2::SvcTrace
