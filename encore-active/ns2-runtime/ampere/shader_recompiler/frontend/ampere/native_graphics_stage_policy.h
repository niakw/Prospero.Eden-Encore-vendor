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
static_assert(IsStandaloneNativeGraphicsStage(Shader::Stage::VertexB));
static_assert(IsStandaloneNativeGraphicsStage(Shader::Stage::Fragment));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::VertexA));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::Compute));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::Geometry));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::TessellationControl));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::TessellationEval));
} // namespace Shader::Ampere
