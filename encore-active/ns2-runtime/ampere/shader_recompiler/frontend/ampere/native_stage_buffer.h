// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include "shader_recompiler/frontend/ampere/instruction.h"

namespace Shader::Ampere {

// SM70+ ISBERD/ISBEWR stage-buffer operands (Lichii nx2, generic Task/Mesh ABI).
// Decoding is NOT an implementation of stage-buffer reads/writes: a valid
// instruction must still be lowered through the Task/Mesh IR, SPIR-V payload
// and Vulkan pipeline; returning an ordinary Maxwell MOV would lose geometry.
struct NativeStageBufferAccess {
    u32 address_register{};
    u32 value_register{};
    u32 byte_offset{};
    bool write{};
    bool width_32{};
    bool attribute{};
    bool skew{};
    bool output{};
};

[[nodiscard]] inline std::optional<NativeStageBufferAccess>
DecodeNativeStageBufferAccess(const Instruction& instruction) {
    const u32 opcode = instruction.Bits(0, 12);
    if (opcode != 0x923 && opcode != 0x927) return std::nullopt;

    const bool write = opcode == 0x927;
    const u32 region = instruction.Bits(76, 2);
    const bool output = instruction.Bits(79, 1) != 0;
    const bool is_32 = instruction.Bits(75, 1) != 0;
    const u32 value = instruction.Bits(write ? 32 : 16, 8);
    // Only region 3 (attribute) and 0 (primitive/map) are defined.
    // Task/Mesh reads are attribute-input; writes are output-region.
    // U8 writes in the attribute region are not supported by the native
    // guest stage-buffer layout and must not silently become U32 writes.
    if ((instruction.words[2] & ~0xf800U) != 0 ||
        (region != 0 && region != 3) ||
        (write != output) ||
        (!write && region != 3) ||
        (!write && value == 255) ||
        (write && region == 3 && !is_32)) {
        return std::nullopt;
    }
    return NativeStageBufferAccess{
        .address_register = instruction.Bits(24, 8),
        .value_register = value,
        .byte_offset = instruction.Bits(40, 24),
        .write = write,
        .width_32 = is_32,
        .attribute = region == 3,
        .skew = instruction.Bits(78, 1) != 0,
        .output = output,
    };
}

} // namespace Shader::Ampere
