// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/frontend/ir/modifiers.h"

namespace Shader::Backend::SPIRV {
namespace {
Id Image(EmitContext& ctx, IR::TextureInstInfo info) {
    if (info.type == TextureType::Buffer) {
        const ImageBufferDefinition def{ctx.image_buffers.at(info.descriptor_index)};
        return def.id;
    } else {
        const ImageDefinition def{ctx.images.at(info.descriptor_index)};
        return def.id;
    }
}

std::pair<Id, Id> AtomicArgs(EmitContext& ctx) {
    const Id scope{ctx.Const(static_cast<u32>(spv::Scope::Device))};
    const Id semantics{ctx.u32_zero_value};
    return {scope, semantics};
}

Id ImageAtomicU32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords, Id value,
                  Id (Sirit::Module::*atomic_func)(Id, Id, Id, Id, Id)) {
    if (!index.IsImmediate() || index.U32() != 0) {
        // TODO: handle layers
        throw NotImplementedException("Image indexing");
    }
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const Id image{Image(ctx, info)};
    const Id pointer{ctx.OpImageTexelPointer(ctx.image_u32, image, coords, ctx.Const(0U))};
    const auto [scope, semantics]{AtomicArgs(ctx)};
    return (ctx.*atomic_func)(ctx.U32[1], pointer, scope, semantics, value);
}

enum class ImageCasOperation {
    Increment,
    Decrement,
};

Id ImageAtomicCasU32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords, Id limit,
                     ImageCasOperation operation) {
    if (!index.IsImmediate() || index.U32() != 0) {
        // Descriptor-array indexing is handled separately; do not conflate it
        // with the Inc/Dec CAS semantics.
        throw NotImplementedException("Image indexing");
    }
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const Id image{Image(ctx, info)};
    const Id pointer{ctx.OpImageTexelPointer(ctx.image_u32, image, coords, ctx.Const(0U))};
    const auto [scope, semantics]{AtomicArgs(ctx)};

    // OpImageTexelPointer results are restricted to atomic operations, so use
    // an atomic load for the retry comparator instead of the SSBO CAS helper's
    // ordinary OpLoad. Keep the loop inline: Vulkan logical addressing does
    // not permit a StorageClass::Image pointer as a function-call argument.
    const Id loop_header{ctx.OpLabel()};
    const Id continue_block{ctx.OpLabel()};
    const Id merge_block{ctx.OpLabel()};
    ctx.OpBranch(loop_header);
    ctx.AddLabel(loop_header);
    ctx.OpLoopMerge(merge_block, continue_block, spv::LoopControlMask::MaskNone);
    ctx.OpBranch(continue_block);

    ctx.AddLabel(continue_block);
    const Id old_value{ctx.OpAtomicLoad(ctx.U32[1], pointer, scope, semantics)};
    Id new_value{};
    switch (operation) {
    case ImageCasOperation::Increment: {
        // NVIDIA atomicInc: old >= limit ? 0 : old + 1.
        const Id wrapped{ctx.OpUGreaterThanEqual(ctx.U1, old_value, limit)};
        const Id incremented{ctx.OpIAdd(ctx.U32[1], old_value, ctx.Const(1U))};
        new_value = ctx.OpSelect(ctx.U32[1], wrapped, ctx.u32_zero_value, incremented);
        break;
    }
    case ImageCasOperation::Decrement: {
        // NVIDIA atomicDec: old == 0 || old > limit ? limit : old - 1.
        const Id zero{ctx.OpIEqual(ctx.U1, old_value, ctx.u32_zero_value)};
        const Id above_limit{ctx.OpUGreaterThan(ctx.U1, old_value, limit)};
        const Id wrapped{ctx.OpLogicalOr(ctx.U1, zero, above_limit)};
        const Id decremented{ctx.OpISub(ctx.U32[1], old_value, ctx.Const(1U))};
        new_value = ctx.OpSelect(ctx.U32[1], wrapped, limit, decremented);
        break;
    }
    }

    const Id atomic_result{ctx.OpAtomicCompareExchange(ctx.U32[1], pointer, scope, semantics,
                                                       semantics, new_value, old_value)};
    const Id success{ctx.OpIEqual(ctx.U1, atomic_result, old_value)};
    ctx.OpBranchConditional(success, merge_block, loop_header);

    ctx.AddLabel(merge_block);
    return atomic_result;
}
} // Anonymous namespace

Id EmitImageAtomicIAdd32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                         Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicIAdd);
}

Id EmitImageAtomicSMin32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                         Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicSMin);
}

Id EmitImageAtomicUMin32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                         Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicUMin);
}

Id EmitImageAtomicSMax32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                         Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicSMax);
}

Id EmitImageAtomicUMax32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                         Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicUMax);
}

Id EmitImageAtomicInc32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                        Id limit) {
    return ImageAtomicCasU32(ctx, inst, index, coords, limit, ImageCasOperation::Increment);
}

Id EmitImageAtomicDec32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                        Id limit) {
    return ImageAtomicCasU32(ctx, inst, index, coords, limit, ImageCasOperation::Decrement);
}

Id EmitImageAtomicAnd32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                        Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicAnd);
}

Id EmitImageAtomicOr32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                       Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicOr);
}

Id EmitImageAtomicXor32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                        Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicXor);
}

Id EmitImageAtomicExchange32(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                             Id value) {
    return ImageAtomicU32(ctx, inst, index, coords, value, &Sirit::Module::OpAtomicExchange);
}

Id EmitBindlessImageAtomicIAdd32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicSMin32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicUMin32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicSMax32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicUMax32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicInc32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicDec32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicAnd32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicOr32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicXor32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBindlessImageAtomicExchange32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicIAdd32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicSMin32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicUMin32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicSMax32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicUMax32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicInc32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicDec32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicAnd32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicOr32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicXor32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitBoundImageAtomicExchange32(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

} // namespace Shader::Backend::SPIRV
