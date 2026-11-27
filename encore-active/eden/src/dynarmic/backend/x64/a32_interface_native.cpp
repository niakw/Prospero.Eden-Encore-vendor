#include <cstdio>
#include <new>
#if defined(EDEN_JIT_A32_DEV_PROFILE)
#include <chrono>
extern "C" bool eden_native_detailed_logging() noexcept __attribute__((weak));
extern "C" void eden_jit_compile(unsigned core, unsigned long long ns) __attribute__((weak));
extern "C" void eden_jit_phases(unsigned core, unsigned long long translate,
                                unsigned long long optimize, unsigned long long emit,
                                unsigned long long location) __attribute__((weak));
extern "C" void eden_jit_cache_event(unsigned core, unsigned kind) __attribute__((weak));
#endif
extern "C" void eden_jit_evacuation(unsigned core) __attribute__((weak));
extern "C" void eden_jit_block(unsigned core, unsigned long long location, const void* entry, unsigned long long size) __attribute__((weak));
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <functional>
#include <memory>
#include <mutex>

#include <boost/icl/interval_set.hpp>
#include <fmt/format.h>
#include "common/assert.h"
#include <bit>
#include "common/common_types.h"
#include "dynarmic/common/llvm_disassemble.h"

#include "dynarmic/backend/x64/a32_emit_x64.h"
#include "dynarmic/backend/x64/a32_jitstate.h"
#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/callback.h"
#include "dynarmic/backend/x64/devirtualize.h"
#include "dynarmic/backend/x64/jitstate_info.h"
#include "dynarmic/common/atomic.h"
#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/frontend/A32/translate/a32_translate.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/location_descriptor.h"
#include "dynarmic/ir/opt_passes.h"

namespace Dynarmic::A32 {

using namespace Backend::X64;

static RunCodeCallbacks GenRunCodeCallbacks(A32::UserCallbacks* cb, CodePtr (*LookupBlock)(void* lookup_block_arg), void* arg, const A32::UserConfig& conf) {
    return RunCodeCallbacks{
        ArgCallback(LookupBlock, reinterpret_cast<u64>(arg)),
        ArgCallback(Devirtualize<&A32::UserCallbacks::AddTicks>(cb)),
        ArgCallback(Devirtualize<&A32::UserCallbacks::GetTicksRemaining>(cb)),
        conf.enable_cycle_counting,
    };
}

static std::function<void(BlockOfCode&)> GenRCP(const A32::UserConfig& conf) {
    return [conf](BlockOfCode& code) {
        if (conf.page_table) {
            code.mov(code.r14, std::bit_cast<u64>(conf.page_table));
        }
        if (conf.fastmem_pointer) {
            code.mov(code.r13, *conf.fastmem_pointer);
        }
    };
}

static Optimization::PolyfillOptions GenPolyfillOptions(const BlockOfCode& code) {
    return Optimization::PolyfillOptions{
        .sha256 = !code.HasHostFeature(HostFeature::SHA),
        .vector_multiply_widen = true,
    };
}

struct Jit::Impl {
    Impl(Jit* jit, A32::UserConfig conf) noexcept
        : block_of_code(GenRunCodeCallbacks(conf.callbacks, &GetCurrentBlockThunk, this, conf), JitStateInfo{jit_state}, conf.code_cache_size, GenRCP(conf))
        , emitter(block_of_code, conf, jit)
        , polyfill_options(GenPolyfillOptions(block_of_code))
        , conf(std::move(conf))
        , jit_interface(jit)
    {}

    ~Impl() = default;

    HaltReason Run() {
        ASSERT(!jit_interface->is_executing);
        PerformRequestedCacheInvalidation(static_cast<HaltReason>(Atomic::Load(&jit_state.halt_reason)));
        jit_interface->is_executing = true;
        const CodePtr current_codeptr = [this] {
            // RSB optimization
            const u32 new_rsb_ptr = (jit_state.rsb_ptr - 1) & A32JitState::RSB_PTR_MASK;
            if (jit_state.GetUniqueHash() == jit_state.rsb_location_descriptors[new_rsb_ptr]) {
                jit_state.rsb_ptr = new_rsb_ptr;
                return reinterpret_cast<CodePtr>(jit_state.rsb_codeptrs[new_rsb_ptr]);
            }

            return GetCurrentBlock();
        }();
        const HaltReason hr = block_of_code.RunCode(&jit_state, current_codeptr);
        PerformRequestedCacheInvalidation(hr);
        jit_interface->is_executing = false;
        return hr;
    }

    HaltReason Step() {
        ASSERT(!jit_interface->is_executing);
        PerformRequestedCacheInvalidation(static_cast<HaltReason>(Atomic::Load(&jit_state.halt_reason)));
        jit_interface->is_executing = true;
        const HaltReason hr = block_of_code.StepCode(&jit_state, GetCurrentSingleStep());
        PerformRequestedCacheInvalidation(hr);
        jit_interface->is_executing = false;
        return hr;
    }

    void ClearCache() {
        std::unique_lock lock{invalidation_mutex};
        invalidate_entire_cache = true;
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void InvalidateCacheRange(std::uint32_t start_address, std::size_t length) {
        if (length == 0) {
            return;
        }
        std::unique_lock lock{invalidation_mutex};
        // A pending full clear already supersedes every narrower invalidation.
        if (!invalidate_entire_cache) {
            // A wrapped or unrepresentable A32 range must not leave stale compiled blocks.
            if (length - 1 > (~u32{0} - start_address)) {
                invalidate_entire_cache = true;
            } else {
                const u32 end_address = start_address + static_cast<u32>(length - 1);
                invalid_cache_ranges.add(boost::icl::discrete_interval<u32>::closed(start_address, end_address));
            }
        }
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void Reset() {
        ASSERT(!jit_interface->is_executing);
        jit_state = {};
    }

    void HaltExecution(HaltReason hr) {
        Atomic::Or(&jit_state.halt_reason, static_cast<u32>(hr));
    }

    void ClearHalt(HaltReason hr) {
        Atomic::And(&jit_state.halt_reason, ~static_cast<u32>(hr));
    }

    void ClearExclusiveState() {
        jit_state.exclusive_state = 0;
    }

    std::array<u32, 16>& Regs() {
        return jit_state.Reg;
    }

    const std::array<u32, 16>& Regs() const {
        return jit_state.Reg;
    }

    std::array<u32, 64>& ExtRegs() {
        return jit_state.ExtReg;
    }

    const std::array<u32, 64>& ExtRegs() const {
        return jit_state.ExtReg;
    }

    u32 Cpsr() const {
        return jit_state.Cpsr();
    }

    void SetCpsr(u32 value) {
        return jit_state.SetCpsr(value);
    }

    u32 Fpscr() const {
        return jit_state.Fpscr();
    }

    void SetFpscr(u32 value) {
        return jit_state.SetFpscr(value);
    }

    std::string Disassemble() const {
        const size_t size = reinterpret_cast<const char*>(block_of_code.getCurr()) - reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        auto const* p = reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        return Common::DisassembleX64(p, p + size);
    }

private:
    static CodePtr GetCurrentBlockThunk(void* this_voidptr) {
        Jit::Impl& this_ = *static_cast<Jit::Impl*>(this_voidptr);
        return this_.GetCurrentBlock();
    }

    IR::LocationDescriptor GetCurrentLocation() const {
        return IR::LocationDescriptor{jit_state.GetUniqueHash()};
    }

    CodePtr GetCurrentBlock() {
        return GetBasicBlock(GetCurrentLocation()).entrypoint;
    }

    CodePtr GetCurrentSingleStep() {
        return GetBasicBlock(A32::LocationDescriptor{GetCurrentLocation()}.SetSingleStepping(true)).entrypoint;
    }

    A32EmitX64::BlockDescriptor GetBasicBlock(IR::LocationDescriptor descriptor) {
        if (auto block = emitter.GetBasicBlock(descriptor))
            return *block;
#if defined(EDEN_JIT_A32_DEV_PROFILE)
        // A32 cold misses must contribute to the same four-core witness as
        // A64. Neither a cache hit nor quiet/release mode reads a clock.
        struct EdenCompileTimer {
            unsigned core;
            bool active;
            std::chrono::steady_clock::time_point start{};
            explicit EdenCompileTimer(unsigned c) : core(c),
                active(eden_native_detailed_logging && eden_native_detailed_logging()) {
                if (active) start = std::chrono::steady_clock::now();
            }
            ~EdenCompileTimer() {
                if (active && eden_jit_compile)
                    eden_jit_compile(core, static_cast<unsigned long long>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - start).count()));
            }
        } eden_compile_timer{unsigned(conf.processor_id)};
#endif

        constexpr size_t MINIMUM_REMAINING_CODESIZE = 1 * 1024 * 1024;
        if (block_of_code.SpaceRemaining() < MINIMUM_REMAINING_CODESIZE) {
            static thread_local unsigned pressure_count;
            if (++pressure_count <= 3) std::printf("EDEN_JIT_PRESSURE bits=32 core=%u capacity=%u remaining=%zu\n", unsigned(conf.processor_id), unsigned(conf.code_cache_size), block_of_code.SpaceRemaining());
            // Match the A64 code-pressure event (not per-block clock work).
            if (eden_jit_evacuation) eden_jit_evacuation(unsigned(conf.processor_id));
            // Publish the full-clear request under invalidation_mutex;
            // concurrent range invalidations must not race with cache pressure.
            ClearCache();
            PerformRequestedCacheInvalidation(HaltReason::CacheInvalidation);
        }
        try {
            block_of_code.EnsureMemoryCommitted(MINIMUM_REMAINING_CODESIZE);
        } catch (const std::bad_alloc&) {
            // Publish the full-clear request under invalidation_mutex;
            // concurrent range invalidations must not race with cache pressure.
            ClearCache();
            PerformRequestedCacheInvalidation(HaltReason::CacheInvalidation);
            block_of_code.EnsureMemoryCommitted(MINIMUM_REMAINING_CODESIZE);
        }

        // LocationDescriptor ctor() does important ops (like tflags) do not skip
        auto const arch_descriptor = A32::LocationDescriptor{descriptor};
        ir_block.Reset(arch_descriptor);
#if defined(EDEN_JIT_A32_DEV_PROFILE)
        const bool eden_phase_timing = eden_compile_timer.active && eden_jit_phases;
        const auto eden_phase_begin = eden_phase_timing ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        A32::Translate(ir_block, arch_descriptor, conf.callbacks, {conf.arch_version, conf.define_unpredictable_behaviour, conf.hook_hint_instructions});
#if defined(EDEN_JIT_A32_DEV_PROFILE)
        const auto eden_phase_translated = eden_phase_timing ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        Optimization::Optimize(ir_block, conf, polyfill_options);
#if defined(EDEN_JIT_A32_DEV_PROFILE)
        const auto eden_phase_optimized = eden_phase_timing ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        const auto emitted = emitter.Emit(ir_block);
#if defined(EDEN_JIT_A32_DEV_PROFILE)
        if (eden_phase_timing) {
            const auto eden_phase_emitted = std::chrono::steady_clock::now();
            const auto eden_ns = [](auto a, auto b) {
                return static_cast<unsigned long long>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
            };
            eden_jit_phases(unsigned(conf.processor_id),
                            eden_ns(eden_phase_begin, eden_phase_translated),
                            eden_ns(eden_phase_translated, eden_phase_optimized),
                            eden_ns(eden_phase_optimized, eden_phase_emitted),
                            descriptor.Value());
        }
#endif
        if (eden_jit_block)
            eden_jit_block(unsigned(conf.processor_id), descriptor.Value(), reinterpret_cast<const void*>(emitted.entrypoint), emitted.size);
        return emitted;
    }

    void PerformRequestedCacheInvalidation(HaltReason hr) {
        if (Has(hr, HaltReason::CacheInvalidation)) {
            // Consume the pending request under its mutex, but never hold it
            // while invalidating compiled blocks or changing RX/RW code state.
            // A concurrent guest write can queue the NEXT request while this
            // JIT thread processes the current snapshot.
            bool clear_all = false;
            boost::icl::interval_set<u32> ranges;
            {
                std::unique_lock lock{invalidation_mutex};
                ClearHalt(HaltReason::CacheInvalidation);
                clear_all = invalidate_entire_cache;
                invalidate_entire_cache = false;
                ranges.swap(invalid_cache_ranges);
            }

            if (!clear_all && ranges.empty()) {
                return;
            }

            jit_state.ResetRSB();
            if (clear_all) {
                block_of_code.ClearCache();
                emitter.ClearCache();
            } else {
                emitter.InvalidateCacheRanges(ranges);
            }
#if defined(EDEN_JIT_A32_DEV_PROFILE)
            // This clear/range belongs to the snapshot actually consumed.
            if (eden_native_detailed_logging && eden_native_detailed_logging() &&
                eden_jit_cache_event) {
                eden_jit_cache_event(unsigned(conf.processor_id),
                                     clear_all ? 0u : 1u);
            }
#endif
        }
    }

    IR::Block ir_block = {LocationDescriptor(0, PSR(0), FPSCR(0), false)};
    A32JitState jit_state;
    BlockOfCode block_of_code;
    A32EmitX64 emitter;
    Optimization::PolyfillOptions polyfill_options;
    // Keep it here in order to not mess with initializer lists
    const A32::UserConfig conf;
    Jit* jit_interface;

    // Requests made during execution to invalidate the cache are queued up here.
    bool invalidate_entire_cache = false;
    boost::icl::interval_set<u32> invalid_cache_ranges;
    std::mutex invalidation_mutex;
};

Jit::Jit(UserConfig conf)
        : impl(std::make_unique<Impl>(this, std::move(conf))) {}

Jit::~Jit() = default;

HaltReason Jit::Run() {
    return impl->Run();
}

HaltReason Jit::Step() {
    return impl->Step();
}

void Jit::ClearCache() {
    impl->ClearCache();
}

void Jit::InvalidateCacheRange(std::uint32_t start_address, std::size_t length) {
    impl->InvalidateCacheRange(start_address, length);
}

void Jit::Reset() {
    impl->Reset();
}

void Jit::HaltExecution(HaltReason hr) {
    impl->HaltExecution(hr);
}

void Jit::ClearHalt(HaltReason hr) {
    impl->ClearHalt(hr);
}

std::array<std::uint32_t, 16>& Jit::Regs() {
    return impl->Regs();
}

const std::array<std::uint32_t, 16>& Jit::Regs() const {
    return impl->Regs();
}

std::array<std::uint32_t, 64>& Jit::ExtRegs() {
    return impl->ExtRegs();
}

const std::array<std::uint32_t, 64>& Jit::ExtRegs() const {
    return impl->ExtRegs();
}

std::uint32_t Jit::Cpsr() const {
    return impl->Cpsr();
}

void Jit::SetCpsr(std::uint32_t value) {
    impl->SetCpsr(value);
}

std::uint32_t Jit::Fpscr() const {
    return impl->Fpscr();
}

void Jit::SetFpscr(std::uint32_t value) {
    impl->SetFpscr(value);
}

void Jit::ClearExclusiveState() {
    impl->ClearExclusiveState();
}

std::string Jit::Disassemble() const {
    return impl->Disassemble();
}

}  // namespace Dynarmic::A32
