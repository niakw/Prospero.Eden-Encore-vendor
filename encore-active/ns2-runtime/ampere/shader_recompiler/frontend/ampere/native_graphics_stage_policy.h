// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Only independent native graphics stages can safely produce a standalone
// SPIR-V module. Other stages need pipeline state, interstage linkage and/or
// a separate QMD decoder; never reinterpret them as a VertexB shader.
// Maintained Eden stage.h uses the global size_t from the C header.
#include <stddef.h>
#include "shader_recompiler/stage.h"

namespace Shader::Ampere {
[[nodiscard]] constexpr bool IsStandaloneNativeGraphicsStage(Shader::Stage stage) noexcept {
    return stage == Shader::Stage::VertexB || stage == Shader::Stage::Fragment;
}
[[nodiscard]] constexpr bool RequiresNativeVertexMerge(Shader::Stage stage) noexcept {
    return stage == Shader::Stage::VertexA;
}

// Maxwell graphics_key.unique_hashes has six slots: VertexA, VertexB,
// tessellation control/evaluation, geometry and Fragment. Native Ampere can
// currently compile the two vertex stages (merged when VertexA is present)
// and Fragment. A pipeline requires a VertexB implementation, including when
// rasterization is discarded; a standalone VertexA program must never be
// sent to the Vulkan pipeline builder as a fictitious vertex shader.
[[nodiscard]] constexpr bool NativeGraphicsStageMaskSupported(u32 mask) noexcept {
    constexpr u32 vertex_b = u32{1} << 1;
    constexpr u32 available = (u32{1} << 0) | vertex_b | (u32{1} << 5);
    return (mask & vertex_b) != 0 && (mask & ~available) == 0;
}

static_assert(NativeGraphicsStageMaskSupported(0x02)); // VertexB only
static_assert(NativeGraphicsStageMaskSupported(0x03)); // VertexA + VertexB
static_assert(NativeGraphicsStageMaskSupported(0x22)); // VertexB + Fragment
static_assert(NativeGraphicsStageMaskSupported(0x23)); // merged + Fragment
static_assert(!NativeGraphicsStageMaskSupported(0x00));
static_assert(!NativeGraphicsStageMaskSupported(0x01)); // unmerged VertexA
static_assert(!NativeGraphicsStageMaskSupported(0x20)); // Fragment only
static_assert(!NativeGraphicsStageMaskSupported(0x21)); // VertexA + Fragment
static_assert(!NativeGraphicsStageMaskSupported(0x06)); // tessellation
static_assert(IsStandaloneNativeGraphicsStage(Shader::Stage::VertexB));
static_assert(IsStandaloneNativeGraphicsStage(Shader::Stage::Fragment));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::VertexA));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::Compute));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::Geometry));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::TessellationControl));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::TessellationEval));
} // namespace Shader::Ampere
