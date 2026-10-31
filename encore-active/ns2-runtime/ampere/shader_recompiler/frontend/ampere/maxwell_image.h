// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Opt-in bridge into the *existing* Maxwell shader-environment code ABI.
// The result is a read-only guest-code image, not a compiled GPU pipeline.
#include <optional>
#include <span>
#include <vector>
#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/lower_bridge.h"

namespace Shader::Ampere {

enum class MaxwellImageStatus : u8 {
    Ready,
    InvalidHeader,
    LoweringFailed,
    InvalidLayout,
};

struct MaxwellShaderImage {
    MaxwellImageStatus status{MaxwellImageStatus::InvalidHeader};
    LowerBridgeStatus lowering_status{LowerBridgeStatus::InvalidInput};
    u32 failed_native_offset{};
    u32 consumed_native_bytes{};
    u32 first_instruction_address{0x50};
    u32 instruction_end_address{};
    u32 cached_highest_address{};
    u64 diagnostic_cache_key{};
    // Concrete Shader::Stage index, not merely a cache hash input.
    u32 native_stage{};
    std::vector<u64> code{};
    [[nodiscard]] bool Ready() const { return status == MaxwellImageStatus::Ready; }
    // Ensure copied/movable images have an intact CFG-executable layout.
    [[nodiscard]] bool ValidForCompiler() const;
    // ReadInstruction-compatible: rejects header, schedule slots and
    // appended branch maps, rather than exposing metadata as executable code.
    [[nodiscard]] std::optional<u64> ReadMaxwellInstruction(u32 address) const;
    // Convert native BRA/BRX targets (byte addresses) to Maxwell byte offsets.
    [[nodiscard]] std::optional<u32> TranslateNativeAddress(u32 address) const;
};

// The header is the maintained Maxwell 0x50-byte guest shader header,
// supplied by the caller after checking guest memory ownership/mapping.
// Input begins at the first 128-bit native instruction, not the header.
// The shader_stage value is for cache/domain separation, NOT stage support.
[[nodiscard]] MaxwellShaderImage BuildMaxwellShaderImage(
    std::span<const u8> maxwell_header, std::span<const u8> native_code,
    u32 shader_stage);
} // namespace Shader::Ampere
