namespace {
constexpr unsigned EdenPrmtIndexReference(unsigned a, unsigned b, unsigned selector) {
    unsigned result = 0;
    for (unsigned output = 0; output < 4; ++output) {
        const unsigned nibble = (selector >> (4u * output)) & 15u;
        const unsigned source = (nibble & 4u) ? b : a;
        unsigned byte = (source >> (8u * (nibble & 3u))) & 255u;
        if (nibble & 8u)
            byte = (byte & 128u) ? 255u : 0u;
        result |= byte << (8u * output);
    }
    return result;
}
static_assert(EdenPrmtIndexReference(0x11223344u, 0x55667788u, 0x3210u) == 0x11223344u);
static_assert(EdenPrmtIndexReference(0x11223344u, 0x55667788u, 0x7654u) == 0x55667788u);
static_assert(EdenPrmtIndexReference(0x80abcdefu, 0u, 0xbbbbu) == 0xffffffffu);
static_assert(EdenPrmtIndexReference(0x11223344u, 0x55667788u, 0xffffu) == 0u);
}

// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/maxwell/opcodes.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {

[[noreturn]] static void ThrowNotImplemented(Opcode opcode) {
    throw NotImplementedException("Instruction {} is not implemented", opcode);
}

namespace {
// Maxwell GM107 IMUL encoding: envytools gm107.c tab5c38_{0,1}/tab1f00_{0,1}.
// Algorithm adapted from carafe-nx/carafe-lab b1f10cef (GPL-3.0-or-later).
// Build the high word from 16-bit products so the IR never requires an
// integer-64 multiply that a host shader backend may not provide.
IR::U32 MultiplyHigh(TranslatorVisitor& v, const IR::U32& a, const IR::U32& b,
                     bool is_a_signed, bool is_b_signed) {
    const IR::U32 mask{v.ir.Imm32(0xffff)};
    const IR::U32 sixteen{v.ir.Imm32(16)};
    const IR::U32 a_lo{v.ir.BitwiseAnd(a, mask)};
    const IR::U32 a_hi{v.ir.ShiftRightLogical(a, sixteen)};
    const IR::U32 b_lo{v.ir.BitwiseAnd(b, mask)};
    const IR::U32 b_hi{v.ir.ShiftRightLogical(b, sixteen)};
    const IR::U32 lo_lo{v.ir.IMul(a_lo, b_lo)};
    const IR::U32 hi_lo{v.ir.IMul(a_hi, b_lo)};
    const IR::U32 lo_hi{v.ir.IMul(a_lo, b_hi)};
    const IR::U32 hi_hi{v.ir.IMul(a_hi, b_hi)};
    const IR::U32 middle{v.ir.IAdd(v.ir.IAdd(v.ir.ShiftRightLogical(lo_lo, sixteen),
                                             v.ir.BitwiseAnd(hi_lo, mask)),
                                   v.ir.BitwiseAnd(lo_hi, mask))};
    IR::U32 high{v.ir.IAdd(v.ir.IAdd(hi_hi, v.ir.ShiftRightLogical(hi_lo, sixteen)),
                           v.ir.IAdd(v.ir.ShiftRightLogical(lo_hi, sixteen),
                                     v.ir.ShiftRightLogical(middle, sixteen)))};
    if (is_a_signed) {
        const IR::U1 negative{v.ir.ILessThan(a, v.ir.Imm32(0), true)};
        high = v.ir.ISub(high, IR::U32{v.ir.Select(negative, b, v.ir.Imm32(0))});
    }
    if (is_b_signed) {
        const IR::U1 negative{v.ir.ILessThan(b, v.ir.Imm32(0), true)};
        high = v.ir.ISub(high, IR::U32{v.ir.Select(negative, a, v.ir.Imm32(0))});
    }
    return high;
}

void TranslateIMul(TranslatorVisitor& v, u64 insn, const IR::U32& src_b,
                   bool high, bool is_a_signed, bool is_b_signed, bool cc) {
    union {
        u64 raw;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> src_reg_a;
    } const imul{insn};

    // Condition-code publication has separate carry/overflow semantics.
    // Keep it fail-closed until those flags are implemented and verified.
    if (cc) {
        throw NotImplementedException("IMUL.CC");
    }
    const IR::U32 src_a{v.X(imul.src_reg_a)};
    const IR::U32 result{
        high ? MultiplyHigh(v, src_a, src_b, is_a_signed, is_b_signed)
             : v.ir.IMul(src_a, src_b)};
    v.X(imul.dest_reg, result);
}

void TranslateIMul(TranslatorVisitor& v, u64 insn, const IR::U32& src_b) {
    union {
        u64 raw;
        BitField<39, 1, u64> high;
        BitField<40, 1, u64> is_a_signed;
        BitField<41, 1, u64> is_b_signed;
        BitField<47, 1, u64> cc;
    } const imul{insn};
    TranslateIMul(v, insn, src_b, imul.high != 0, imul.is_a_signed != 0,
                  imul.is_b_signed != 0, imul.cc != 0);
}

// NVIDIA PTX prmt.b32 specialized controls and envytools GM107 tab5bc0_0
// use mode values 1..6 in bits 48..51. Values 7..15 are reserved.
enum class PrmtMode : u32 {
    Index = 0,
    Forward4Extract = 1,
    Backward4Extract = 2,
    Replicate8 = 3,
    EdgeClampLeft = 4,
    EdgeClampRight = 5,
    Replicate16 = 6,
};

constexpr u32 PrmtTableIndex(PrmtMode mode, u32 selector, u32 byte) {
    switch (mode) {
    case PrmtMode::Forward4Extract:
        return (selector + byte) & 7;
    case PrmtMode::Backward4Extract:
        return (selector - byte) & 7;
    case PrmtMode::Replicate8:
        return selector;
    case PrmtMode::EdgeClampLeft:
        return selector > byte ? selector : byte;
    case PrmtMode::EdgeClampRight:
        return selector < byte ? selector : byte;
    case PrmtMode::Replicate16:
        return (byte & 1) + 2 * (selector & 1);
    case PrmtMode::Index:
        break;
    }
    return 8;
}

static_assert(PrmtTableIndex(PrmtMode::Forward4Extract, 1, 3) == 4);
static_assert(PrmtTableIndex(PrmtMode::Backward4Extract, 0, 3) == 5);
static_assert(PrmtTableIndex(PrmtMode::Replicate8, 2, 3) == 2);
static_assert(PrmtTableIndex(PrmtMode::EdgeClampLeft, 1, 1) == 1);
static_assert(PrmtTableIndex(PrmtMode::EdgeClampRight, 2, 3) == 2);
static_assert(PrmtTableIndex(PrmtMode::Replicate16, 1, 2) == 2);

IR::U32 PickPrmtByte(TranslatorVisitor& v, const IR::U32& a, const IR::U32& b,
                     const IR::U32& index, const IR::U1& replicate_sign) {
    const IR::U1 from_b{
        v.ir.INotEqual(v.ir.BitwiseAnd(index, v.ir.Imm32(4)), v.ir.Imm32(0))};
    const IR::U32 source{v.ir.Select(from_b, b, a)};
    const IR::U32 offset{
        v.ir.ShiftLeftLogical(v.ir.BitwiseAnd(index, v.ir.Imm32(3)), v.ir.Imm32(3))};
    const IR::U32 value{
        v.ir.BitFieldExtract(source, offset, v.ir.Imm32(8), false)};
    const IR::U1 negative{
        v.ir.INotEqual(v.ir.BitwiseAnd(value, v.ir.Imm32(0x80)), v.ir.Imm32(0))};
    const IR::U32 sign_byte{
        v.ir.Select(negative, v.ir.Imm32(0xff), v.ir.Imm32(0))};
    return IR::U32{v.ir.Select(replicate_sign, sign_byte, value)};
}

void TranslatePrmt(TranslatorVisitor& v, u64 insn, const IR::U32& selector,
                   const IR::U32& src_b) {
    union {
        u64 raw;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> src_reg_a;
        BitField<48, 4, u64> mode;
    } const prmt{insn};

    const u32 raw_mode = static_cast<u32>(prmt.mode.Value());
    if (raw_mode > static_cast<u32>(PrmtMode::Replicate16)) {
        throw NotImplementedException("PRMT reserved mode {}", raw_mode);
    }
    const PrmtMode mode{static_cast<PrmtMode>(raw_mode)};
    const IR::U32 src_a{v.X(prmt.src_reg_a)};
    IR::U32 result{v.ir.Imm32(0)};
    for (u32 byte = 0; byte < 4; ++byte) {
        IR::U32 index;
        IR::U1 replicate_sign{v.ir.Imm1(false)};
        if (mode == PrmtMode::Index) {
            const IR::U32 nibble{
                v.ir.BitFieldExtract(selector, v.ir.Imm32(byte * 4), v.ir.Imm32(4), false)};
            index = v.ir.BitwiseAnd(nibble, v.ir.Imm32(7));
            replicate_sign =
                v.ir.INotEqual(v.ir.BitwiseAnd(nibble, v.ir.Imm32(8)), v.ir.Imm32(0));
        } else {
            // Specialized PTX modes use only selector c[1:0].
            const IR::U32 control{v.ir.BitwiseAnd(selector, v.ir.Imm32(3))};
            index = v.ir.Imm32(PrmtTableIndex(mode, 3, byte));
            for (u32 value = 0; value < 3; ++value) {
                index = IR::U32{v.ir.Select(v.ir.IEqual(control, v.ir.Imm32(value)),
                                            v.ir.Imm32(PrmtTableIndex(mode, value, byte)),
                                            index)};
            }
        }
        const IR::U32 picked{PickPrmtByte(v, src_a, src_b, index, replicate_sign)};
        const IR::U32 shifted{
            byte == 0 ? picked
                      : IR::U32{v.ir.ShiftLeftLogical(picked, v.ir.Imm32(byte * 8))}};
        result = v.ir.BitwiseOr(result, shifted);
    }
    v.X(prmt.dest_reg, result);
}
} // namespace

void TranslatorVisitor::ATOMS_cas(u64) {
    ThrowNotImplemented(Opcode::ATOMS_cas);
}

void TranslatorVisitor::B2R(u64) {
    ThrowNotImplemented(Opcode::B2R);
}

void TranslatorVisitor::BPT(u64) {
    ThrowNotImplemented(Opcode::BPT);
}

void TranslatorVisitor::BRA(u64) {
    ThrowNotImplemented(Opcode::BRA);
}

void TranslatorVisitor::BRK(u64) {
    ThrowNotImplemented(Opcode::BRK);
}

void TranslatorVisitor::CAL(u64) {
    // CAL is a no-op
}

void TranslatorVisitor::CCTL(u64) {
    ThrowNotImplemented(Opcode::CCTL);
}

void TranslatorVisitor::CCTLL(u64) {
    ThrowNotImplemented(Opcode::CCTLL);
}

void TranslatorVisitor::CCTLT(u64) {
    ThrowNotImplemented(Opcode::CCTLT);
}

void TranslatorVisitor::CONT(u64) {
    ThrowNotImplemented(Opcode::CONT);
}

void TranslatorVisitor::CS2R(u64) {
    ThrowNotImplemented(Opcode::CS2R);
}

void TranslatorVisitor::FCHK_reg(u64) {
    ThrowNotImplemented(Opcode::FCHK_reg);
}

void TranslatorVisitor::FCHK_cbuf(u64) {
    ThrowNotImplemented(Opcode::FCHK_cbuf);
}

void TranslatorVisitor::FCHK_imm(u64) {
    ThrowNotImplemented(Opcode::FCHK_imm);
}

void TranslatorVisitor::GETCRSPTR(u64) {
    ThrowNotImplemented(Opcode::GETCRSPTR);
}

void TranslatorVisitor::GETLMEMBASE(u64) {
    ThrowNotImplemented(Opcode::GETLMEMBASE);
}

void TranslatorVisitor::IDE(u64) {
    ThrowNotImplemented(Opcode::IDE);
}

void TranslatorVisitor::IDP_reg(u64) {
    ThrowNotImplemented(Opcode::IDP_reg);
}

void TranslatorVisitor::IDP_imm(u64) {
    ThrowNotImplemented(Opcode::IDP_imm);
}

void TranslatorVisitor::IMAD_reg(u64) {
    ThrowNotImplemented(Opcode::IMAD_reg);
}

void TranslatorVisitor::IMAD_rc(u64) {
    ThrowNotImplemented(Opcode::IMAD_rc);
}

void TranslatorVisitor::IMAD_cr(u64) {
    ThrowNotImplemented(Opcode::IMAD_cr);
}

void TranslatorVisitor::IMAD_imm(u64) {
    ThrowNotImplemented(Opcode::IMAD_imm);
}

void TranslatorVisitor::IMAD32I(u64) {
    ThrowNotImplemented(Opcode::IMAD32I);
}

void TranslatorVisitor::IMADSP_reg(u64) {
    ThrowNotImplemented(Opcode::IMADSP_reg);
}

void TranslatorVisitor::IMADSP_rc(u64) {
    ThrowNotImplemented(Opcode::IMADSP_rc);
}

void TranslatorVisitor::IMADSP_cr(u64) {
    ThrowNotImplemented(Opcode::IMADSP_cr);
}

void TranslatorVisitor::IMADSP_imm(u64) {
    ThrowNotImplemented(Opcode::IMADSP_imm);
}

void TranslatorVisitor::IMUL_reg(u64 insn) {
    TranslateIMul(*this, insn, GetReg20(insn));
}

void TranslatorVisitor::IMUL_cbuf(u64 insn) {
    TranslateIMul(*this, insn, GetCbuf(insn));
}

void TranslatorVisitor::IMUL_imm(u64 insn) {
    TranslateIMul(*this, insn, GetImm20(insn));
}

void TranslatorVisitor::IMUL32I(u64 insn) {
    union {
        u64 raw;
        BitField<52, 1, u64> cc;
        BitField<53, 1, u64> high;
        BitField<54, 1, u64> is_a_signed;
        BitField<55, 1, u64> is_b_signed;
    } const imul{insn};
    TranslateIMul(*this, insn, GetImm32(insn), imul.high != 0,
                  imul.is_a_signed != 0, imul.is_b_signed != 0, imul.cc != 0);
}

void TranslatorVisitor::JCAL(u64) {
    ThrowNotImplemented(Opcode::JCAL);
}

void TranslatorVisitor::JMP(u64) {
    ThrowNotImplemented(Opcode::JMP);
}

void TranslatorVisitor::KIL(u64) {
    // KIL is a no-op
}

void TranslatorVisitor::LD(u64) {
    ThrowNotImplemented(Opcode::LD);
}

void TranslatorVisitor::LEPC(u64) {
    ThrowNotImplemented(Opcode::LEPC);
}

void TranslatorVisitor::LONGJMP(u64) {
    ThrowNotImplemented(Opcode::LONGJMP);
}

void TranslatorVisitor::NOP(u64) {
    // NOP is a no-op
}

void TranslatorVisitor::PBK(u64) {
    // PBK is a no-op
}

void TranslatorVisitor::PCNT(u64) {
    // PCNT is a no-op
}

void TranslatorVisitor::PEXIT(u64) {
    ThrowNotImplemented(Opcode::PEXIT);
}

void TranslatorVisitor::PLONGJMP(u64) {
    ThrowNotImplemented(Opcode::PLONGJMP);
}

void TranslatorVisitor::PRET(u64) {
    ThrowNotImplemented(Opcode::PRET);
}

void TranslatorVisitor::PRMT_reg(u64 insn) {
    TranslatePrmt(*this, insn, GetReg20(insn), GetReg39(insn));
}

void TranslatorVisitor::PRMT_rc(u64 insn) {
    TranslatePrmt(*this, insn, GetReg39(insn), GetCbuf(insn));
}

void TranslatorVisitor::PRMT_cr(u64 insn) {
    TranslatePrmt(*this, insn, GetCbuf(insn), GetReg39(insn));
}

void TranslatorVisitor::PRMT_imm(u64 insn) {
    TranslatePrmt(*this, insn, GetImm20(insn), GetReg39(insn));
}

void TranslatorVisitor::R2B(u64) {
    ThrowNotImplemented(Opcode::R2B);
}

void TranslatorVisitor::RAM(u64) {
    LOG_WARNING(Shader, "(STUBBED) RAM Instruction");
}

void TranslatorVisitor::RET(u64) {
    ThrowNotImplemented(Opcode::RET);
}

void TranslatorVisitor::RTT(u64) {
    ThrowNotImplemented(Opcode::RTT);
}

void TranslatorVisitor::SAM(u64) {
    LOG_WARNING(Shader, "(STUBBED) SAM Instruction");
}

void TranslatorVisitor::SETCRSPTR(u64) {
    ThrowNotImplemented(Opcode::SETCRSPTR);
}

void TranslatorVisitor::SETLMEMBASE(u64) {
    ThrowNotImplemented(Opcode::SETLMEMBASE);
}

void TranslatorVisitor::SSY(u64) {
    // SSY is a no-op
}

void TranslatorVisitor::ST(u64) {
    ThrowNotImplemented(Opcode::ST);
}

void TranslatorVisitor::STP(u64) {
    ThrowNotImplemented(Opcode::STP);
}

void TranslatorVisitor::SUATOM_cas(u64) {
    ThrowNotImplemented(Opcode::SUATOM_cas);
}

void TranslatorVisitor::SYNC(u64) {
    ThrowNotImplemented(Opcode::SYNC);
}

void TranslatorVisitor::TXA(u64) {
    ThrowNotImplemented(Opcode::TXA);
}

void TranslatorVisitor::VABSDIFF(u64) {
    ThrowNotImplemented(Opcode::VABSDIFF);
}

void TranslatorVisitor::VABSDIFF4(u64) {
    ThrowNotImplemented(Opcode::VABSDIFF4);
}

void TranslatorVisitor::VADD(u64) {
    ThrowNotImplemented(Opcode::VADD);
}

void TranslatorVisitor::VSET(u64) {
    ThrowNotImplemented(Opcode::VSET);
}
void TranslatorVisitor::VSHL(u64) {
    ThrowNotImplemented(Opcode::VSHL);
}

void TranslatorVisitor::VSHR(u64) {
    ThrowNotImplemented(Opcode::VSHR);
}

} // namespace Shader::Maxwell
