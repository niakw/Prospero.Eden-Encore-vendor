// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Standalone per-GPU-channel native MME upload/execution state. Unlike the
// Switch 1 macro engine it stores TU104 96-bit instructions and 0xC00 DRAM.
// No API here binds Ampere 3D to a legacy Maxwell renderer.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "video_core/macro_turing.h"

namespace Tegra {
class NativeAmpereMacroState final {
public:
    using Word=std::uint32_t;
    using Result=TuringMacroInterpreter::Result;
    using DataRam=TuringMacroInterpreter::DataRam;

    // Hardware data RAM belongs to the GPU channel and is shared by its
    // 3D and compute MME engines. Code/position RAM remain engine-local.
    explicit NativeAmpereMacroState(DataRam* shared_dram=nullptr) noexcept
        : active_dram{shared_dram ? shared_dram : &owned_dram} {}
    NativeAmpereMacroState(const NativeAmpereMacroState&)=delete;
    NativeAmpereMacroState& operator=(const NativeAmpereMacroState&)=delete;
    static constexpr std::size_t MaxCodeWords=0x3000;
    static constexpr std::size_t MaxPositions=0x80;
    static constexpr std::size_t MaxFifo=4096;
    [[nodiscard]] bool SetCodePointer(Word instruction_index) {
        if (instruction_index>=MaxCodeWords/3) return false;
        write_offset=std::size_t{instruction_index}*3;
        return true;
    }
    [[nodiscard]] bool PushCode(Word word) {
        if (write_offset>=code.size()) return false;
        code[write_offset++]=word;
        uploaded=std::max(uploaded,write_offset);
        return true;
    }
    [[nodiscard]] bool SetPositionPointer(Word index) {
        if (index>=positions.size()) return false;
        position_pointer=index;
        return true;
    }
    [[nodiscard]] bool BindPosition(Word start_pc) {
        if (position_pointer>=positions.size() ||
            start_pc>=MaxCodeWords/3) return false;
        positions[position_pointer]=start_pc;
        bound[position_pointer]=true;
        ++position_pointer;
        return true;
    }
    template<class Read, class Emit>
    [[nodiscard]] Result Execute(Word macro_method,
                                 std::span<const Word> parameters,
                                 Read read, Emit emit) {
        return ExecuteWithMutableFifo(
            macro_method, parameters, read,
            [&](Word address, Word value, std::vector<Word>&) {
                return emit(address, value);
            });
    }

    // Stage an entire guest MME invocation before publishing GPU methods.
    // A rejected late register read, invalid emit, or missing terminator
    // must not leave the first half of a draw/PCAS command live. Reads of
    // earlier emitted registers see the pending value, never stale hardware
    // state. The channel DRAM is restored if the interpreter rejects.
    //
    // Some TU104 methods (notably MME_DMA_READ[_FIFOED]) affect the MME's
    // private DRAM/load FIFO while the macro is still executing. The effects
    // overload exposes only those temporary objects to the caller; published
    // GPU register writes remain staged until the full invocation validates.
    template<class Read, class Allowed, class Effect, class Commit, class Valid>
    [[nodiscard]] Result ExecuteStagedWithEffects(
        Word macro_method, std::span<const Word> parameters,
        Read read, Allowed allowed, Effect effect, Commit commit, Valid valid) {
        static constexpr std::size_t MaxPendingMethods = 65536;
        const auto old_dram = *active_dram;
        std::vector<std::pair<Word, Word>> pending;
        std::unordered_map<Word, Word> shadow;
        const auto stage_read = [&](Word address) -> Word {
            if (const auto it = shadow.find(address); it != shadow.end())
                return it->second;
            return read(address);
        };
        const auto stage_emit = [&](Word address, Word value,
                                    std::vector<Word>& fifo) -> bool {
            if (pending.size() >= MaxPendingMethods || !allowed(address, value))
                return false;
            pending.emplace_back(address, value);
            shadow[address] = value;
            return effect(address, value, fifo, *active_dram, stage_read);
        };
        Result result{};
        bool validation_ok{};
        try {
            result = ExecuteWithMutableFifo(
                macro_method, parameters, stage_read, stage_emit);
            // Validation is part of staging. If the channel epoch or a
            // register-read validator throws, restore MME DRAM before the
            // caller rejects the invocation; no methods have been committed.
            if (result.success) validation_ok = valid();
        } catch (...) {
            *active_dram = old_dram;
            throw;
        }
        if (!result.success || !validation_ok) {
            *active_dram = old_dram;
            if (result.success)
                return {false, result.pc, result.consumed,
                        "invalid guest MME register read"};
            return result;
        }
        // The external GPU method owner can throw during commit. Restore
        // channel-local DRAM even then; published GPU methods cannot be
        // undone here, so the Puller MUST poison the native channel on
        // that exception rather than resume a partially applied macro.
        try {
            for (const auto& [address, value] : pending)
                commit(address, value);
        } catch (...) {
            *active_dram = old_dram;
            throw;
        }
        return result;
    }

    template<class Read, class Allowed, class Commit, class Valid>
    [[nodiscard]] Result ExecuteStaged(
        Word macro_method, std::span<const Word> parameters,
        Read read, Allowed allowed, Commit commit, Valid valid) {
        const auto no_effect = [](Word, Word, std::vector<Word>&,
                                  DataRam&, const auto&) { return true; };
        return ExecuteStagedWithEffects(
            macro_method, parameters, read, allowed, no_effect, commit, valid);
    }

    [[nodiscard]] std::size_t UploadedWords() const {return uploaded;}
private:
    template<class Read, class Emit>
    [[nodiscard]] Result ExecuteWithMutableFifo(
        Word macro_method, std::span<const Word> parameters,
        Read read, Emit emit) {
        // NVIDIA exposes 256 CALL_MME_MACRO/CALL_MME_DATA method pairs
        // at 0xe00..0xfff, while the hardware position RAM has 128 entries.
        // The high half aliases the same position table, matching Eden's
        // Maxwell path and the observed NX2 C797/C7C0 implementations.
        if(macro_method<0xe00 || macro_method>=0x1000 ||
           (macro_method&1u)!=0u) return {false,0,0,"invalid macro method"};
        const auto index=
            std::size_t{(macro_method-0xe00u)>>1}%positions.size();
        if(!bound[index])
            return {false,0,0,"unbound macro entry"};
        const auto start=positions[index];
        if(std::size_t{start}*3+2>=uploaded)
            return {false,start,0,"instruction outside uploaded RAM"};
        if(parameters.size()>MaxFifo)
            return {false,start,0,"native macro FIFO limit exceeded"};
        std::vector<Word> fifo;
        fifo.reserve(MaxFifo);
        fifo.insert(fifo.end(), parameters.begin(), parameters.end());
        interpreter.active_dram=active_dram;
        return interpreter.Execute(
            std::span<const Word>{code.data(),uploaded}, start, fifo, read,
            [&](Word address, Word value) {
                return emit(address, value, fifo);
            });
    }
    std::array<Word,MaxCodeWords> code{};
    std::size_t write_offset{};
    std::size_t uploaded{};
    std::array<Word,MaxPositions> positions{};
    std::array<bool,MaxPositions> bound{};
    std::size_t position_pointer{};
    TuringMacroInterpreter interpreter;
    DataRam owned_dram{};
    DataRam* active_dram{};
};
} // namespace Tegra
