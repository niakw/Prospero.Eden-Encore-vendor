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
#include <vector>
#include "video_core/macro_turing.h"

namespace Tegra {
class NativeAmpereMacroState final {
public:
    using Word=std::uint32_t;
    using Result=TuringMacroInterpreter::Result;
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
        if(macro_method<0xe00 || macro_method>=0xf00 ||
           (macro_method&1u)!=0u) return {false,0,0,"invalid macro method"};
        const auto index=std::size_t{(macro_method-0xe00u)>>1};
        if(index>=positions.size() || !bound[index])
            return {false,0,0,"unbound macro entry"};
        const auto start=positions[index];
        if(std::size_t{start}*3+2>=uploaded)
            return {false,start,0,"instruction outside uploaded RAM"};
        if(parameters.size()>MaxFifo)
            return {false,start,0,"native macro FIFO limit exceeded"};
        std::vector<Word> fifo{parameters.begin(),parameters.end()};
        interpreter.active_dram=&dram;
        return interpreter.Execute(
            std::span<const Word>{code.data(),uploaded},start,fifo,read,emit);
    }
    [[nodiscard]] std::size_t UploadedWords() const {return uploaded;}
private:
    std::array<Word,MaxCodeWords> code{};
    std::size_t write_offset{};
    std::size_t uploaded{};
    std::array<Word,MaxPositions> positions{};
    std::array<bool,MaxPositions> bound{};
    std::size_t position_pointer{};
    TuringMacroInterpreter interpreter;
    TuringMacroInterpreter::DataRam dram{};
};
} // namespace Tegra
