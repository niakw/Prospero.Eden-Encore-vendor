// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Research-only Ampere -> Maxwell lowering handoff. It returns a bounded
// legacy instruction stream, NOT a compiled shader nor an executable pipeline.
#include <span>
#include <string_view>
#include <vector>
#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/probe.h"

namespace Shader::Ampere {
enum class LowerBridgeStatus : u8 {
    Ready,
    InvalidInput,
    UnrecognizedOpcode,
    UnreviewedFields,
    UnsupportedSemantics,
    InvalidOutput,
};
struct LowerBridgeResult {
    LowerBridgeStatus status{LowerBridgeStatus::InvalidInput};
    ProgramProbe preflight{};
    std::vector<u64> maxwell_instructions{};
    std::vector<u32> source_instruction_offsets{};
    u32 consumed_native_bytes{};
    u32 failed_native_byte_offset{};
    std::string_view reason{};
    [[nodiscard]] bool Ready() const { return status == LowerBridgeStatus::Ready; }
};
// A real, tested translation for precisely the native instructions supported
// by the pinned upstream lowering table. Fail-closed for every other opcode,
// unreviewed bit, branch relocation or unsupported semantic. No guest I/O.
[[nodiscard]] LowerBridgeResult LowerReviewedToMaxwell(std::span<const u8> native_code);
} // namespace Shader::Ampere
