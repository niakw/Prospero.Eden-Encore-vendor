#include <array>
#include <chrono>
#include <cstdio>
#include <new>
#include <optional>
extern "C" bool eden_native_detailed_logging() noexcept __attribute__((weak));
extern "C" void eden_jit_evacuation(unsigned core) __attribute__((weak));
extern "C" void eden_jit_compile(unsigned core, unsigned long long ns) __attribute__((weak));
extern "C" void eden_jit_phases(unsigned core, unsigned long long translate, unsigned long long optimize, unsigned long long emit, unsigned long long location) __attribute__((weak));
#if defined(EDEN_JIT_A64_DEV_PROFILE)
extern "C" void eden_jit_cache_event(unsigned core, unsigned kind) __attribute__((weak));
#endif
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <cstring>
#include <memory>
#include <mutex>

#include <boost/icl/interval_set.hpp>
#include "common/assert.h"
#include "dynarmic/common/fp/fpcr.h"
#include "dynarmic/common/llvm_disassemble.h"
#include <bit>

#include "dynarmic/backend/x64/a64_emit_x64.h"
#include "dynarmic/backend/x64/a64_jitstate.h"
#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/devirtualize.h"
#include "dynarmic/backend/x64/jitstate_info.h"
#include "dynarmic/common/atomic.h"
#include "dynarmic/frontend/A64/translate/a64_translate.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/opt_passes.h"

namespace Dynarmic::A64 {

using namespace Backend::X64;

static RunCodeCallbacks GenRunCodeCallbacks(A64::UserCallbacks* cb, CodePtr (*LookupBlock)(void* lookup_block_arg), void* arg, const A64::UserConfig& conf) {
    return RunCodeCallbacks{
        ArgCallback(LookupBlock, reinterpret_cast<u64>(arg)),
        ArgCallback(Devirtualize<&A64::UserCallbacks::AddTicks>(cb)),
        ArgCallback(Devirtualize<&A64::UserCallbacks::GetTicksRemaining>(cb)),
        conf.enable_cycle_counting,
    };
}

static std::function<void(BlockOfCode&)> GenRCP(const A64::UserConfig& conf) {
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

struct Jit::Impl final {
public:
    Impl(Jit* jit, UserConfig conf)
        : conf(conf)
        , block_of_code(GenRunCodeCallbacks(conf.callbacks, &GetCurrentBlockThunk, this, conf), JitStateInfo{jit_state}, conf.code_cache_size, GenRCP(conf))
        , emitter(block_of_code, conf, jit)
        , polyfill_options(GenPolyfillOptions(block_of_code))
    {
        ASSERT(conf.page_table_address_space_bits >= 12 && conf.page_table_address_space_bits <= 64);
    }

    ~Impl() = default;

    HaltReason Run() {
        ASSERT(!is_executing);
        PerformRequestedCacheInvalidation(static_cast<HaltReason>(Atomic::Load(&jit_state.halt_reason)));
        is_executing = true;
        // TODO: Check code alignment
        const CodePtr current_code_ptr = [this] {
            // RSB optimization
            const u32 new_rsb_ptr = (jit_state.rsb_ptr - 1) & A64JitState::RSB_PTR_MASK;
            if (jit_state.GetUniqueHash() == jit_state.rsb_location_descriptors[new_rsb_ptr]) {
                jit_state.rsb_ptr = new_rsb_ptr;
                return CodePtr(jit_state.rsb_codeptrs[new_rsb_ptr]);
            }
            return CodePtr((uintptr_t(GetCurrentBlock()) + 15) & ~uintptr_t(15));
        }();
        const HaltReason hr = block_of_code.RunCode(&jit_state, current_code_ptr);
        PerformRequestedCacheInvalidation(hr);
        is_executing = false;
        return hr;
    }

    HaltReason Step() {
        ASSERT(!is_executing);
        PerformRequestedCacheInvalidation(static_cast<HaltReason>(Atomic::Load(&jit_state.halt_reason)));
        is_executing = true;
        const HaltReason hr = block_of_code.StepCode(&jit_state, GetCurrentSingleStep());
        PerformRequestedCacheInvalidation(hr);
        is_executing = false;
        return hr;
    }

    void ClearCache() {
        std::unique_lock lock{invalidation_mutex};
        invalidate_entire_cache = true;
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void InvalidateCacheRange(u64 start_address, size_t length) {
        if (length == 0) {
            return;
        }
        std::unique_lock lock{invalidation_mutex};
        // A pending full clear already supersedes every narrower invalidation.
        if (!invalidate_entire_cache) {
            // A wrapped or unrepresentable address range must invalidate conservatively.
            if (length - 1 > (~u64{0} - start_address)) {
                invalidate_entire_cache = true;
            } else {
                const u64 end_address = start_address + static_cast<u64>(length - 1);
                const auto range = boost::icl::discrete_interval<u64>::closed(start_address, end_address);
                invalid_cache_ranges.add(range);
            }
        }
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void Reset() {
        ASSERT(!is_executing);
        jit_state = {};
    }

    void HaltExecution(HaltReason hr) {
        Atomic::Or(&jit_state.halt_reason, static_cast<u32>(hr));
    }

    void ClearHalt(HaltReason hr) {
        Atomic::And(&jit_state.halt_reason, ~static_cast<u32>(hr));
    }

    u64 GetSP() const {
        return jit_state.sp;
    }

    void SetSP(u64 value) {
        jit_state.sp = value;
    }

    u64 GetPC() const {
        return jit_state.pc;
    }

    void SetPC(u64 value) {
        jit_state.pc = value;
    }

    u64 GetRegister(size_t index) const {
        if (index == 31)
            return GetSP();
        return jit_state.reg.at(index);
    }

    void SetRegister(size_t index, u64 value) {
        if (index == 31)
            return SetSP(value);
        jit_state.reg.at(index) = value;
    }

    std::array<u64, 31> GetRegisters() const {
        return jit_state.reg;
    }

    void SetRegisters(const std::array<u64, 31>& value) {
        jit_state.reg = value;
    }

    Vector GetVector(size_t index) const {
        return {jit_state.vec.at(index * 2), jit_state.vec.at(index * 2 + 1)};
    }

    void SetVector(size_t index, Vector value) {
        jit_state.vec.at(index * 2) = value[0];
        jit_state.vec.at(index * 2 + 1) = value[1];
    }

    std::array<Vector, 32> GetVectors() const {
        std::array<Vector, 32> ret;
        static_assert(sizeof(ret) == sizeof(jit_state.vec));
        std::memcpy(ret.data(), jit_state.vec.data(), sizeof(jit_state.vec));
        return ret;
    }

    void SetVectors(const std::array<Vector, 32>& value) {
        static_assert(sizeof(value) == sizeof(jit_state.vec));
        std::memcpy(jit_state.vec.data(), value.data(), sizeof(jit_state.vec));
    }

    u32 GetFpcr() const {
        return jit_state.GetFpcr();
    }

    void SetFpcr(u32 value) {
        jit_state.SetFpcr(value);
    }

    u32 GetFpsr() const {
        return jit_state.GetFpsr();
    }

    void SetFpsr(u32 value) {
        jit_state.SetFpsr(value);
    }

    u32 GetPstate() const {
        return jit_state.GetPstate();
    }

    void SetPstate(u32 value) {
        jit_state.SetPstate(value);
    }

    void ClearExclusiveState() {
        jit_state.exclusive_state = 0;
    }

    bool IsExecuting() const {
        return is_executing;
    }

    std::string Disassemble() const {
        const size_t size = reinterpret_cast<const char*>(block_of_code.getCurr()) - reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        auto const* p = reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        return Common::DisassembleX64(p, p + size);
    }

private:
    static CodePtr GetCurrentBlockThunk(void* thisptr) {
        Jit::Impl* this_ = static_cast<Jit::Impl*>(thisptr);
        return this_->GetCurrentBlock();
    }

    IR::LocationDescriptor GetCurrentLocation() const {
        return IR::LocationDescriptor{jit_state.GetUniqueHash()};
    }

    CodePtr GetCurrentBlock() {
        return GetBlock(GetCurrentLocation());
    }

    CodePtr GetCurrentSingleStep() {
        return GetBlock(A64::LocationDescriptor{GetCurrentLocation()}.SetSingleStepping(true));
    }

    CodePtr GetBlock(IR::LocationDescriptor descriptor) {
        if (auto block = emitter.GetBasicBlock(descriptor))
            return block->entrypoint;
#if defined(EDEN_JIT_A64_DEV_PROFILE)
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
#ifdef PS5_NATIVE
            static thread_local unsigned pressure_count;
            if (eden_native_detailed_logging && eden_native_detailed_logging() && ++pressure_count <= 200) std::printf("EDEN_JIT_PRESSURE core=%u capacity=%u remaining=%zu count=%u mono_ns=%lld\n", unsigned(conf.processor_id), unsigned(conf.code_cache_size), block_of_code.SpaceRemaining(), pressure_count, static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
            if (eden_jit_evacuation) eden_jit_evacuation(unsigned(conf.processor_id));
#else
            std::fprintf(stderr, "EDEN_JIT_PRESSURE core=%u capacity=%u remaining=%zu\n", unsigned(conf.processor_id), unsigned(conf.code_cache_size), block_of_code.SpaceRemaining());
#endif
            // Immediately evacuate cache
            // Publish the full-clear request under invalidation_mutex;
            // concurrent range invalidations must not race with cache pressure.
            ClearCache();
            PerformRequestedCacheInvalidation(HaltReason::CacheInvalidation);
        }
#ifdef PS5_NATIVE
        try {
            block_of_code.EnsureMemoryCommitted(MINIMUM_REMAINING_CODESIZE);
        } catch (const std::bad_alloc&) {
            // Publish the full-clear request under invalidation_mutex;
            // concurrent range invalidations must not race with cache pressure.
            ClearCache();
            PerformRequestedCacheInvalidation(HaltReason::CacheInvalidation);
            block_of_code.EnsureMemoryCommitted(MINIMUM_REMAINING_CODESIZE);
        }
#else
        block_of_code.EnsureMemoryCommitted(MINIMUM_REMAINING_CODESIZE);
#endif

        // JIT Compile
#if defined(EDEN_JIT_A64_BATCH)
        // Publish bounded, unconditional chains together without compiling
        // either unselected side of a conditional branch.
        // Do not follow indirect branches, supervisor calls or halt terminals.
        const auto get_code = [this](u64 vaddr) { return conf.callbacks->MemoryReadCode(vaddr); };
        struct Publish {
            BlockOfCode& code;
            ~Publish() { code.DisableWriting(); }
        } publish{block_of_code};
        CodePtr first = nullptr;
        std::array<std::optional<IR::LocationDescriptor>, 32> pending{};
        pending[0] = descriptor;
        unsigned queued = 1, batch = 0;
        for (unsigned cursor = 0; cursor < queued && batch < 16; ++cursor) {
            descriptor = *pending[cursor];
            if (cursor && (emitter.GetBasicBlock(descriptor) ||
                           !get_code(A64::LocationDescriptor{descriptor}.PC())))
                continue;
            if (batch && block_of_code.SpaceRemaining() < MINIMUM_REMAINING_CODESIZE)
                break;
            // Commit the lazy code cache before each batched successor, not only the first.
            if (batch) block_of_code.EnsureMemoryCommitted(MINIMUM_REMAINING_CODESIZE);
            // Count additional batched blocks.
            const auto arch_descriptor = A64::LocationDescriptor{descriptor};
            ir_block.Reset(arch_descriptor);
#if defined(EDEN_JIT_A64_DEV_PROFILE)
            const auto eden_t0 = std::chrono::steady_clock::now();
            A64::Translate(ir_block, arch_descriptor, get_code, {conf.define_unpredictable_behaviour, conf.wall_clock_cntpct});
            const auto eden_t1 = std::chrono::steady_clock::now();
            Optimization::Optimize(ir_block, conf, polyfill_options);
            const auto eden_t2 = std::chrono::steady_clock::now();
            const auto entrypoint = emitter.Emit(ir_block, false).entrypoint;
            if (eden_jit_phases) {
                const auto eden_ns = [](auto a, auto b) { return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()); };
                eden_jit_phases(unsigned(conf.processor_id), eden_ns(eden_t0, eden_t1), eden_ns(eden_t1, eden_t2), eden_ns(eden_t2, std::chrono::steady_clock::now()), descriptor.Value());
            }
#else
            A64::Translate(ir_block, arch_descriptor, get_code, {conf.define_unpredictable_behaviour, conf.wall_clock_cntpct});
            Optimization::Optimize(ir_block, conf, polyfill_options);
            const auto entrypoint = emitter.Emit(ir_block, false).entrypoint;
#endif
            if (!first) first = entrypoint;
            ++batch;
            if (arch_descriptor.SingleStepping() || block_of_code.hasWritableAlias()) break;
            const auto queue = [&](const IR::Term::LeafTerminal& leaf) {
                const IR::LocationDescriptor* next = nullptr;
                if (const auto* link = std::get_if<IR::Term::LinkBlock>(&leaf)) next = &link->next;
                if (const auto* link = std::get_if<IR::Term::LinkBlockFast>(&leaf)) next = &link->next;
                if (!next || queued == pending.size() || emitter.GetBasicBlock(*next)) return;
                for (unsigned i = 0; i < queued; ++i)
                    if (*pending[i] == *next) return;
                pending[queued++] = *next;
            };
            if (const auto* leaf = std::get_if<IR::Term::LeafTerminal>(&ir_block.terminal)) queue(*leaf);
        }
        return first;
#else
        const auto get_code = [this](u64 vaddr) { return conf.callbacks->MemoryReadCode(vaddr); };
        // LocationDescriptor ctor() does important ops (like tflags) do not skip
        auto const arch_descriptor = A64::LocationDescriptor{descriptor};
        ir_block.Reset(arch_descriptor);
#if defined(EDEN_JIT_A64_DEV_PROFILE)
        // Detailed-only phase measurements for the default per-core compiler.
        // The release path and quiet DEV path do not read any clocks here.
        const bool eden_phase_timing = eden_compile_timer.active && eden_jit_phases;
        const auto eden_phase_begin = eden_phase_timing ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        A64::Translate(ir_block, arch_descriptor, get_code, {conf.define_unpredictable_behaviour, conf.wall_clock_cntpct});
#if defined(EDEN_JIT_A64_DEV_PROFILE)
        const auto eden_phase_translated = eden_phase_timing ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        Optimization::Optimize(ir_block, conf, polyfill_options);
#if defined(EDEN_JIT_A64_DEV_PROFILE)
        const auto eden_phase_optimized = eden_phase_timing ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        const auto entrypoint = emitter.Emit(ir_block).entrypoint;
#if defined(EDEN_JIT_A64_DEV_PROFILE)
        if (eden_phase_timing) {
            const auto eden_phase_emitted = std::chrono::steady_clock::now();
            const auto eden_phase_ns = [](auto a, auto b) {
                return static_cast<unsigned long long>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
            };
            eden_jit_phases(unsigned(conf.processor_id),
                            eden_phase_ns(eden_phase_begin, eden_phase_translated),
                            eden_phase_ns(eden_phase_translated, eden_phase_optimized),
                            eden_phase_ns(eden_phase_optimized, eden_phase_emitted),
                            descriptor.Value());
        }
#endif
        return entrypoint;
#endif
    }

    void PerformRequestedCacheInvalidation(HaltReason hr) {
        if (Has(hr, HaltReason::CacheInvalidation)) {
            // Consume the pending request under its mutex, but never hold it
            // while invalidating compiled blocks or changing RX/RW code state.
            // A concurrent guest write can queue the NEXT request while this
            // JIT thread processes the current snapshot.
            bool clear_all = false;
            boost::icl::interval_set<u64> ranges;
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
#if defined(EDEN_JIT_A64_DEV_PROFILE)
            // Attribute the event to this snapshot, not a newer request.
            if (eden_native_detailed_logging && eden_native_detailed_logging() &&
                eden_jit_cache_event) {
                eden_jit_cache_event(unsigned(conf.processor_id),
                                     clear_all ? 0u : 1u);
            }
#endif
        }
    }

    IR::Block ir_block = {LocationDescriptor(0, FP::FPCR(0), false)};
    const UserConfig conf;
    A64JitState jit_state;
    BlockOfCode block_of_code;
    A64EmitX64 emitter;
    Optimization::PolyfillOptions polyfill_options;
    bool is_executing = false;
    bool invalidate_entire_cache = false;
    boost::icl::interval_set<u64> invalid_cache_ranges;
    std::mutex invalidation_mutex;
};

Jit::Jit(UserConfig conf)
        : impl(std::make_unique<Jit::Impl>(this, conf)) {}

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

void Jit::InvalidateCacheRange(u64 start_address, size_t length) {
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

u64 Jit::GetSP() const {
    return impl->GetSP();
}

void Jit::SetSP(u64 value) {
    impl->SetSP(value);
}

u64 Jit::GetPC() const {
    return impl->GetPC();
}

void Jit::SetPC(u64 value) {
    impl->SetPC(value);
}

u64 Jit::GetRegister(size_t index) const {
    return impl->GetRegister(index);
}

void Jit::SetRegister(size_t index, u64 value) {
    impl->SetRegister(index, value);
}

std::array<u64, 31> Jit::GetRegisters() const {
    return impl->GetRegisters();
}

void Jit::SetRegisters(const std::array<u64, 31>& value) {
    impl->SetRegisters(value);
}

Vector Jit::GetVector(size_t index) const {
    return impl->GetVector(index);
}

void Jit::SetVector(size_t index, Vector value) {
    impl->SetVector(index, value);
}

std::array<Vector, 32> Jit::GetVectors() const {
    return impl->GetVectors();
}

void Jit::SetVectors(const std::array<Vector, 32>& value) {
    impl->SetVectors(value);
}

u32 Jit::GetFpcr() const {
    return impl->GetFpcr();
}

void Jit::SetFpcr(u32 value) {
    impl->SetFpcr(value);
}

u32 Jit::GetFpsr() const {
    return impl->GetFpsr();
}

void Jit::SetFpsr(u32 value) {
    impl->SetFpsr(value);
}

u32 Jit::GetPstate() const {
    return impl->GetPstate();
}

void Jit::SetPstate(u32 value) {
    impl->SetPstate(value);
}

void Jit::ClearExclusiveState() {
    impl->ClearExclusiveState();
}

bool Jit::IsExecuting() const {
    return impl->IsExecuting();
}

std::string Jit::Disassemble() const {
    return impl->Disassemble();
}

}  // namespace Dynarmic::A64
