// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/renderer_spirv_bridge.h"
#include <exception>
#include <utility>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/ir_bridge.h"
#include "shader_recompiler/frontend/ampere/native_graphics_stage_policy.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/runtime_info.h"

namespace Shader::Ampere {
NativeSpirvResult CompileReviewedNativeGraphicsSpirv(
    NativeMaxwellEnvironment& env,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    bool exits_to_dispatcher) {
    // Standalone Fragment is safe only with the real caller's stage-specific
    // RuntimeInfo. Do not pretend VertexA is merged or synthesize tessellation
    // interfaces, geometry linkage or compute QMD metadata.
    if (!IsStandaloneNativeGraphicsStage(env.ShaderStage()))
        return {NativeSpirvStatus::UnsupportedStage, {}};
    if (!env.Image().ValidForCompiler() ||
        env.Image().native_stage != static_cast<u32>(env.ShaderStage()))
        return {NativeSpirvStatus::InvalidImage, {}};
    try {
        Shader::ObjectPool<Shader::IR::Inst> inst_pool{8192};
        Shader::ObjectPool<Shader::IR::Block> block_pool{512};
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool{512};
        auto ir = TranslateReviewedGraphicsIR(
            inst_pool, block_pool, flow_pool, env, host_info, exits_to_dispatcher);
        if (ir.stage != env.ShaderStage() || ir.blocks.empty())
            return {NativeSpirvStatus::CompilationFailed, {}};
        // The production renderer builds its own RuntimeInfo from the
        // pipeline key and bound device features; never invent these here.
        Shader::Maxwell::ConvertLegacyToGeneric(ir, runtime_info);
        Shader::Backend::Bindings bindings{};
        auto words = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime_info, ir,
                                                        bindings);
        if (words.size() <= 5 || words.size() > 262144 ||
            words[0] != 0x07230203U || words[3] == 0)
            return {NativeSpirvStatus::InvalidModule, {}};
        return {NativeSpirvStatus::Ready, std::move(words)};
    } catch (const std::exception&) {
        // No partial shader publication or fallback to raw NS2 GMMU bytes.
        return {NativeSpirvStatus::CompilationFailed, {}};
    }
}
NativeSpirvResult CompileReviewedNativeVertexSpirv(
    NativeMaxwellEnvironment& env,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    bool exits_to_dispatcher) {
    if (env.ShaderStage() != Shader::Stage::VertexB)
        return {NativeSpirvStatus::UnsupportedStage, {}};
    return CompileReviewedNativeGraphicsSpirv(
        env, host_info, profile, runtime_info, exits_to_dispatcher);
}
} // namespace Shader::Ampere
