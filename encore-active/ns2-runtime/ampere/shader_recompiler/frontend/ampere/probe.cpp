// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/probe.h"

#include <array>
#include <bit>
#include <cstring>
#include "shader_recompiler/frontend/ampere/ampere_isa_table.h"

namespace Shader::Ampere {
namespace {
constexpr std::size_t MaxInstructions = 65536;
struct OpcodeIndex {
    std::array<const IsaRow*, 4096> slots{};
    constexpr OpcodeIndex() {
        for (const auto& entry : ISA_ROWS) slots[entry.op12] = &entry.row;
    }
};
constexpr OpcodeIndex Opcodes{};
} // namespace

const IsaRow* FindOpcodeRow(u32 op12) {
    return Opcodes.slots[op12 & 0xfff];
}
int FirstUnreviewedBit(const Instruction& instruction, const IsaRow& row) {
    for (u32 i = 0; i != 4; ++i) {
        const u32 unknown = instruction.words[i] & ~row.known[i];
        if (unknown) return static_cast<int>(i * 32 + std::countr_zero(unknown));
    }
    return -1;
}
ProgramProbe ProbeProgram(std::span<const u8> code) {
    ProgramProbe result;
    if (code.empty()) return result;
    if (code.size() % sizeof(Instruction)) {
        result.status = ProbeStatus::InvalidSize;
        return result;
    }
    if (code.size() / sizeof(Instruction) > MaxInstructions) {
        result.status = ProbeStatus::TooManyInstructions;
        return result;
    }

    result.instructions = static_cast<u32>(code.size() / sizeof(Instruction));
    result.status = ProbeStatus::Recognized;
    for (u32 i = 0; i != result.instructions; ++i) {
        Instruction inst{};
        // Explicit little-endian decode, without alignment or host-endianness
        // assumptions and without reading beyond the supplied byte range.
        const u8* p = code.data() + i * sizeof(Instruction);
        for (u32 w = 0; w != 4; ++w) {
            for (u32 byte = 0; byte != 4; ++byte) {
                inst.words[w] |= u32{p[w * 4 + byte]} << (byte * 8);
            }
        }
        const u32 op12 = inst.words[0] & 0xfff;
        const IsaRow* row = FindOpcodeRow(op12);
        if (!row) {
            if (result.status == ProbeStatus::Recognized ||
                result.status == ProbeStatus::UnreviewedFields) {
                result.status = ProbeStatus::UnknownOpcode;
                result.first_problem_index = i;
                result.first_problem_opcode = op12;
            }
            continue;
        }
        ++result.recognized;
        const auto kind = Classify(op12).kind;
        if (kind != OpKind::Plain && kind != OpKind::Nop) ++result.control_flow;
        const int unknown_bit = FirstUnreviewedBit(inst, *row);
        if (unknown_bit >= 0) {
            ++result.unreviewed_fields;
            if (result.status == ProbeStatus::Recognized) {
                result.status = ProbeStatus::UnreviewedFields;
                result.first_problem_index = i;
                result.first_problem_opcode = op12;
                result.first_unknown_bit = unknown_bit;
            }
        }
    }
    return result;
}
} // namespace Shader::Ampere
