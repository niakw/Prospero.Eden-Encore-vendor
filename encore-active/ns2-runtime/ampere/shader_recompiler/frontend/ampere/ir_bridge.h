// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Opt-in Ampere -> Maxwell CFG/IR research handoff. Does not create a
// SPIR-V module or attach a pipeline to the production Vulkan renderer.
#include "shader_recompiler/frontend/ampere/maxwell_environment.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"

namespace Shader::Ampere {

// Caller owns all pools until the resulting IR and its blocks have been
// consumed, and keeps env alive for the full translation. Neither the native
// source bytes nor the Switch 1 shader cache are consulted by this function.
[[nodiscard]] Shader::IR::Program TranslateReviewedGraphicsIR(
    Shader::ObjectPool<Shader::IR::Inst>& inst_pool,
    Shader::ObjectPool<Shader::IR::Block>& block_pool,
    Shader::ObjectPool<Shader::Maxwell::Flow::Block>& flow_pool,
    NativeMaxwellEnvironment& env,
    const Shader::HostTranslateInfo& host_info,
    bool exits_to_dispatcher = false);
} // namespace Shader::Ampere
