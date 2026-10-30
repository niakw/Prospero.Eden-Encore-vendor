#include <atomic>
#include <cstdio>

namespace {
unsigned EdenGpuBoundedSample(std::atomic<unsigned>& samples) noexcept {
    unsigned current = samples.load(std::memory_order_relaxed);
    while (current < 8u) {
        if (samples.compare_exchange_weak(current, current + 1u,
                                          std::memory_order_relaxed))
            return current;
    }
    return 8u;
}
}


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

#include "common/common_types.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/maxwell/opcodes.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {

[[noreturn]] static void ThrowNotImplemented(Opcode opcode) {
    throw NotImplementedException("Instruction {} is not implemented", opcode);
}

void TranslatorVisitor::ATOM_cas(u64) {
    ThrowNotImplemented(Opcode::ATOM_cas);
}

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

void TranslatorVisitor::IMUL_reg(u64) {
    ThrowNotImplemented(Opcode::IMUL_reg);
}

void TranslatorVisitor::IMUL_cbuf(u64) {
    ThrowNotImplemented(Opcode::IMUL_cbuf);
}

void TranslatorVisitor::IMUL_imm(u64) {
    ThrowNotImplemented(Opcode::IMUL_imm);
}

void TranslatorVisitor::IMUL32I(u64) {
    ThrowNotImplemented(Opcode::IMUL32I);
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
    // Maxwell SASS PRMT register selector opcode (0x5bc0).
    // Dynamic selector is R[20:27]; A=R8, B=R39, mode[48:50].
    const unsigned mode = static_cast<unsigned>((insn >> 48) & 7ULL);
    if (mode != 0) {
        static std::atomic<unsigned> unsupported_samples{0};
        const unsigned sample = EdenGpuBoundedSample(unsupported_samples);
        if (sample < 8)
            std::fprintf(stderr, "EDEN_GPU_PRMT_REG_UNSUPPORTED raw=%016llx mode=%u sample=%u\n",
                         static_cast<unsigned long long>(insn), mode, sample + 1);
        ThrowNotImplemented(Opcode::PRMT_reg);
    }

    const IR::U32 a{GetReg8(insn)};
    const IR::U32 b{GetReg39(insn)};
    const IR::U32 selector{GetReg20(insn)};
    IR::U32 result{ir.Imm32(0)};
    for (unsigned i = 0; i < 4; ++i) {
        const IR::U32 nibble{ir.BitwiseAnd(
            ir.ShiftRightLogical(selector, ir.Imm32(i * 4u)), ir.Imm32(15))};
        const IR::U1 from_b{ir.INotEqual(
            ir.BitwiseAnd(nibble, ir.Imm32(4)), ir.Imm32(0))};
        const IR::U32 source{ir.Select(from_b, b, a)};
        const IR::U32 byte_shift{ir.ShiftLeftLogical(
            ir.BitwiseAnd(nibble, ir.Imm32(3)), ir.Imm32(3))};
        const IR::U32 selected_byte{ir.BitwiseAnd(
            ir.ShiftRightLogical(source, byte_shift), ir.Imm32(255))};
        const IR::U1 signed_replication{ir.INotEqual(
            ir.BitwiseAnd(nibble, ir.Imm32(8)), ir.Imm32(0))};
        const IR::U1 negative{ir.INotEqual(
            ir.BitwiseAnd(selected_byte, ir.Imm32(128)), ir.Imm32(0))};
        const IR::U32 replicated{ir.Select(negative, ir.Imm32(255), ir.Imm32(0))};
        const IR::U32 byte{ir.Select(signed_replication, replicated, selected_byte)};
        const IR::U32 shifted{i == 0 ? byte :
            IR::U32{ir.ShiftLeftLogical(byte, ir.Imm32(i * 8u))}};
        result = ir.BitwiseOr(result, shifted);
    }
    X(static_cast<IR::Reg>(insn & 255ULL), result);
    static std::atomic<unsigned> successes{0};
    const unsigned sample = EdenGpuBoundedSample(successes);
    if (sample < 8)
        std::fprintf(stderr, "EDEN_GPU_PRMT_REG_INDEX raw=%016llx sample=%u\n",
                     static_cast<unsigned long long>(insn), sample + 1);
}

void TranslatorVisitor::PRMT_rc(u64) {
    ThrowNotImplemented(Opcode::PRMT_rc);
}

void TranslatorVisitor::PRMT_cr(u64) {
    ThrowNotImplemented(Opcode::PRMT_cr);
}

void TranslatorVisitor::PRMT_imm(u64 insn) {
    // Maxwell SASS PRMT immediate (0x36c0) uses:
    // A = R[8:15], B = R[39:46], selector = bits[20:35],
    // mode = bits[48:50], D = R[0:7].
    // Only mode 0 (index byte permutation) is translated here.
    // Each nibble selects one of eight source bytes and its high bit
    // requests *sign replication* of that byte, not its raw value.
    const unsigned mode = static_cast<unsigned>((insn >> 48) & 7ULL);
    if (mode != 0) {
        static std::atomic<unsigned> unsupported_samples{0};
        const unsigned index = EdenGpuBoundedSample(unsupported_samples);
        if (index < 8)
            std::fprintf(stderr, "EDEN_GPU_PRMT_IMM raw=%016llx mode=%u sample=%u\n",
                         static_cast<unsigned long long>(insn), mode, index + 1);
        ThrowNotImplemented(Opcode::PRMT_imm);
    }

    const unsigned selector = static_cast<unsigned>((insn >> 20) & 0xffffULL);
    const IR::U32 a{GetReg8(insn)};
    const IR::U32 b{GetReg39(insn)};
    IR::U32 result{ir.Imm32(0)};
    for (unsigned output_byte = 0; output_byte < 4; ++output_byte) {
        const unsigned nibble = (selector >> (output_byte * 4)) & 15u;
        const IR::U32 source = (nibble & 4u) ? b : a;
        const unsigned src_offset = (nibble & 3u) * 8u;
        // When nibble[3] is set, the NVIDIA byte sign bit
        // becomes either 0x00 or 0xff in the output.
        const IR::U32 extracted = (nibble & 8u)
            ? ir.BitFieldExtract(source, ir.Imm32(src_offset + 7u), ir.Imm32(1), true)
            : ir.BitFieldExtract(source, ir.Imm32(src_offset), ir.Imm32(8), false);
        const IR::U32 value = ir.BitwiseAnd(extracted, ir.Imm32(255));
        const IR::U32 shifted = output_byte == 0 ? value :
            IR::U32{ir.ShiftLeftLogical(value, ir.Imm32(output_byte * 8u))};
        result = ir.BitwiseOr(result, shifted);
    }
    X(static_cast<IR::Reg>(insn & 255ULL), result);

    static std::atomic<unsigned> implemented_samples{0};
    const unsigned implemented = EdenGpuBoundedSample(implemented_samples);
    if (implemented < 8)
        std::fprintf(stderr, "EDEN_GPU_PRMT_IMM_INDEX raw=%016llx selector=%04x sample=%u\n",
                     static_cast<unsigned long long>(insn), selector, implemented + 1);
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
