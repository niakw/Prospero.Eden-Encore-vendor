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
#include <optional>

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
            // The SDK may patch release payloads while dependencies are
            // pending, so fetch the descriptor just before FIRST dispatch.
            // Once a GPU submission was accepted, that descriptor and its
            // release payloads form a single immutable transaction. On a
            // partial semaphore failure, do not read possibly reused guest
            // memory and attach different releases to the original dispatch.
            if (!state.dispatched &&
                !read(request.address, state.descriptor))
                return "unmapped QMD";
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
// Decode the real QMD3 launch ABI into an immutable host description. This
// is deliberately NOT a Vulkan submission: the Ampere compute shader,
// descriptors and target-device pipeline must first be installed by their
// own authenticated NS2 renderer owner.
struct NativeComputeLaunch {
    struct ConstantBuffer {
        std::uint64_t gpu_address{};
        std::uint32_t size{};
        bool enabled{};
    };
    std::uint64_t code_base{};
    std::uint64_t program_address{};
    std::uint32_t program_start{};
    std::array<std::uint32_t, 3> grid{};
    std::array<std::uint32_t, 3> block{};
    std::array<ConstantBuffer, 8> constant_buffers{};
    std::uint32_t shared_bytes{};
    std::uint32_t local_pos_bytes{};
    std::uint32_t local_crs_bytes{};
    bool linked_tsc{};
};

[[nodiscard]] inline std::optional<NativeComputeLaunch> DecodeNativeComputeLaunch(
    const NativeQmdScheduler::Descriptor& qmd) {
    if ((qmd[18] & 0xffU) != 0x30U || (qmd[4] & 0x100U))
        return std::nullopt;
    NativeComputeLaunch launch;
    launch.grid = {qmd[12], qmd[13] & 0xffffU, qmd[14] & 0xffffU};
    launch.block = {qmd[18] >> 16, qmd[19] & 0xffffU, qmd[19] >> 16};
    for (auto dim : launch.grid)
        if (dim == 0) return std::nullopt;
    for (auto dim : launch.block)
        if (dim == 0) return std::nullopt;
    // QMD3 stores a 49-bit code base as a low word and 17 address bits.
    // The remaining bits of qmd[49] are control fields, not address bits.
    constexpr std::uint64_t gpu_address_limit = std::uint64_t{1} << 49;
    launch.code_base = (std::uint64_t{qmd[49] & 0x1ffffU} << 32) | qmd[48];
    launch.program_start = qmd[8];
    if (launch.code_base >= gpu_address_limit ||
        launch.program_start >= gpu_address_limit - launch.code_base)
        return std::nullopt;
    launch.program_address = launch.code_base + launch.program_start;
    launch.linked_tsc = (qmd[11] & (1U << 30)) != 0;
    launch.shared_bytes = qmd[17] & 0x3ffffU;
    // QMD3 allocates 24 bits to each native local-memory size field.
    // A 20-bit mask silently dropped high allocation bits and could bind
    // too little scratch space for an otherwise valid Ampere program.
    launch.local_pos_bytes = qmd[23] & 0xffffffU;
    launch.local_crs_bytes = qmd[50] & 0xffffffU;
    const std::uint32_t cbuf_mask = qmd[20] & 0xffU;
    for (std::size_t index = 0; index < launch.constant_buffers.size(); ++index) {
        const auto high = qmd[33 + index * 2];
        // A native Ampere QMD3 CBUF address uses the same 49-bit GPU VA
        // space as its program code. Reject only the two unsupported high
        // control bits; truncating bits 8..16 silently aliases guest buffers
        // with an unrelated low-40-bit mapping.
        if (high & 0x60000U) return std::nullopt;
        auto& buffer = launch.constant_buffers[index];
        buffer.gpu_address = (std::uint64_t{high & 0x1ffffU} << 32) |
                             qmd[32 + index * 2];
        buffer.size = ((high >> 19) & 0x1fffU) << 4;
        buffer.enabled = (cbuf_mask & (1U << index)) != 0;
        if (buffer.enabled && buffer.size &&
            buffer.gpu_address >= gpu_address_limit - buffer.size)
            return std::nullopt;
    }
    return launch;
}

// Confirm the first native 128-bit shader instruction and every enabled
// constant-buffer span are backed by the current GMMU before any renderer
// attempts native compilation or descriptor binding. The eventual full
// shader capture must still validate each additional instruction read.
// Do not use this preflight as a substitute for GPU resource ownership.
template <typename IsMapped>
[[nodiscard]] inline bool NativeComputeLaunchMapped(
    const NativeComputeLaunch& launch, IsMapped&& mapped) {
    if (!mapped(launch.program_address, std::size_t{16}))
        return false;
    for (const auto& cbuf : launch.constant_buffers) {
        if (cbuf.enabled && cbuf.size &&
            !mapped(cbuf.gpu_address, std::size_t{cbuf.size}))
            return false;
    }
    return true;
}

// Native SET_REPORT_SEMAPHORE_D reduction operations (Ampere class 0xC7C0).
// Side-effect free so the GPU fence callback can apply them only after
// validating that the owning channel and destination mapping still exist.
[[nodiscard]] constexpr std::uint32_t NativeComputeSemaphoreReduction(
    std::uint32_t old_value, std::uint32_t payload, std::uint32_t mode) {
    switch (mode) {
    case 0: return old_value + payload;
    case 1: return std::min(old_value, payload);
    case 2: return std::max(old_value, payload);
    case 3: return old_value >= payload ? 0U : old_value + 1U;
    case 4: return old_value == 0 || old_value > payload
                       ? payload : old_value - 1U;
    case 5: return old_value & payload;
    case 6: return old_value | payload;
    default: return old_value ^ payload;
    }
}

// A single authenticated GPU process owns this state and all its channels
// share it. Mutex serializes cross-channel dependency changes; the process
// registry invalidates the entire session on teardown/re-registration.
struct NativeQmdSession {
    std::mutex mutex{};
    NativeQmdScheduler scheduler{};
};
} // namespace Tegra
