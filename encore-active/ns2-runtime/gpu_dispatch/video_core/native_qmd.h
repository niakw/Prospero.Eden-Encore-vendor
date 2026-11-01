// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <limits>
#include <mutex>

namespace Tegra {

// Ampere QMD 3.0 dependency scheduling, adapted from Eglantine NX2.
// Research component only. No legacy Maxwell compute dispatch is implied.
// A scheduler instance is owned by an authenticated GPU process epoch; it
// must not share static state across guest titles or reused channel FDs.
class NativeQmdScheduler {
public:
    using Word = std::uint32_t;
    using Address = std::uint64_t;
    using Descriptor = std::array<Word, 64>;
    struct RejectShaderDispatch { bool operator()(const Descriptor&) const { return false; } };
    // Optional diagnostics: called with "load"/"count"/"defer"/"run" events.
    std::function<void(std::string_view, Address, std::int32_t)> trace;
    template <typename Read, typename Release>
    std::string_view Signal(Address address, Word argument, Read read, Release release) {
        return Signal(address, argument, read, release, RejectShaderDispatch{});
    }
    template <typename Read, typename Release, typename Dispatch>
    std::string_view Signal(Address address, Word argument, Read read, Release release, Dispatch dispatch) {
        struct Pending { Address address; Word action; Word count; };
        std::string_view error;
        std::array<Pending, 256> pending{};
        std::size_t count = 1;
        pending[0] = {address, argument & 15, ((argument >> 14) & 0x3ff) + 1};
        for (std::size_t i = 0; i < count; ++i) {
            const auto request = pending[i];
            if (states.size() >= 4096 && !states.contains(request.address)) {
                // Finished QMDs with nothing deferred are only history, drop them instead of stalling.
                std::erase_if(states, [](const auto& entry) {
                    return entry.second.completed && entry.second.deferred == 0;
                });
                if (states.size() >= 4096) return "QMD cache limit";
            }
            auto& state = states[request.address];
            if (request.action == 3 || request.action == 2) {
                if (request.action == 3 || !state.loaded) {
                    // A caller may retry a failed release with action 2. A
                    // fresh action 3 must not reset accepted semaphore slots
                    // and re-execute a shader from an unfinished transaction.
                    if (request.action == 3 && state.dispatched && !state.completed)
                        return "QMD reload during pending release";
                    if (!read(request.address, state.descriptor)) return "unmapped QMD";
                    const auto& qmd = state.descriptor;
                    if ((qmd[18] & 0xff) != 0x30 || (qmd[4] & 0x100)) return "unsupported QMD version or queue";
                    if constexpr (std::is_same_v<Dispatch, RejectShaderDispatch>) {
                        if (qmd[12] && (qmd[13] & 0xffff) && (qmd[14] & 0xffff))
                            return "native shader dispatch required";
                    }
                    state.dependencies = std::int32_t((qmd[4] >> 14) & 0xffff) + state.deferred;
                    if (trace) trace("base", request.address, std::int32_t((qmd[4] >> 14) & 0xffff));
                    if (trace) trace("load", request.address, state.dependencies);
                    state.deferred = 0;
                    state.loaded = true;
                    state.completed = false;
                    // A fresh launch/reload starts a new completion transaction.
                    state.dispatched = false;
                    state.released_slots = 0;
                }
                state.scheduled = true;
            } else if (request.action == 7 || request.action == 12) {
                const auto delta = request.action == 7 ? -std::int32_t(request.count) : std::int32_t(request.count);
                if (!state.loaded || state.completed) {
                    state.deferred += delta;
                    if (trace) trace("defer", request.address, state.deferred);
                    continue;
                }
                state.dependencies += delta;
                if (trace) trace("count", request.address, state.dependencies);
            } else if (request.action == 0 || request.action == 1) {
                continue;
            } else {
                return "unsupported PCAS action";
            }
            if (!state.loaded || !state.scheduled || state.completed || state.dependencies > 0) continue;
            if (state.dependencies < 0) return "QMD dependency underflow";
            // The SDK patches release payloads while a scheduled QMD is
            // waiting for its remaining dependencies. Fetch them at execution.
            if (!read(request.address, state.descriptor)) return "unmapped QMD";
            const auto qmd = state.descriptor;
            if ((qmd[18] & 0xff) != 0x30 || (qmd[4] & 0x100)) return "unsupported QMD version or queue";
            // Validate the complete dependent QMD structure before a shader
            // launch or guest-visible release. A late error must never leave
            // semaphores published while silently dropping the dependency.
            if (qmd[16] & (1U << 5)) return "second dependent QMD unsupported";
            const bool has_dependent = (qmd[16] & 1U) != 0;
            const Word dependent_action = (qmd[16] >> 1) & 7U;
            if (has_dependent &&
                (dependent_action != 4 && dependent_action != 3 && dependent_action != 1))
                return "unsupported dependent QMD action";
            if (has_dependent && count == pending.size()) return "QMD chain limit";
            // Validate the release slots before making any visible.
            for (const Word base : ReleaseSlots) {
                const Word flags = qmd[base + 1];
                if (!(flags & (1U << 23))) continue;
                const bool add = (flags & (1U << 26)) != 0;
                if ((flags & (0x3bU << 24)) || (flags >> 30) != 1 ||
                    (add && (flags & (7U << 20)))) return "unsupported QMD release format";
                if (add) {
                    if constexpr (!std::is_invocable_r_v<bool, Release, Address, Word, Word>)
                        return "unsupported QMD reduction callback";
                }
            }
            // Never forge completion of a native shader which did not
            // execute. Upstream currently releases despite a rejected shader
            // to advance guest queues; that masks incorrect rendering and
            // can let the guest reuse memory while GPU work never happened.
            // Keep this QMD pending for a future genuine dispatch backend.
            // A failed semaphore delivery must not mark the QMD complete.
            // On retry, never launch the same GPU work or queue an already
            // accepted release a second time (especially atomic reductions).
            const bool dispatched = state.dispatched ||
                !(qmd[12] && (qmd[13] & 0xffff) && (qmd[14] & 0xffff)) || dispatch(qmd);
            if (trace) trace("run", request.address, dispatched ? 1 : 0);
            if (!dispatched) return "native shader dispatch required";
            state.dispatched = true;
            for (std::size_t slot = 0; slot < ReleaseSlots.size(); ++slot) {
                const Word base = ReleaseSlots[slot];
                const Word flags = qmd[base + 1];
                if (!(flags & (1U << 23)) || (state.released_slots & (Word{1} << slot)))
                    continue;
                const Address destination = (Address{flags & 0xff} << 32) | qmd[base];
                bool released{};
                if constexpr (std::is_invocable_r_v<bool, Release, Address, Word, Word>) {
                    released = release(destination, qmd[base + 2], flags);
                } else {
                    released = release(destination, qmd[base + 2]);
                }
                if (!released) return "unmapped QMD release";
                state.released_slots |= Word{1} << slot;
            }
            state.completed = true;
            if (has_dependent) {
                pending[count++] = {Address{qmd[15]} << 8,
                                    dependent_action == 4 ? 7U : dependent_action == 3 ? 3U : 2U, 1};
            }
        }
        return error;
    }
private:
    // Release slots (address low, flags | address high, payload). NVN2 patches a third one at
    // word 52 for queue syncs (e.g. a compute queue releasing a semaphore another queue waits on).
    static constexpr std::array<Word, 3> ReleaseSlots{24, 28, 52};

    struct State {
        Descriptor descriptor{};
        std::int32_t dependencies{};
        std::int32_t deferred{};
        bool loaded{};
        bool scheduled{};
        bool completed{};
        bool dispatched{};
        Word released_slots{};
    };
    // One instance must be shared by all GPU channels in a single process
    // epoch: dependency counters belong to QMD addresses, not subchannels.
    // The owner destroys the instance at process teardown.
    std::unordered_map<Address, State> states;
};
// A single authenticated GPU process owns this state and all its channels
// share it. Mutex serializes cross-channel dependency changes; the process
// registry invalidates the entire session on teardown/re-registration.
struct NativeQmdSession {
    std::mutex mutex{};
    NativeQmdScheduler scheduler{};
};
} // namespace Tegra
