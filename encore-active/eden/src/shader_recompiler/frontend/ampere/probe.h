// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bounded SM70+/Ampere instruction recognition for opt-in NS2 research.
// Decoding is not shader translation, compilation, Vulkan rendering or a
// promise that the upstream opcode masks cover every retail GPU program.
#include <cstddef>
#include <span>
#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/classify.h"
#include "shader_recompiler/frontend/ampere/instruction.h"

namespace Shader::Ampere {
enum class ProbeStatus : u8 {
    Recognized,
    Empty,
    InvalidSize,
    TooManyInstructions,
    UnknownOpcode,
    UnreviewedFields,
};

struct ProgramProbe {
    ProbeStatus status{ProbeStatus::Empty};
    u32 instructions{};
    u32 recognized{};
    u32 control_flow{};
    u32 unreviewed_fields{};
    u32 first_problem_index{};
    u32 first_problem_opcode{};
    int first_unknown_bit{-1};
};

// Reads only complete 16-byte instructions from a bounded byte stream.
[[nodiscard]] ProgramProbe ProbeProgram(std::span<const u8> code);
// Individual 128-bit instruction classification; returns nullptr for unknown.
struct IsaRow;
[[nodiscard]] const IsaRow* FindOpcodeRow(u32 op12);
[[nodiscard]] int FirstUnreviewedBit(const Instruction& instruction, const IsaRow& row);
} // namespace Shader::Ampere
