// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/ir_bridge.h"
#include <stdexcept>
#include "shader_recompiler/program_header.h"

namespace Shader::Ampere {
Shader::IR::Program TranslateReviewedGraphicsIR(
    Shader::ObjectPool<Shader::IR::Inst>& inst_pool,
    Shader::ObjectPool<Shader::IR::Block>& block_pool,
    Shader::ObjectPool<Shader::Maxwell::Flow::Block>& flow_pool,
    NativeMaxwellEnvironment& env,
    const Shader::HostTranslateInfo& host_info,
    bool exits_to_dispatcher) {
    // The Vulkan graphics pipeline starts its Maxwell CFG after the SPH.
    // ReadInstruction() receives byte addresses; both sides must agree
    // before allocating CFG blocks or executing any guest-dependent reads.
    static_assert(sizeof(Shader::ProgramHeader) == 0x50);
    const u32 address = env.StartAddress() + static_cast<u32>(sizeof(Shader::ProgramHeader));
    if (!env.Image().ValidForCompiler() ||
        env.Image().native_stage != static_cast<u32>(env.ShaderStage()) ||
        env.ShaderStage() == Shader::Stage::Compute ||
        address != env.Image().first_instruction_address ||
        !env.Image().ReadMaxwellInstruction(address)) {
        throw std::invalid_argument{"NS2 IR bridge requires verified native graphics instructions"};
    }
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, address, exits_to_dispatcher};
    return Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host_info);
}
} // namespace Shader::Ampere
