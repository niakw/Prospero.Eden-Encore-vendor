// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <tuple>
#include <utility>

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/frontend/ampere/native_indirect_cbuf.h"

namespace Shader::Backend::SPIRV {
namespace {
template <typename... Args>
Id AttrPointer(EmitContext& ctx, Id pointer_type, Id vertex, Id base, Args&&... args) {
    switch (ctx.stage) {
    case Stage::TessellationControl:
    case Stage::TessellationEval:
    case Stage::Geometry:
        return ctx.OpAccessChain(pointer_type, base, vertex, std::forward<Args>(args)...);
    default:
        return ctx.OpAccessChain(pointer_type, base, std::forward<Args>(args)...);
    }
}

template <typename... Args>
Id OutputAccessChain(EmitContext& ctx, Id result_type, Id base, Args&&... args) {
    if (ctx.stage == Stage::TessellationControl) {
        const Id invocation_id{ctx.OpLoad(ctx.U32[1], ctx.invocation_id)};
        return ctx.OpAccessChain(result_type, base, invocation_id, std::forward<Args>(args)...);
    } else {
        return ctx.OpAccessChain(result_type, base, std::forward<Args>(args)...);
    }
}

struct OutAttr {
    OutAttr(Id pointer_) : pointer{pointer_} {}
    OutAttr(Id pointer_, Id type_) : pointer{pointer_}, type{type_} {}

    Id pointer{};
    Id type{};
};

std::optional<OutAttr> OutputAttrPointer(EmitContext& ctx, IR::Attribute attr) {
    if (IR::IsGeneric(attr)) {
        const u32 index{IR::GenericAttributeIndex(attr)};
        const u32 element{IR::GenericAttributeElement(attr)};
        const GenericElementInfo& info{ctx.output_generics.at(index).at(element)};
        if (info.num_components == 1) {
            return info.id;
        } else {
            const u32 index_element{element - info.first_element};
            const Id index_id{ctx.Const(index_element)};
            return OutputAccessChain(ctx, ctx.output_f32, info.id, index_id);
        }
    }

    switch (attr) {
    case IR::Attribute::PointSize:
        return ctx.output_point_size;
    case IR::Attribute::PositionX:
    case IR::Attribute::PositionY:
    case IR::Attribute::PositionZ:
    case IR::Attribute::PositionW: {
        const u32 element{static_cast<u32>(attr) % 4};
        const Id element_id{ctx.Const(element)};
        return OutputAccessChain(ctx, ctx.output_f32, ctx.output_position, element_id);
    }
    case IR::Attribute::ClipDistance0:
    case IR::Attribute::ClipDistance1:
    case IR::Attribute::ClipDistance2:
    case IR::Attribute::ClipDistance3:
    case IR::Attribute::ClipDistance4:
    case IR::Attribute::ClipDistance5:
    case IR::Attribute::ClipDistance6:
    case IR::Attribute::ClipDistance7: {
        const u32 base{static_cast<u32>(IR::Attribute::ClipDistance0)};
        const u32 index{static_cast<u32>(attr) - base};
        if (index >= ctx.profile.max_user_clip_distances) {
            LOG_WARNING(Shader, "Ignoring clip distance store {} >= {} supported", index,
                        ctx.profile.max_user_clip_distances);
            return std::nullopt;
        }
        const Id clip_num{ctx.Const(index)};
        return OutputAccessChain(ctx, ctx.output_f32, ctx.clip_distances, clip_num);
    }
    case IR::Attribute::Layer:
        if (ctx.profile.support_viewport_index_layer_non_geometry ||
            ctx.stage == Shader::Stage::Geometry) {
            return OutAttr{ctx.layer, ctx.U32[1]};
        }
        return std::nullopt;
    case IR::Attribute::ViewportIndex:
        if (!ctx.profile.support_multi_viewport) {
            LOG_WARNING(Shader, "Ignoring viewport index store on non-supporting driver");
            return std::nullopt;
        }
        if (ctx.profile.support_viewport_index_layer_non_geometry ||
            ctx.stage == Shader::Stage::Geometry) {
            return OutAttr{ctx.viewport_index, ctx.U32[1]};
        }
        return std::nullopt;
    case IR::Attribute::ViewportMask:
        if (!ctx.profile.support_viewport_mask) {
            return std::nullopt;
        }
        return OutAttr{ctx.OpAccessChain(ctx.output_u32, ctx.viewport_mask, ctx.u32_zero_value),
                       ctx.U32[1]};
    default:
        throw NotImplementedException("Read attribute {}", attr);
    }
}

Id GetCbuf(EmitContext& ctx, Id result_type, Id UniformDefinitions::*member_ptr, u32 element_size,
           const IR::Value& binding, const IR::Value& offset, const Id indirect_func,
           u32 native_access_bytes, bool next_vector_for_pair = false) {
    Id buffer_offset;
    const Id uniform_type{ctx.uniform_types.*member_ptr};
    if (offset.IsImmediate()) {
        // Hardware been proved to read the aligned offset (e.g. LDC.U32 at 6 will read offset 4)
        const Id imm_offset{ctx.Const(offset.U32() / element_size)};
        buffer_offset = imm_offset;
    } else if (element_size > 1) {
        const u32 log2_element_size{static_cast<u32>(std::countr_zero(element_size))};
        const Id shift{ctx.Const(log2_element_size)};
        buffer_offset = ctx.OpShiftRightLogical(ctx.U32[1], ctx.Def(offset), shift);
    } else {
        buffer_offset = ctx.Def(offset);
    }
    if (Shader::Ampere::NativeSpirvCbufBoundsActive() && offset.IsImmediate() &&
        (offset.U32() > 0x10000U - native_access_bytes ||
         (native_access_bytes == 8 && (offset.U32() & 3U)))) {
        // Two guest words require a word-aligned 64-bit span. An offset
        // at 4/12 is valid: a second 8-byte vector carries its high word.
        // Sub-word native U32x2 behavior has not been authenticated.
        return ctx.ConstantNull(result_type);
    }
    if (next_vector_for_pair) {
        // The second guest 32-bit word may occupy the following U32x2
        // (8 bytes) or U32x4 (16 bytes) uniform vector. The first word
        // always uses the original vector; bump only the second vector's
        // array index at its last component boundary.
        if (offset.IsImmediate()) {
            if ((offset.U32() & (element_size - 1U)) >= element_size - 4U)
                buffer_offset = ctx.OpIAdd(ctx.U32[1], buffer_offset, ctx.Const(1U));
        } else {
            const Id last_word{ctx.OpUGreaterThanEqual(
                ctx.U1, ctx.OpBitwiseAnd(ctx.U32[1], ctx.Def(offset),
                                        ctx.Const(element_size - 1U)),
                ctx.Const(element_size - 4U))};
            const Id delta{ctx.OpSelect(ctx.U32[1], last_word,
                                        ctx.Const(1U), ctx.u32_zero_value)};
            buffer_offset = ctx.OpIAdd(ctx.U32[1], buffer_offset, delta);
        }
    }
    if (Shader::Ampere::NativeSpirvCbufBoundsActive() && !offset.IsImmediate()) {
        // The old index-only test silently accepted a U32x2 read at
        // 0xfffc by rounding down to the in-range row at 0xfff8.
        // Validate ORIGINAL guest bytes, not only the rounded SPIR-V
        // element index. U8 through an aligned U32x4 must still admit
        // byte 0xffff, while a genuine U32x2 cannot cross that boundary.
        const Id valid_span{ctx.OpULessThanEqual(
            ctx.U1, ctx.Def(offset), ctx.Const(0x10000U - native_access_bytes))};
        const Id valid_offset{native_access_bytes != 8 ? valid_span :
            ctx.OpLogicalAnd(ctx.U1, valid_span,
                ctx.OpIEqual(ctx.U1,
                    ctx.OpBitwiseAnd(ctx.U32[1], ctx.Def(offset), ctx.Const(3U)),
                    ctx.u32_zero_value))};
        // Both the indirect QMD3 bank switch and the direct-bank bounds
        // guard below reject this sentinel before creating a guest pointer.
        // No change to the legacy Maxwell compiler path.
        buffer_offset = ctx.OpSelect(ctx.U32[1], valid_offset,
                                     buffer_offset, ctx.Const(~u32{0}));
    }
    if (!binding.IsImmediate()) {
        return ctx.OpFunctionCall(result_type, indirect_func, ctx.Def(binding), buffer_offset);
    }

    // Compile the authentic QMD3 bank ABI, not the larger NS1 synthetic
    // uniform table. Resource-plan validation happens AFTER SPIR-V emission;
    // an untrusted immediate bank must not index cbufs out of bounds or
    // compile a shader that the eight-bank QMD cannot bind.
    if (Shader::Ampere::NativeSpirvCbufBoundsActive() && binding.U32() >= 8) {
        throw NotImplementedException(
            "Native QMD3 CBUF bank {} outside eight-bank ABI", binding.U32());
    }
    const Id cbuf{ctx.cbufs[binding.U32()].*member_ptr};
    // A numerically valid QMD bank may still have no reflected descriptor.
    // Do not emit an OpAccessChain through an undeclared SPIR-V uniform;
    // reject malformed native IR before Vulkan pipeline publication.
    if (Shader::Ampere::NativeSpirvCbufBoundsActive() && !ValidId(cbuf)) {
        throw NotImplementedException(
            "Native QMD3 CBUF bank {} has no declared uniform", binding.U32());
    }
    if (!Shader::Ampere::NativeSpirvCbufBoundsActive()) {
        // Byte-for-byte equivalent to the original Maxwell SPIR-V path.
        // It remains active for Switch 1 even when NS2 source overlays are
        // compiled into the SAME shader_recompiler binary.
        const Id access_chain{ctx.OpAccessChain(uniform_type, cbuf, ctx.u32_zero_value, buffer_offset)};
        const Id val = ctx.OpLoad(result_type, access_chain);

        if (offset.IsImmediate() || !ctx.profile.has_broken_robust) {
            return val;
        }

        const Id cond = ctx.OpULessThanEqual(ctx.TypeBool(), buffer_offset, ctx.Const(0xFFFFu));
        const Id zero = ctx.ConstantNull(result_type);
        return ctx.OpSelect(result_type, cond, val, zero);
    }
    if (offset.IsImmediate()) {
        // Maxwell's signed LDC immediate can fold to a large U32 constant
        // (e.g. RZ - 4). Never let the generic used-size collector's 32-bit
        // addition overflow create a valid-looking native out-of-bounds load.
        // Offsets inside the 64-KiB window are aligned in element units.
        if (offset.U32() >= 0x10000U)
            return ctx.ConstantNull(result_type);
        // The native QMD3 resource planner checks the reflected in-range
        // constant offset against the real launch buffer size at dispatch.
        const Id ptr{ctx.OpAccessChain(uniform_type, cbuf, ctx.u32_zero_value,
                                      buffer_offset)};
        return ctx.OpLoad(result_type, ptr);
    }

    // NS2 native variant only. The original Maxwell path emits an unchecked
    // direct-bank OpAccessChain and may rely on device robustness even after
    // the subsequent zero-select. Bounds-check BEFORE pointer creation:
    // OpSelect at the result is too late to prevent a speculative OOB load.
    // 'buffer_offset' is already in elements (1/2/4/8/16 bytes), and the
    // shader-CBUF uniform array always has 65536/element_size entries.
    const Id in_bounds{ctx.OpULessThan(
        ctx.U1, buffer_offset, ctx.Const(0x10000U / element_size))};
    const Id safe_offset{ctx.OpSelect(ctx.U32[1], in_bounds, buffer_offset,
                                     ctx.u32_zero_value)};
    const Id ptr{ctx.OpAccessChain(uniform_type, cbuf, ctx.u32_zero_value,
                                  safe_offset)};
    const Id value{ctx.OpLoad(result_type, ptr)};
    return ctx.OpSelect(result_type, in_bounds, value,
                        ctx.ConstantNull(result_type));
}

Id GetCbufU32(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset,
              u32 native_access_bytes = 4) {
    return GetCbuf(ctx, ctx.U32[1], &UniformDefinitions::U32, sizeof(u32), binding, offset,
                   ctx.load_const_func_u32, native_access_bytes);
}

Id GetCbufU32x4(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset,
                u32 native_access_bytes = 4, bool next_vector_for_pair = false) {
    return GetCbuf(ctx, ctx.U32[4], &UniformDefinitions::U32x4, sizeof(u32[4]), binding, offset,
                   ctx.load_const_func_u32x4, native_access_bytes, next_vector_for_pair);
}

Id GetCbufElement(EmitContext& ctx, Id vector, const IR::Value& offset, u32 index_offset,
                  u32 vector_components = 4) {
    if (offset.IsImmediate()) {
        const u32 element{(offset.U32() / 4) % vector_components + index_offset};
        return ctx.OpCompositeExtract(ctx.U32[1], vector,
            Shader::Ampere::NativeSpirvCbufBoundsActive()
                ? element % vector_components : element);
    }
    const Id shift{ctx.OpShiftRightLogical(ctx.U32[1], ctx.Def(offset), ctx.Const(2u))};
    Id element{ctx.OpBitwiseAnd(ctx.U32[1], shift, ctx.Const(vector_components - 1U))};
    if (index_offset > 0) {
        element = ctx.OpIAdd(ctx.U32[1], element, ctx.Const(index_offset));
    }
    // The native pair caller may select the next uniform vector when a
    // source U32x2 straddles its boundary. Wrap the component *inside*
    // that vector. NS1's original four-component indexing is preserved.
    if (index_offset && Shader::Ampere::NativeSpirvCbufBoundsActive())
        element = ctx.OpBitwiseAnd(ctx.U32[1], element,
                                   ctx.Const(vector_components - 1U));
    return ctx.OpVectorExtractDynamic(ctx.U32[1], vector, element);
}
} // Anonymous namespace

void EmitGetRegister(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitSetRegister(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetPred(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitSetPred(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitSetGotoVariable(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetGotoVariable(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitSetIndirectBranchVariable(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetIndirectBranchVariable(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitGetCbufU8(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset) {
    if (ctx.profile.support_descriptor_aliasing && ctx.profile.support_int8 &&
        ctx.profile.support_uniform_and_storage_buffer_8bit) {
        const Id load{GetCbuf(ctx, ctx.U8, &UniformDefinitions::U8, sizeof(u8), binding, offset,
                              ctx.load_const_func_u8, 1)};
        return ctx.OpUConvert(ctx.U32[1], load);
    }
    Id element{};
    if (ctx.profile.support_descriptor_aliasing) {
        element = GetCbufU32(ctx, binding, offset, 1);
    } else {
        const Id vector{GetCbufU32x4(ctx, binding, offset, 1)};
        element = GetCbufElement(ctx, vector, offset, 0u);
    }
    const Id bit_offset{ctx.BitOffset8(offset)};
    return ctx.OpBitFieldUExtract(ctx.U32[1], element, bit_offset, ctx.Const(8u));
}

Id EmitGetCbufS8(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset) {
    if (ctx.profile.support_descriptor_aliasing && ctx.profile.support_int8 &&
        ctx.profile.support_uniform_and_storage_buffer_8bit) {
        const Id load{GetCbuf(ctx, ctx.S8, &UniformDefinitions::S8, sizeof(s8), binding, offset,
                              ctx.load_const_func_u8, 1)};
        return ctx.OpSConvert(ctx.U32[1], load);
    }
    Id element{};
    if (ctx.profile.support_descriptor_aliasing) {
        element = GetCbufU32(ctx, binding, offset, 1);
    } else {
        const Id vector{GetCbufU32x4(ctx, binding, offset, 1)};
        element = GetCbufElement(ctx, vector, offset, 0u);
    }
    const Id bit_offset{ctx.BitOffset8(offset)};
    return ctx.OpBitFieldSExtract(ctx.U32[1], element, bit_offset, ctx.Const(8u));
}

Id EmitGetCbufU16(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset) {
    if (ctx.profile.support_descriptor_aliasing && ctx.profile.support_int16 &&
        ctx.profile.support_uniform_and_storage_buffer_16bit) {
        const Id load{GetCbuf(ctx, ctx.U16, &UniformDefinitions::U16, sizeof(u16), binding, offset,
                              ctx.load_const_func_u16, 2)};
        return ctx.OpUConvert(ctx.U32[1], load);
    }
    Id element{};
    if (ctx.profile.support_descriptor_aliasing) {
        element = GetCbufU32(ctx, binding, offset, 2);
    } else {
        const Id vector{GetCbufU32x4(ctx, binding, offset, 2)};
        element = GetCbufElement(ctx, vector, offset, 0u);
    }
    const Id bit_offset{ctx.BitOffset16(offset)};
    return ctx.OpBitFieldUExtract(ctx.U32[1], element, bit_offset, ctx.Const(16u));
}

Id EmitGetCbufS16(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset) {
    if (ctx.profile.support_descriptor_aliasing && ctx.profile.support_int16 &&
        ctx.profile.support_uniform_and_storage_buffer_16bit) {
        const Id load{GetCbuf(ctx, ctx.S16, &UniformDefinitions::S16, sizeof(s16), binding, offset,
                              ctx.load_const_func_u16, 2)};
        return ctx.OpSConvert(ctx.U32[1], load);
    }
    Id element{};
    if (ctx.profile.support_descriptor_aliasing) {
        element = GetCbufU32(ctx, binding, offset, 2);
    } else {
        const Id vector{GetCbufU32x4(ctx, binding, offset, 2)};
        element = GetCbufElement(ctx, vector, offset, 0u);
    }
    const Id bit_offset{ctx.BitOffset16(offset)};
    return ctx.OpBitFieldSExtract(ctx.U32[1], element, bit_offset, ctx.Const(16u));
}

Id EmitGetCbufU32(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset) {
    if (ctx.profile.support_descriptor_aliasing) {
        return GetCbufU32(ctx, binding, offset);
    } else {
        const Id vector{GetCbufU32x4(ctx, binding, offset)};
        return GetCbufElement(ctx, vector, offset, 0u);
    }
}

Id EmitGetCbufF32(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset) {
    if (ctx.profile.support_descriptor_aliasing) {
        return GetCbuf(ctx, ctx.F32[1], &UniformDefinitions::F32, sizeof(f32), binding, offset,
                       ctx.load_const_func_f32, 4);
    } else {
        const Id vector{GetCbufU32x4(ctx, binding, offset)};
        return ctx.OpBitcast(ctx.F32[1], GetCbufElement(ctx, vector, offset, 0u));
    }
}

Id EmitGetCbufU32x2(EmitContext& ctx, const IR::Value& binding, const IR::Value& offset) {
    if (ctx.profile.support_descriptor_aliasing) {
        if (Shader::Ampere::NativeSpirvCbufBoundsActive()) {
            // Native guest U32x2 is two consecutive 32-bit words, not a
            // rounded-down 64-bit vector. Reading at byte 4 (or 12) must
            // combine the last component of one U32x2 uniform vector
            // with the first component of the NEXT. Both loads preserve
            // the original 8-byte guest-span/authenticated bank guard.
            const Id first_row{GetCbuf(ctx, ctx.U32[2], &UniformDefinitions::U32x2,
                sizeof(u32[2]), binding, offset, ctx.load_const_func_u32x2, 8)};
            const Id second_row{GetCbuf(ctx, ctx.U32[2], &UniformDefinitions::U32x2,
                sizeof(u32[2]), binding, offset, ctx.load_const_func_u32x2, 8, true)};
            return ctx.OpCompositeConstruct(ctx.U32[2],
                GetCbufElement(ctx, first_row, offset, 0u, 2),
                GetCbufElement(ctx, second_row, offset, 1u, 2));
        }
        return GetCbuf(ctx, ctx.U32[2], &UniformDefinitions::U32x2, sizeof(u32[2]), binding, offset,
                       ctx.load_const_func_u32x2, 8);
    } else {
        if (Shader::Ampere::NativeSpirvCbufBoundsActive()) {
            // Source U32x2 can straddle two aligned 16-byte uniform rows.
            // This must not silently extract nonexistent component 4.
            // The first/second rows share one authenticated 8-byte guest
            // span and are independently checked before OpAccessChain.
            const Id first_row{GetCbufU32x4(ctx, binding, offset, 8)};
            const Id second_row{GetCbufU32x4(ctx, binding, offset, 8, true)};
            return ctx.OpCompositeConstruct(ctx.U32[2],
                GetCbufElement(ctx, first_row, offset, 0u),
                GetCbufElement(ctx, second_row, offset, 1u));
        }
        const Id vector{GetCbufU32x4(ctx, binding, offset)};
        return ctx.OpCompositeConstruct(ctx.U32[2], GetCbufElement(ctx, vector, offset, 0u),
                                        GetCbufElement(ctx, vector, offset, 1u));
    }
}

Id EmitGetAttribute(EmitContext& ctx, IR::Attribute attr, Id vertex) {
    const u32 element{static_cast<u32>(attr) % 4};
    if (IR::IsGeneric(attr)) {
        const u32 index{IR::GenericAttributeIndex(attr)};
        const auto& generic{ctx.input_generics.at(index)};
        if (!ValidId(generic.id)) {
            // Attribute is disabled or varying component is not written
            return ctx.Const(element == 3 ? 1.0f : 0.0f);
        }
        const Id pointer{
            AttrPointer(ctx, generic.pointer_type, vertex, generic.id, ctx.Const(element))};
        const Id value{ctx.OpLoad(generic.component_type, pointer)};
        return [&ctx, generic, value]() {
            switch (generic.load_op) {
            case InputGenericLoadOp::Bitcast:
                return ctx.OpBitcast(ctx.F32[1], value);
            case InputGenericLoadOp::SToF:
                return ctx.OpConvertSToF(ctx.F32[1], value);
            case InputGenericLoadOp::UToF:
                return ctx.OpConvertUToF(ctx.F32[1], value);
            default:
                return value;
            };
        }();
    }
    switch (attr) {
    case IR::Attribute::PrimitiveId:
        return ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(ctx.U32[1], ctx.primitive_id));
    case IR::Attribute::Layer:
        return ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(ctx.U32[1], ctx.layer));
    case IR::Attribute::PositionX:
    case IR::Attribute::PositionY:
    case IR::Attribute::PositionZ:
    case IR::Attribute::PositionW:
        return ctx.OpLoad(
            ctx.F32[1],
            ctx.need_input_position_indirect
                ? AttrPointer(ctx, ctx.input_f32, vertex, ctx.input_position, ctx.u32_zero_value,
                              ctx.Const(element))
                : AttrPointer(ctx, ctx.input_f32, vertex, ctx.input_position, ctx.Const(element)));
    case IR::Attribute::InstanceId:
        if (ctx.profile.support_vertex_instance_id) {
            return ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(ctx.U32[1], ctx.instance_id));
        } else {
            const Id index{ctx.OpLoad(ctx.U32[1], ctx.instance_index)};
            const Id base{ctx.OpLoad(ctx.U32[1], ctx.base_instance)};
            return ctx.OpBitcast(ctx.F32[1], ctx.OpISub(ctx.U32[1], index, base));
        }
    case IR::Attribute::VertexId:
        if (ctx.profile.support_vertex_instance_id) {
            return ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(ctx.U32[1], ctx.vertex_id));
        } else {
            return ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(ctx.U32[1], ctx.vertex_index));
        }
    case IR::Attribute::BaseInstance:
        return ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(ctx.U32[1], ctx.base_instance));
    case IR::Attribute::BaseVertex:
        return ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(ctx.U32[1], ctx.base_vertex));
    case IR::Attribute::DrawID:
        return ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(ctx.U32[1], ctx.draw_index));
    case IR::Attribute::FrontFace:
        return ctx.OpSelect(ctx.F32[1], ctx.OpLoad(ctx.U1, ctx.front_face),
                            ctx.OpBitcast(ctx.F32[1], ctx.Const((std::numeric_limits<u32>::max)())),
                            ctx.f32_zero_value);
    case IR::Attribute::PointSpriteS:
        return ctx.OpLoad(ctx.F32[1],
                          ctx.OpAccessChain(ctx.input_f32, ctx.point_coord, ctx.u32_zero_value));
    case IR::Attribute::PointSpriteT:
        return ctx.OpLoad(ctx.F32[1],
                          ctx.OpAccessChain(ctx.input_f32, ctx.point_coord, ctx.Const(1U)));
    case IR::Attribute::TessellationEvaluationPointU:
        return ctx.OpLoad(ctx.F32[1],
                          ctx.OpAccessChain(ctx.input_f32, ctx.tess_coord, ctx.u32_zero_value));
    case IR::Attribute::TessellationEvaluationPointV:
        return ctx.OpLoad(ctx.F32[1],
                          ctx.OpAccessChain(ctx.input_f32, ctx.tess_coord, ctx.Const(1U)));
    default:
        throw NotImplementedException("Read attribute {}", attr);
    }
}

Id EmitGetAttributeU32(EmitContext& ctx, IR::Attribute attr, Id) {
    switch (attr) {
    case IR::Attribute::PrimitiveId:
        return ctx.OpLoad(ctx.U32[1], ctx.primitive_id);
    case IR::Attribute::InstanceId:
        if (ctx.profile.support_vertex_instance_id) {
            return ctx.OpLoad(ctx.U32[1], ctx.instance_id);
        } else {
            const Id index{ctx.OpLoad(ctx.U32[1], ctx.instance_index)};
            const Id base{ctx.OpLoad(ctx.U32[1], ctx.base_instance)};
            return ctx.OpISub(ctx.U32[1], index, base);
        }
    case IR::Attribute::VertexId:
        if (ctx.profile.support_vertex_instance_id) {
            return ctx.OpLoad(ctx.U32[1], ctx.vertex_id);
        } else {
            return ctx.OpLoad(ctx.U32[1], ctx.vertex_index);
        }
    case IR::Attribute::BaseInstance:
        return ctx.OpLoad(ctx.U32[1], ctx.base_instance);
    case IR::Attribute::BaseVertex:
        return ctx.OpLoad(ctx.U32[1], ctx.base_vertex);
    case IR::Attribute::DrawID:
        return ctx.OpLoad(ctx.U32[1], ctx.draw_index);
    default:
        throw NotImplementedException("Read U32 attribute {}", attr);
    }
}

void EmitSetAttribute(EmitContext& ctx, IR::Attribute attr, Id value, [[maybe_unused]] Id vertex) {
    const std::optional<OutAttr> output{OutputAttrPointer(ctx, attr)};
    if (!output) {
        return;
    }
    if (Sirit::ValidId(output->type)) {
        value = ctx.OpBitcast(output->type, value);
    }

    static constexpr IR::Attribute cd0 = IR::Attribute::ClipDistance0;
    static constexpr IR::Attribute cd7 = IR::Attribute::ClipDistance7;

    if (attr >= cd0 && attr <= cd7) {
        const u32 idx = (u32) attr - (u32) cd0;
        clip_distance_written.set(idx);
    }
    ctx.OpStore(output->pointer, value);
}

Id EmitGetAttributeIndexed(EmitContext& ctx, Id offset, Id vertex) {
    switch (ctx.stage) {
    case Stage::TessellationControl:
    case Stage::TessellationEval:
    case Stage::Geometry:
        return ctx.OpFunctionCall(ctx.F32[1], ctx.indexed_load_func, offset, vertex);
    default:
        return ctx.OpFunctionCall(ctx.F32[1], ctx.indexed_load_func, offset);
    }
}

void EmitSetAttributeIndexed(EmitContext& ctx, Id offset, Id value, Id vertex) {
    if (ctx.stage == Stage::TessellationControl) {
        ctx.OpFunctionCall(ctx.void_id, ctx.indexed_store_func, offset, value, vertex);
    } else {
        ctx.OpFunctionCall(ctx.void_id, ctx.indexed_store_func, offset, value);
    }
}

Id EmitGetPatch(EmitContext& ctx, IR::Patch patch) {
    // Generic and built-in patch inputs exist only in tessellation stages.
    // A non-tessellation program must not emit an undeclared Input/Output
    // access chain (or silently select the TES Input storage class).
    const bool control{ctx.stage == Stage::TessellationControl};
    const bool evaluation{ctx.stage == Stage::TessellationEval};
    if (!control && !evaluation) {
        throw NotImplementedException("Tessellation patch read outside tessellation stage");
    }
    if (IR::IsGeneric(patch)) {
        const u32 index{IR::GenericPatchIndex(patch)};
        if (index >= ctx.patches.size() || !ValidId(ctx.patches.at(index))) {
            throw NotImplementedException("Undeclared generic tessellation patch {}", patch);
        }
        const Id element{ctx.Const(IR::GenericPatchElement(patch))};
        const Id type{control ? ctx.output_f32 : ctx.input_f32};
        const Id pointer{ctx.OpAccessChain(type, ctx.patches.at(index), element)};
        return ctx.OpLoad(ctx.F32[1], pointer);
    }
    Id variable{};
    Id element{};
    switch (patch) {
    case IR::Patch::TessellationLodLeft:
    case IR::Patch::TessellationLodTop:
    case IR::Patch::TessellationLodRight:
    case IR::Patch::TessellationLodBottom:
        variable = control ? ctx.output_tess_level_outer : ctx.input_tess_level_outer;
        element = ctx.Const(static_cast<u32>(patch) -
                            static_cast<u32>(IR::Patch::TessellationLodLeft));
        break;
    case IR::Patch::TessellationLodInteriorU:
        variable = control ? ctx.output_tess_level_inner : ctx.input_tess_level_inner;
        element = ctx.u32_zero_value;
        break;
    case IR::Patch::TessellationLodInteriorV:
        variable = control ? ctx.output_tess_level_inner : ctx.input_tess_level_inner;
        element = ctx.Const(1u);
        break;
    default:
        throw NotImplementedException("Unsupported tessellation patch {}", patch);
    }
    // Do not manufacture levels when a TCS program did not declare the
    // corresponding output. TES always declares the read-only inputs.
    if (!ValidId(variable)) {
        throw NotImplementedException("Undeclared tessellation patch {}", patch);
    }
    const Id element_ptr{ctx.OpAccessChain(
        control ? ctx.output_f32 : ctx.input_f32, variable, element)};
    return ctx.OpLoad(ctx.F32[1], element_ptr);
}

void EmitSetPatch(EmitContext& ctx, IR::Patch patch, Id value) {
    // Only a tessellation-control shader owns patch outputs. TES patch
    // variables are read-only inputs and may not be written by an IR error.
    if (ctx.stage != Stage::TessellationControl) {
        throw NotImplementedException("Tessellation patch write outside control stage");
    }
    const Id pointer{[&] {
        if (IR::IsGeneric(patch)) {
            const u32 index{IR::GenericPatchIndex(patch)};
            if (index >= ctx.patches.size() || !ValidId(ctx.patches.at(index))) {
                throw NotImplementedException("Undeclared generic tessellation patch {}", patch);
            }
            const Id element{ctx.Const(IR::GenericPatchElement(patch))};
            return ctx.OpAccessChain(ctx.output_f32, ctx.patches.at(index), element);
        }
        switch (patch) {
        case IR::Patch::TessellationLodLeft:
        case IR::Patch::TessellationLodRight:
        case IR::Patch::TessellationLodTop:
        case IR::Patch::TessellationLodBottom: {
            const u32 index{static_cast<u32>(patch) - u32(IR::Patch::TessellationLodLeft)};
            const Id index_id{ctx.Const(index)};
            return ctx.OpAccessChain(ctx.output_f32, ctx.output_tess_level_outer, index_id);
        }
        case IR::Patch::TessellationLodInteriorU:
            return ctx.OpAccessChain(ctx.output_f32, ctx.output_tess_level_inner,
                                     ctx.u32_zero_value);
        case IR::Patch::TessellationLodInteriorV:
            return ctx.OpAccessChain(ctx.output_f32, ctx.output_tess_level_inner, ctx.Const(1u));
        default:
            throw NotImplementedException("Patch {}", patch);
        }
    }()};
    ctx.OpStore(pointer, value);
}

void EmitSetFragColor(EmitContext& ctx, u32 index, u32 component, Id value) {
    const Id component_id{ctx.Const(component)};
    const AttributeType type{ctx.runtime_info.color_output_types[index]};
    if (type == AttributeType::Float) {
        const Id pointer{ctx.OpAccessChain(ctx.output_f32, ctx.frag_color.at(index), component_id)};
        ctx.OpStore(pointer, value);
    } else if (type == AttributeType::UnsignedInt) {
        const Id pointer{ctx.OpAccessChain(ctx.output_u32, ctx.frag_color.at(index), component_id)};
        ctx.OpStore(pointer, ctx.OpBitcast(ctx.U32[1], value));
    } else if (type == AttributeType::SignedInt) {
        const Id output_s32{ctx.TypePointer(spv::StorageClass::Output, ctx.S32[1])};
        const Id pointer{ctx.OpAccessChain(output_s32, ctx.frag_color.at(index), component_id)};
        ctx.OpStore(pointer, ctx.OpBitcast(ctx.S32[1], value));
    } else {
        // Disabled or unknown, treat as float
        const Id pointer{ctx.OpAccessChain(ctx.output_f32, ctx.frag_color.at(index), component_id)};
        ctx.OpStore(pointer, value);
    }
}

void EmitSetSampleMask(EmitContext& ctx, Id value) {
    const Id pointer{ctx.OpAccessChain(ctx.output_u32, ctx.sample_mask, ctx.u32_zero_value)};
    ctx.OpStore(pointer, value);
}

void EmitSetFragDepth(EmitContext& ctx, Id value) {
    if (!ctx.runtime_info.convert_depth_mode || ctx.profile.support_native_ndc) {
        ctx.OpStore(ctx.frag_depth, value);
        return;
    }
    const Id unit{ctx.Const(0.5f)};
    const Id new_depth{ctx.OpFma(ctx.F32[1], value, unit, unit)};
    ctx.OpStore(ctx.frag_depth, new_depth);
}

void EmitGetZFlag(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

void EmitGetSFlag(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

void EmitGetCFlag(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

void EmitGetOFlag(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

void EmitSetZFlag(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

void EmitSetSFlag(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

void EmitSetCFlag(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

void EmitSetOFlag(EmitContext&) {
    throw NotImplementedException("SPIR-V Instruction");
}

Id EmitWorkgroupId(EmitContext& ctx) {
    return ctx.OpLoad(ctx.U32[3], ctx.workgroup_id);
}

Id EmitLocalInvocationId(EmitContext& ctx) {
    return ctx.OpLoad(ctx.U32[3], ctx.local_invocation_id);
}

Id EmitInvocationId(EmitContext& ctx) {
    return ctx.OpLoad(ctx.U32[1], ctx.invocation_id);
}

Id EmitInvocationInfo(EmitContext& ctx) {
    switch (ctx.stage) {
    case Stage::TessellationControl:
    case Stage::TessellationEval:
        return ctx.OpShiftLeftLogical(ctx.U32[1], ctx.OpLoad(ctx.U32[1], ctx.patch_vertices_in), ctx.Const(16u));
    case Stage::Geometry:
        return ctx.OpShiftLeftLogical(ctx.U32[1], ctx.Const(InputTopologyVertices::vertices(ctx.runtime_info.input_topology)), ctx.Const(16u));
    default:
        LOG_WARNING(Shader, "(STUBBED) called");
        return ctx.Const(0x00ff0000u);
    }
}

Id EmitSampleId(EmitContext& ctx) {
    return ctx.OpLoad(ctx.U32[1], ctx.sample_id);
}

Id EmitIsHelperInvocation(EmitContext& ctx) {
    return ctx.OpLoad(ctx.U1, ctx.is_helper_invocation);
}

Id EmitSR_WScaleFactorXY(EmitContext& ctx) {
    LOG_WARNING(Shader, "(STUBBED) called");
    return ctx.Const(0x00ff0000u);
}

Id EmitSR_WScaleFactorZ(EmitContext& ctx) {
    LOG_WARNING(Shader, "(STUBBED) called");
    return ctx.Const(0x00ff0000u);
}

Id EmitYDirection(EmitContext& ctx) {
    return ctx.Const(ctx.runtime_info.y_negate ? -1.0f : 1.0f);
}

Id EmitResolutionDownFactor(EmitContext& ctx) {
    if (ctx.profile.unified_descriptor_binding) {
        const Id pointer_type{ctx.TypePointer(spv::StorageClass::PushConstant, ctx.F32[1])};
        const Id index{ctx.Const(ctx.rescaling_downfactor_member_index)};
        const Id pointer{ctx.OpAccessChain(pointer_type, ctx.rescaling_push_constants, index)};
        return ctx.OpLoad(ctx.F32[1], pointer);
    } else {
        const Id composite{ctx.OpLoad(ctx.F32[4], ctx.rescaling_uniform_constant)};
        return ctx.OpCompositeExtract(ctx.F32[1], composite, 2u);
    }
}

Id EmitRenderArea(EmitContext& ctx) {
    if (ctx.profile.unified_descriptor_binding) {
        const Id pointer_type{ctx.TypePointer(spv::StorageClass::PushConstant, ctx.F32[4])};
        const Id index{ctx.Const(ctx.render_are_member_index)};
        const Id pointer{ctx.OpAccessChain(pointer_type, ctx.render_area_push_constant, index)};
        return ctx.OpLoad(ctx.F32[4], pointer);
    } else {
        throw NotImplementedException("SPIR-V Instruction");
    }
}

Id EmitLoadLocal(EmitContext& ctx, Id word_offset) {
    const Id pointer{
        ctx.OpAccessChain(ctx.private_u32, ctx.local_memory, word_offset, ctx.Const(0U))};
    return ctx.OpLoad(ctx.U32[1], pointer);
}

void EmitWriteLocal(EmitContext& ctx, Id word_offset, Id value) {
    const Id pointer{
        ctx.OpAccessChain(ctx.private_u32, ctx.local_memory, word_offset, ctx.Const(0U))};
    ctx.OpStore(pointer, value);
}

} // namespace Shader::Backend::SPIRV
