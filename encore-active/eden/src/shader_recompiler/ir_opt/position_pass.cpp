// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/small_vector.hpp>

#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace Shader::Optimization {

namespace {
struct PositionInst {
    IR::Inst* inst;
    IR::Block* block;
    IR::Attribute attr;
};
using PositionInstVector = boost::container::small_vector<PositionInst, 24>;
} // Anonymous namespace

void PositionPass(Environment& env, IR::Program& program) {
    if (env.ShaderStage() != Stage::VertexB || env.ReadViewportTransformState()) {
        return;
    }

    Info& info{program.info};
    info.uses_render_area = true;

    PositionInstVector to_replace;
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            switch (inst.GetOpcode()) {
            case IR::Opcode::SetAttribute: {
                const IR::Attribute attr{inst.Arg(0).Attribute()};
                switch (attr) {
                case IR::Attribute::PositionX:
                case IR::Attribute::PositionY: {
                    to_replace.push_back(PositionInst{.inst = &inst, .block = block, .attr = attr});
                    break;
                }
                default:
                    break;
                }
                break;
            }
            default:
                break;
            }
        }
    }

    for (PositionInst& position_inst : to_replace) {
        IR::IREmitter ir{*position_inst.block,
                         IR::Block::InstructionList::s_iterator_to(*position_inst.inst)};
        const IR::F32 value(position_inst.inst->Arg(1));
        const IR::F32F64 scale(ir.Imm32(2.f));
        const IR::F32 negative_one{ir.Imm32(-1.f)};
        switch (position_inst.attr) {
        case IR::Attribute::PositionX: {
            position_inst.inst->SetArg(
                1,
                ir.FPFma(value, ir.FPMul(ir.FPRecip(ir.RenderAreaWidth()), scale), negative_one));
            break;
        }
        case IR::Attribute::PositionY: {
            position_inst.inst->SetArg(
                1,
                ir.FPFma(value, ir.FPMul(ir.FPRecip(ir.RenderAreaHeight()), scale), negative_one));
            break;
        }
        default:
            break;
        }
    }
}

// Native fullscreen blits can consume a console root-table block through the
// remapped bank 17. Synthesize only the known block when every immediate
// bank-17 read stays inside it and at least one requested word is nonzero.
// Other root-table users keep the real words uploaded by methods 0x504/0x508.
void BlitConstantsPass(Environment& env, IR::Program& program) {
    if (env.ShaderStage() != Stage::VertexB && env.ShaderStage() != Stage::Fragment) {
        return;
    }
    const auto in_blit_block = [](u32 offset) {
        return offset < 0x20 || (offset >= 0x30 && offset < 0x4c) ||
               (offset >= 0x50 && offset < 0x60);
    };
    const auto is_nonzero_word = [](u32 offset) {
        return offset < 0x20 || offset == 0x38 || offset == 0x3c ||
               offset == 0x58 || offset == 0x5c;
    };
    bool reads_nonzero_word{};
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            const IR::Opcode opcode{inst.GetOpcode()};
            if ((opcode != IR::Opcode::GetCbufU32 &&
                 opcode != IR::Opcode::GetCbufF32 &&
                 opcode != IR::Opcode::GetCbufU32x2) ||
                !inst.Arg(0).IsImmediate() || inst.Arg(0).U32() != 17 ||
                !inst.Arg(1).IsImmediate()) {
                continue;
            }
            const u32 offset{inst.Arg(1).U32()};
            const bool pair{opcode == IR::Opcode::GetCbufU32x2};
            if (!in_blit_block(offset) ||
                (pair && !in_blit_block(offset + sizeof(u32)))) {
                return;
            }
            reads_nonzero_word |= is_nonzero_word(offset) ||
                (pair && is_nonzero_word(offset + sizeof(u32)));
        }
    }
    if (!reads_nonzero_word) {
        return;
    }
    bool used{};
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            const IR::Opcode opcode{inst.GetOpcode()};
            if ((opcode != IR::Opcode::GetCbufU32 &&
                 opcode != IR::Opcode::GetCbufF32 &&
                 opcode != IR::Opcode::GetCbufU32x2) ||
                !inst.Arg(0).IsImmediate() || inst.Arg(0).U32() != 17 ||
                !inst.Arg(1).IsImmediate()) {
                continue;
            }
            IR::IREmitter ir{*block,
                             IR::Block::InstructionList::s_iterator_to(inst)};
            const IR::F32 width{ir.RenderAreaWidth()};
            const IR::F32 height{ir.RenderAreaHeight()};
            const auto word = [&](u32 offset) -> IR::U32 {
                switch (offset) {
                case 0x0:
                case 0x10:
                    return ir.BitCast<IR::U32>(IR::F32{ir.FPRecip(width)});
                case 0x4:
                case 0x14:
                    return ir.BitCast<IR::U32>(IR::F32{ir.FPRecip(height)});
                case 0x8:
                    return ir.BitCast<IR::U32>(width);
                case 0xc:
                    return ir.BitCast<IR::U32>(height);
                case 0x18:
                case 0x1c:
                case 0x38:
                case 0x3c:
                    return ir.Imm32(0x3f800000U);
                case 0x58:
                    return IR::U32{ir.ConvertFToU(
                        32, ir.FPAdd(width, ir.Imm32(-1.0f)))};
                case 0x5c:
                    return IR::U32{ir.ConvertFToU(
                        32, ir.FPAdd(height, ir.Imm32(-1.0f)))};
                default:
                    return ir.Imm32(0U);
                }
            };
            const u32 offset{inst.Arg(1).U32()};
            IR::Value value;
            if (opcode == IR::Opcode::GetCbufU32x2) {
                value = ir.CompositeConstruct(word(offset), word(offset + sizeof(u32)));
            } else if (opcode == IR::Opcode::GetCbufF32) {
                value = ir.BitCast<IR::F32>(word(offset));
            } else {
                value = word(offset);
            }
            inst.ReplaceUsesWith(value);
            used = true;
        }
    }
    if (used) {
        program.info.uses_render_area = true;
    }
}
} // namespace Shader::Optimization
