// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/lower_bridge.h"
#include <algorithm>
#include <vector>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace Shader::Ampere {
namespace {
// This bridge is used only for early shader analysis. The upstream lowering
// supports up to 65536 instructions, but keep this entry point much smaller
// until native shader allocation and cache lifetimes have been qualified.
constexpr std::size_t BridgeMaxInstructions = 4096;
constexpr std::size_t BridgeMaxMaxwellWords = 131072;
}

LowerBridgeResult LowerReviewedToMaxwell(std::span<const u8> native_code) {
    LowerBridgeResult result;
    if (native_code.empty() || native_code.size() % sizeof(Instruction) ||
        native_code.size() / sizeof(Instruction) > BridgeMaxInstructions) {
        result.reason = "invalid bounded native shader length";
        return result;
    }
    result.preflight = ProbeProgram(native_code);
    if (result.preflight.status == ProbeStatus::UnknownOpcode) {
        result.status = LowerBridgeStatus::UnrecognizedOpcode;
        result.failed_native_byte_offset = result.preflight.first_problem_index * 16;
        result.reason = "opcode not in pinned reviewed Ampere table";
        return result;
    }
    if (result.preflight.status == ProbeStatus::UnreviewedFields) {
        result.status = LowerBridgeStatus::UnreviewedFields;
        result.failed_native_byte_offset = result.preflight.first_problem_index * 16;
        result.reason = "native instruction has fields outside known opcode masks";
        return result;
    }
    if (result.preflight.status != ProbeStatus::Recognized) {
        result.reason = "unrecognized or malformed 128-bit shader program";
        return result;
    }

    std::vector<Instruction> input(native_code.size() / sizeof(Instruction));
    for (std::size_t i = 0; i != input.size(); ++i) {
        const u8* p = native_code.data() + i * sizeof(Instruction);
        for (u32 word = 0; word != 4; ++word)
            for (u32 byte = 0; byte != 4; ++byte)
                input[i].words[word] |= u32{p[word * 4 + byte]} << (byte * 8);
    }
    auto lowered = LowerToMaxwell(input);
    if (!lowered) {
        result.status = LowerBridgeStatus::UnsupportedSemantics;
        result.failed_native_byte_offset = lowered.error_offset;
        result.reason = lowered.error;
        return result;
    }

    // Avoid returning a successful but structurally incomplete lowering:
    // require the source map to account for each consumed native instruction,
    // ordered Maxwell offsets and a non-empty result. The caller is still
    // responsible for translating this to its environment's instruction
    // scheduling/control words and for compiling guest IR.
    if (lowered.instructions.empty() ||
        lowered.instructions.size() > BridgeMaxMaxwellWords ||
        !lowered.source_size ||
        lowered.source_size % sizeof(Instruction) ||
        lowered.source_size > native_code.size() ||
        lowered.source_instruction_offsets.size() !=
            lowered.source_size / sizeof(Instruction) ||
        !std::is_sorted(lowered.source_instruction_offsets.begin(),
                        lowered.source_instruction_offsets.end()) ||
        std::any_of(lowered.source_instruction_offsets.begin(),
                    lowered.source_instruction_offsets.end(),
                    [&](u32 offset) { return offset >= lowered.instructions.size(); })) {
        result.status = LowerBridgeStatus::InvalidOutput;
        result.reason = "native lowering source map or Maxwell stream invalid";
        return result;
    }

    result.status = LowerBridgeStatus::Ready;
    result.consumed_native_bytes = lowered.source_size;
    result.source_instruction_offsets = std::move(lowered.source_instruction_offsets);
    result.maxwell_instructions = std::move(lowered.instructions);
    return result;
}
} // namespace Shader::Ampere
