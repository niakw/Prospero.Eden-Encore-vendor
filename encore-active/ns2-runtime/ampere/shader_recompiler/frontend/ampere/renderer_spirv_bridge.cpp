// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/renderer_spirv_bridge.h"
#include <cstddef>
#include <exception>
#include <utility>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/ir_bridge.h"
#include "shader_recompiler/frontend/ampere/native_indirect_cbuf.h"
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
    bool exits_to_dispatcher,
    Shader::Backend::Bindings initial_bindings) {
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
        Shader::Backend::Bindings bindings{initial_bindings};
        NativeSpirvCbufBoundsScope native_bounds;
        auto words = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime_info, ir,
                                                        bindings);
        if (words.size() <= 5 || words.size() > 262144 ||
            words[0] != 0x07230203U || words[3] == 0)
            return {NativeSpirvStatus::InvalidModule, {}};
        return {NativeSpirvStatus::Ready, std::move(words),
                std::move(ir.info), bindings, env.ShaderStage()};
    } catch (const std::exception&) {
        // No partial shader publication or fallback to raw NS2 GMMU bytes.
        return {NativeSpirvStatus::CompilationFailed, {}};
    }
}
NativeSpirvResult CompileReviewedNativeMergedVertexSpirv(
    NativeMaxwellEnvironment& vertex_a,
    NativeMaxwellEnvironment& vertex_b,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Backend::Bindings initial_bindings) {
    if (vertex_a.ShaderStage()!=Shader::Stage::VertexA ||
        vertex_b.ShaderStage()!=Shader::Stage::VertexB ||
        !vertex_a.Image().ValidForCompiler() || !vertex_b.Image().ValidForCompiler() ||
        vertex_a.Image().native_stage!=static_cast<u32>(Shader::Stage::VertexA) ||
        vertex_b.Image().native_stage!=static_cast<u32>(Shader::Stage::VertexB))
        return {NativeSpirvStatus::UnsupportedStage, {}};
    try {
        // Preserve the exact upstream order: translate both independently,
        // merge with the actual Maxwell VertexA/B IR passes, then lower to
        // SPIR-V only once for the combined VertexB stage.
        Shader::ObjectPool<Shader::IR::Inst> inst_pool{16384};
        Shader::ObjectPool<Shader::IR::Block> block_pool{1024};
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool{1024};
        auto ir_a=TranslateReviewedGraphicsIR(
            inst_pool,block_pool,flow_pool,vertex_a,host_info,true);
        auto ir_b=TranslateReviewedGraphicsIR(
            inst_pool,block_pool,flow_pool,vertex_b,host_info,false);
        if (ir_a.stage!=Shader::Stage::VertexA ||
            ir_b.stage!=Shader::Stage::VertexB ||
            ir_a.blocks.empty() || ir_b.blocks.empty())
            return {NativeSpirvStatus::CompilationFailed, {}};
        auto merged=Shader::Maxwell::MergeDualVertexPrograms(ir_a,ir_b,vertex_b);
        if (merged.stage!=Shader::Stage::VertexB || merged.blocks.empty())
            return {NativeSpirvStatus::CompilationFailed, {}};
        Shader::Maxwell::ConvertLegacyToGeneric(merged,runtime_info);
        Shader::Backend::Bindings bindings{initial_bindings};
        NativeSpirvCbufBoundsScope native_bounds;
        auto words=Shader::Backend::SPIRV::EmitSPIRV(
            profile,runtime_info,merged,bindings);
        if (words.size()<=5 || words.size()>262144 ||
            words[0]!=0x07230203U || words[3]==0)
            return {NativeSpirvStatus::InvalidModule, {}};
        return {NativeSpirvStatus::Ready,std::move(words),
                std::move(merged.info),bindings, Shader::Stage::VertexB};
    } catch (const std::exception&) {
        return {NativeSpirvStatus::CompilationFailed, {}};
    }
}

NativeSpirvResult CompileReviewedNativeComputeSpirv(
    NativeMaxwellEnvironment& env,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Backend::Bindings initial_bindings,
    u32 qmd_enabled_cbuf_mask) {
    if (env.ShaderStage() != Shader::Stage::Compute ||
        !env.Image().ValidForCompiler() ||
        env.Image().native_stage != static_cast<u32>(Shader::Stage::Compute))
        return {NativeSpirvStatus::UnsupportedStage, {}};
    try {
        Shader::ObjectPool<Shader::IR::Inst> inst_pool{8192};
        Shader::ObjectPool<Shader::IR::Block> block_pool{512};
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool{512};
        auto ir = TranslateReviewedComputeIR(
            inst_pool, block_pool, flow_pool, env, host_info);
        if (ir.stage != Shader::Stage::Compute || ir.blocks.empty())
            return {NativeSpirvStatus::CompilationFailed, {}};
        // The common translator's NVN helper skips write-only global
        // shaders. Reconstruct ONLY provable Compute CBUF0 descriptors
        // before the shared SPIR-V global load/store helper is emitted.
        // Fail closed on sparse slot maps, absent CBUF0, or pre-existing
        // descriptors of uncertain order; the NS1 translator is untouched.
        if (!NormalizeNativeQmdGlobalStoreOnlyInfo(ir.info, qmd_enabled_cbuf_mask))
            return {NativeSpirvStatus::CompilationFailed, {}};
        Shader::Maxwell::ConvertLegacyToGeneric(ir, runtime_info);
        // QMD3 has eight physical Compute CBUF slots; do not emit the
        // generic fourteen-slot OpSwitch or bind phantom descriptors.
        if (!NormalizeNativeQmdIndirectCbufIR(ir, qmd_enabled_cbuf_mask))
            return {NativeSpirvStatus::CompilationFailed, {}};
        // An unresolved global atomic has no SPIR-V emitter. The shared
        // storage backend also implements some 64-bit atomics as
        // non-atomic stores when the Vulkan feature is missing. Never
        // publish such a QMD3 shader as an executing GPU atomic.
        if (!NativeQmdAtomicStorageIRAdmissible(ir, profile))
            return {NativeSpirvStatus::CompilationFailed, {}};
        // The shared SPIR-V atomic emitter has no runtime check against
        // the actual bound SSBO size. Collect ONLY constant, word-aligned
        // atomic spans, then authenticate them against real QMD3 descriptor
        // lengths after resource capture. Dynamic offsets remain fail-closed.
        const auto atomic_spans = CollectNativeQmdAtomicSpans(ir);
        if (!atomic_spans)
            return {NativeSpirvStatus::CompilationFailed, {}};
        // The shared SPIR-V global load/store accessor directly reads
        // cbufs[bank].U32x2 (address) and .U32 (length). Those uniform
        // types must exist even when the Ampere guest's direct LDC usage
        // never referenced them. Do this ONLY for the native Compute IR;
        // the Switch 1 translator and renderer remain untouched.
        if (ir.info.uses_global_memory) {
            if (!profile.support_int64 || !profile.support_descriptor_aliasing ||
                ir.info.storage_buffers_descriptors.empty() ||
                ir.info.storage_buffers_descriptors.size() >
                    ir.info.nvn_buffer_used.size())
                return {NativeSpirvStatus::CompilationFailed, {}};
            bool any_used = false;
            for (std::size_t i = 0;
                 i < ir.info.storage_buffers_descriptors.size(); ++i) {
                const auto& storage = ir.info.storage_buffers_descriptors[i];
                if (storage.count != 1)
                    return {NativeSpirvStatus::CompilationFailed, {}};
                if (!ir.info.nvn_buffer_used.test(i)) continue;
                // Global address is emitted as a U32x2 at offset / 8;
                // the QMD descriptor reader instead uses the literal byte
                // offset. Reject +4 aliases before producing SPIR-V.
                if (storage.cbuf_offset & 7U)
                    return {NativeSpirvStatus::CompilationFailed, {}};
                any_used = true;
                bool bank_declared = false;
                for (const auto& cbuf : ir.info.constant_buffer_descriptors) {
                    if (storage.cbuf_index >= cbuf.index &&
                        storage.cbuf_index - cbuf.index < cbuf.count)
                        bank_declared = true;
                }
                if (!bank_declared)
                    return {NativeSpirvStatus::CompilationFailed, {}};
            }
            if (!any_used)
                return {NativeSpirvStatus::CompilationFailed, {}};
            ir.info.used_constant_buffer_types |=
                Shader::IR::Type::U32 | Shader::IR::Type::U32x2;
        }
        Shader::Backend::Bindings bindings{initial_bindings};
        NativeSpirvCbufBoundsScope native_bounds;
        auto words = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime_info, ir, bindings);
        if (words.size() <= 5 || words.size() > 262144 ||
            words[0] != 0x07230203U || words[3] == 0)
            return {NativeSpirvStatus::InvalidModule, {}};
        return {NativeSpirvStatus::Ready, std::move(words),
                std::move(ir.info), bindings, Shader::Stage::Compute,
                std::move(*atomic_spans)};
    } catch (const std::exception&) {
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
