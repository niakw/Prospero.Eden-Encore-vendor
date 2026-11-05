// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <limits>
#include <mutex>
#include <new>
#include <optional>

namespace Tegra {

// Guest QMD counters are signed: untrusted PCAS dependency updates can
// accumulate beyond the representable range. Reject them before changing
// state rather than overflowing a signed integer (undefined behaviour).
[[nodiscard]] constexpr std::optional<std::int32_t> NativeQmdDependencyAdd(
    std::int32_t value, std::int32_t delta) noexcept {
    if ((delta > 0 && value > std::numeric_limits<std::int32_t>::max() - delta) ||
        (delta < 0 && value < std::numeric_limits<std::int32_t>::min() - delta))
        return std::nullopt;
    return value + delta;
}

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
        // A diagnostic observer may allocate or throw. Never let logging
        // unwind between a successful vkCmdDispatch and the accepted-state
        // bit, or between partially queued semaphore releases.
        const auto safe_trace = [&](std::string_view event, Address qmd_addr,
                                    std::int32_t value) noexcept {
            try { if (trace) trace(event, qmd_addr, value); } catch (...) {}
        };
        std::string_view error;
        // Every used slot is initialized before access; avoid clearing all
        // 256 entries (4 KiB) for each ordinary one-QMD PCAS signal.
        std::array<Pending, 256> pending;
        std::size_t count = 1;
        pending[0] = {address, argument & 15, ((argument >> 14) & 0x3ff) + 1};
        for (std::size_t i = 0; i < count; ++i) {
            const auto request = pending[i];
            // PCAS actions 0/1 do not read, schedule, or modify a QMD.
            // Never allocate a persistent state for them: an untrusted
            // no-op stream can otherwise fill all 4096 entries and prevent
            // legitimate Ampere compute launches from entering the cache.
            if (request.action == 0 || request.action == 1)
                continue;
            // Rejected command types are also stateless. Otherwise a guest
            // stream of malformed PCAS actions creates empty entries before
            // returning an error, exhausting the quota of real QMD3 work.
            if (request.action != 2 && request.action != 3 &&
                request.action != 7 && request.action != 12)
                return "unsupported PCAS action";
            if (states.size() >= 4096 && !states.contains(request.address)) {
                // Finished QMDs with nothing deferred are only history, drop them instead of stalling.
                std::erase_if(states, [](const auto& entry) {
                    return entry.second.completed && entry.second.deferred == 0;
                });
                if (states.size() >= 4096) return "QMD cache limit";
            }
            // QMD state allocation is driven by untrusted PCAS addresses.
            // A failed unordered_map growth must produce an ordinary GPU
            // rejection, not unwind through the GPU worker/process thread.
            // No dispatch or semaphore has been accepted at this point.
            decltype(states)::iterator state_it;
            bool newly_inserted{};
            try {
                const auto [it, inserted] = states.try_emplace(request.address);
                state_it = it;
                newly_inserted = inserted;
            } catch (const std::bad_alloc&) {
                return "QMD state allocation failed";
            }
            auto& state = state_it->second;
            // A callback can throw after queuing irreversible Vulkan work or
            // a guest semaphore. Neither the GPU command nor its fence can
            // be rolled back. Keep the QMD quarantined until process reset;
            // retries must never duplicate dispatches or reductions.
            if (state.poisoned) return "QMD transaction poisoned";
            if (request.action == 3 || request.action == 2) {
                if (request.action == 3 || !state.loaded) {
                    // A caller may retry a failed release with action 2. A
                    // fresh action 3 must not reset accepted semaphore slots
                    // and re-execute a shader from an unfinished transaction.
                    if (request.action == 3 && state.dispatched && !state.completed)
                        return "QMD reload during pending release";
                    bool descriptor_loaded{};
                    try {
                        descriptor_loaded = read(request.address, state.descriptor);
                    } catch (...) {
                        if (newly_inserted) states.erase(state_it);
                        return "QMD read exception";
                    }
                    if (!descriptor_loaded) {
                        // A bad guest QMD VA must not exhaust the process cache
                        // with 4096 empty, never-loaded state entries.
                        if (newly_inserted) states.erase(state_it);
                        return "unmapped QMD";
                    }
                    const auto& qmd = state.descriptor;
                    if ((qmd[18] & 0xff) != 0x30 || (qmd[4] & 0x100)) {
                        if (newly_inserted) states.erase(state_it);
                        return "unsupported QMD version or queue";
                    }
                    if constexpr (std::is_same_v<Dispatch, RejectShaderDispatch>) {
                        if (qmd[12] && (qmd[13] & 0xffff) && (qmd[14] & 0xffff)) {
                            if (newly_inserted) states.erase(state_it);
                            return "native shader dispatch required";
                        }
                    }
                    const auto dependencies = NativeQmdDependencyAdd(
                        std::int32_t((qmd[4] >> 14) & 0xffff), state.deferred);
                    if (!dependencies) return "QMD dependency overflow";
                    state.dependencies = *dependencies;
                    safe_trace("base", request.address, std::int32_t((qmd[4] >> 14) & 0xffff));
                    safe_trace("load", request.address, state.dependencies);
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
                    const auto deferred = NativeQmdDependencyAdd(state.deferred, delta);
                    if (!deferred) return "QMD dependency overflow";
                    state.deferred = *deferred;
                    safe_trace("defer", request.address, state.deferred);
                    continue;
                }
                const auto dependencies = NativeQmdDependencyAdd(state.dependencies, delta);
                if (!dependencies) return "QMD dependency overflow";
                state.dependencies = *dependencies;
                safe_trace("count", request.address, state.dependencies);
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
            if (!state.dispatched) {
                bool descriptor_loaded{};
                try {
                    descriptor_loaded = read(request.address, state.descriptor);
                } catch (...) {
                    return "QMD read exception";
                }
                if (!descriptor_loaded) return "unmapped QMD";
            }
            const auto qmd = state.descriptor;
            if ((qmd[18] & 0xff) != 0x30 || (qmd[4] & 0x100)) return "unsupported QMD version or queue";
            // Validate the complete dependent QMD structure before a shader
            // launch or guest-visible release. A late error must never leave
            // semaphores published while silently dropping the dependency.
            if (qmd[16] & (1U << 5)) {
                if (newly_inserted) states.erase(state_it);
                return "second dependent QMD unsupported";
            }
            const bool has_dependent = (qmd[16] & 1U) != 0;
            const Word dependent_action = (qmd[16] >> 1) & 7U;
            if (has_dependent &&
                (dependent_action != 4 && dependent_action != 3 && dependent_action != 1)) {
                if (newly_inserted) states.erase(state_it);
                return "unsupported dependent QMD action";
            }
            if (has_dependent && count == pending.size()) {
                if (newly_inserted) states.erase(state_it);
                return "QMD chain limit";
            }
            // Validate the release slots before making any visible.
            for (const Word base : ReleaseSlots) {
                const Word flags = qmd[base + 1];
                if (!(flags & (1U << 23))) continue;
                const bool add = (flags & (1U << 26)) != 0;
                if ((flags & (0x3bU << 24)) || (flags >> 30) != 1 ||
                    (add && (flags & (7U << 20)))) {
                    if (newly_inserted) states.erase(state_it);
                    return "unsupported QMD release format";
                }
                if (add) {
                    if constexpr (!std::is_invocable_r_v<bool, Release, Address, Word, Word>) {
                        if (newly_inserted) states.erase(state_it);
                        return "unsupported QMD reduction callback";
                    }
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
            bool dispatched = state.dispatched ||
                !(qmd[12] && (qmd[13] & 0xffff) && (qmd[14] & 0xffff));
            if (!dispatched) {
                try {
                    dispatched = dispatch(qmd);
                } catch (...) {
                    state.poisoned = true;
                    return "QMD dispatch exception";
                }
            }
            safe_trace("run", request.address, dispatched ? 1 : 0);
            if (!dispatched) return "native shader dispatch required";
            state.dispatched = true;
            for (std::size_t slot = 0; slot < ReleaseSlots.size(); ++slot) {
                const Word base = ReleaseSlots[slot];
                const Word flags = qmd[base + 1];
                if (!(flags & (1U << 23)) || (state.released_slots & (Word{1} << slot)))
                    continue;
                const Address destination = (Address{flags & 0xff} << 32) | qmd[base];
                bool released{};
                try {
                    if constexpr (std::is_invocable_r_v<bool, Release, Address, Word, Word>) {
                        released = release(destination, qmd[base + 2], flags);
                    } else {
                        released = release(destination, qmd[base + 2]);
                    }
                } catch (...) {
                    state.poisoned = true;
                    return "QMD release exception";
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
        bool poisoned{};
        Word released_slots{};
    };
    // One instance must be shared by all GPU channels in a single process
    // epoch: dependency counters belong to QMD addresses, not subchannels.
    // The owner destroys the instance at process teardown.
    std::unordered_map<Address, State> states;
};

// A native QMD can patch up to three completion semaphores. Authenticate
// ALL enabled target spans before submitting its shader: rejecting the
// second/third target only after dispatch leaves work running with a queue
// that can never signal completion. This is a pure 49-bit GMMU preflight;
// the real fence callback still rechecks mapping and process ownership.
template <typename IsMapped>
[[nodiscard]] bool NativeQmdReleaseTargetsMapped(
    const NativeQmdScheduler::Descriptor& qmd, IsMapped&& is_mapped) {
    constexpr std::uint64_t gpu_limit = std::uint64_t{1} << 49;
    try {
        for (const std::uint32_t base : {24U, 28U, 52U}) {
            const std::uint32_t flags = qmd[base + 1];
            if (!(flags & (1U << 23))) continue;
            const std::uint64_t addr =
                (std::uint64_t{flags & 0xffU} << 32) | qmd[base];
            if (!addr || (addr & 3U) || addr >= gpu_limit ||
                sizeof(std::uint32_t) > gpu_limit - addr ||
                !is_mapped(addr, sizeof(std::uint32_t)))
                return false;
        }
        return true;
    } catch (...) {
        // A sparse unmap or mapping callback failure cannot escape into
        // the GPU worker AFTER other preparation has begun. No shader
        // submission and no guest semaphore release for a partial map.
        return false;
    }
}
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
    // Ampere CTAs have at most 1024 active threads. Reject an oversized
    // QMD before shader capture or allocating guest/host compute resources.
    // Divide before multiplying so malicious 16-bit dimensions cannot
    // overflow the aggregate thread count.
    std::uint64_t threads_per_block{1};
    for (const auto dim : launch.block) {
        if (dim == 0 || dim > 1024U / threads_per_block)
            return std::nullopt;
        threads_per_block *= dim;
    }
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
        // A half-open CBUF span ending exactly at 2^49 is valid:
        // its final byte is 2^49-1. Only a strict overflow is rejected.
        if (buffer.enabled && buffer.size &&
            buffer.gpu_address > gpu_address_limit - buffer.size)
            return std::nullopt;
    }
    return launch;
}

// A valid Ampere QMD can still exceed the destination Vulkan device's
// per-axis group limits, total invocations, or shared-memory budget.
// Keep this preflight independent of Vulkan so host tests can exercise
// the exact policy used by the native PS5 renderer.
struct NativeComputeDeviceLimits {
    std::array<std::uint32_t, 3> max_grid{};
    std::array<std::uint32_t, 3> max_block{};
    std::uint32_t max_invocations{};
    std::uint32_t max_shared_bytes{};
};

[[nodiscard]] constexpr bool NativeComputeLaunchWithinDeviceLimits(
    const NativeComputeLaunch& launch, const NativeComputeDeviceLimits& limits) noexcept {
    if (!limits.max_invocations || launch.shared_bytes > limits.max_shared_bytes)
        return false;
    std::uint64_t threads{1};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!launch.grid[axis] || launch.grid[axis] > limits.max_grid[axis] ||
            !launch.block[axis] || launch.block[axis] > limits.max_block[axis] ||
            launch.block[axis] > limits.max_invocations / threads)
            return false;
        threads *= launch.block[axis];
    }
    return true;
}

// Confirm the first native 128-bit shader instruction and every enabled
// constant-buffer span are backed by the current GMMU before any renderer
// attempts native compilation or descriptor binding. The eventual full
// shader capture must still validate each additional instruction read.
// Do not use this preflight as a substitute for GPU resource ownership.
template <typename IsMapped>
[[nodiscard]] inline bool NativeComputeLaunchMapped(
    const NativeComputeLaunch& launch, IsMapped&& mapped) {
    // DecodeNativeComputeLaunch authenticates QMD3 metadata, but this is
    // also a public preflight taking an owned/possibly modified launch.
    // Never call a GMMU walker with a 128-bit shader instruction or a CBUF
    // crossing the real 49-bit VA limit. A failed mapping callback must
    // reject the whole launch BEFORE dispatch, like release-target checks.
    constexpr std::uint64_t gpu_limit = std::uint64_t{1} << 49;
    try {
        if (launch.program_address >= gpu_limit ||
            std::uint64_t{16} > gpu_limit - launch.program_address ||
            !mapped(launch.program_address, std::size_t{16}))
            return false;
        for (const auto& cbuf : launch.constant_buffers) {
            if (!cbuf.enabled || !cbuf.size) continue;
            if (cbuf.gpu_address >= gpu_limit ||
                cbuf.size > gpu_limit - cbuf.gpu_address ||
                !mapped(cbuf.gpu_address, std::size_t{cbuf.size}))
                return false;
        }
        return true;
    } catch (...) {
        // The map may change during sparse remap/unmap. A throwing walker
        // must not escape into the irreversible QMD scheduler transaction.
        return false;
    }
}

// Native 0xC7C0 bulk register writes are mostly raw state updates.
// Eden's KeplerCompute::CallMethod() is equivalent to one store for these
// methods, but dispatching that virtual call for thousands of native MME
// writes each frame is avoidable. Preserve every special side-effect method:
// TU104 MME uploads, legacy DMA uploads/launch, QMD3 PCAS, inline QMD
// publication and native report semaphore. The caller still authenticates
// the engine and checks the actual register-file bound.
[[nodiscard]] constexpr bool NativeComputePlainRegister(std::uint32_t method) {
    if (method >= 0xe00U) return false;
    switch (method) {
    case 0x45U: case 0x46U: case 0x47U: case 0x48U:
    case 0x6cU: case 0x6dU: // legacy upload execution / data
    case 0xaeU: case 0xafU: case 0xb0U: // native PCAS and legacy launch
    case 0x107U: // native inline QMD publication
    case 0x6c3U: // compute semaphore report/reduction
        return false;
    default:
        return true;
    }
}

// Ampere SET_REPORT_SEMAPHORE_A/D has an extended high-address field.
// Preserve the full guest address for validation: truncating it to 49 bits
// would silently signal an unrelated low GPU page. The native GMMU has
// exactly 49 VA bits. A report writes either one word or four words.
// Validate the complete span before queueing any GPU fence/query.
[[nodiscard]] constexpr bool NativeComputeSemaphoreSpanValid(
    std::uint64_t address, std::size_t bytes) noexcept {
    constexpr std::uint64_t limit = std::uint64_t{1} << 49;
    return (bytes == sizeof(std::uint32_t) ||
            bytes == 4U * sizeof(std::uint32_t)) &&
           address != 0 && (address & 3U) == 0 &&
           address < limit && bytes <= limit - address;
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
