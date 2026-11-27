// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class AtomOp : u64 {
    ADD,
    MIN,
    MAX,
    INC,
    DEC,
    AND,
    OR,
    XOR,
    EXCH,
};

enum class AtomsSize : u64 {
    U32,
    S32,
    U64,
};

IR::U32U64 ApplyAtomsOp(IR::IREmitter& ir, const IR::U32& offset, const IR::U32U64& op_b, AtomOp op,
                        bool is_signed) {
    switch (op) {
    case AtomOp::ADD:
        return ir.SharedAtomicIAdd(offset, op_b);
    case AtomOp::MIN:
        return ir.SharedAtomicIMin(offset, op_b, is_signed);
    case AtomOp::MAX:
        return ir.SharedAtomicIMax(offset, op_b, is_signed);
    case AtomOp::INC:
        return ir.SharedAtomicInc(offset, op_b);
    case AtomOp::DEC:
        return ir.SharedAtomicDec(offset, op_b);
    case AtomOp::AND:
        return ir.SharedAtomicAnd(offset, op_b);
    case AtomOp::OR:
        return ir.SharedAtomicOr(offset, op_b);
    case AtomOp::XOR:
        return ir.SharedAtomicXor(offset, op_b);
    case AtomOp::EXCH:
        return ir.SharedAtomicExchange(offset, op_b);
    default:
        throw NotImplementedException("Integer Atoms Operation {}", op);
    }
}

IR::U32 AtomsOffset(TranslatorVisitor& v, u64 insn) {
    union {
        u64 raw;
        BitField<8, 8, IR::Reg> offset_reg;
        BitField<30, 22, u64> absolute_offset;
        BitField<30, 22, s64> relative_offset;
    } const encoding{insn};

    if (encoding.offset_reg == IR::Reg::RZ) {
        return v.ir.Imm32(static_cast<u32>(encoding.absolute_offset << 2));
    } else {
        const s32 relative{static_cast<s32>(encoding.relative_offset << 2)};
        return v.ir.IAdd(v.X(encoding.offset_reg), v.ir.Imm32(relative));
    }
}

enum class AtomsCasBcRz : u64 {
    Pair = 0,
    ReplacementRz = 1,
    CompareRz = 2,
    Reserved = 3,
};

IR::U32 AtomsCasOffset(TranslatorVisitor& v, u64 insn) {
    union {
        u64 raw;
        BitField<8, 8, IR::Reg> address_reg;
        BitField<30, 22, s64> relative_offset;
    } const encoding{insn};

    const s32 relative{static_cast<s32>(static_cast<s64>(encoding.relative_offset) * 4)};
    return v.ir.IAdd(v.X(encoding.address_reg), v.ir.Imm32(relative));
}

void StoreResult(TranslatorVisitor& v, IR::Reg dest_reg, const IR::Value& result, AtomsSize size) {
    switch (size) {
    case AtomsSize::U32:
    case AtomsSize::S32:
        return v.X(dest_reg, IR::U32{result});
    case AtomsSize::U64:
        return v.L(dest_reg, IR::U64{result});
    default:
        break;
    }
}
} // Anonymous namespace

void TranslatorVisitor::ATOMS(u64 insn) {
    union {
        u64 raw;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> addr_reg;
        BitField<20, 8, IR::Reg> src_reg_b;
        BitField<28, 2, AtomsSize> size;
        BitField<52, 4, AtomOp> op;
    } const atoms{insn};

    const bool size_64{atoms.size == AtomsSize::U64};
    if (size_64 && atoms.op != AtomOp::EXCH) {
        throw NotImplementedException("64-bit Atoms Operation {}", atoms.op.Value());
    }
    const bool is_signed{atoms.size == AtomsSize::S32};
    const IR::U32 offset{AtomsOffset(*this, insn)};

    IR::Value result;
    if (size_64) {
        result = ApplyAtomsOp(ir, offset, L(atoms.src_reg_b), atoms.op, is_signed);
    } else {
        result = ApplyAtomsOp(ir, offset, X(atoms.src_reg_b), atoms.op, is_signed);
    }
    StoreResult(*this, atoms.dest_reg, result, atoms.size);
}

void TranslatorVisitor::ATOMS_cas(u64 insn) {
    union {
        u64 raw;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<20, 8, IR::Reg> src_reg_b;
        BitField<28, 2, AtomsCasBcRz> bc_rz;
        BitField<52, 4, u64> operation;
    } const atoms{insn};

    // The qualified Maxwell corpus covers only ATOMS.CAS.B32 (sub-op 4).
    // CAST/SPIN/64-bit sub-operations and BcRz=3 remain fail-closed.
    if (atoms.operation != 4) {
        throw NotImplementedException("ATOMS.CAS sub-operation {}", atoms.operation.Value());
    }
    if (atoms.bc_rz == AtomsCasBcRz::Reserved) {
        throw NotImplementedException("ATOMS.CAS reserved BcRz mode");
    }

    IR::U32 comparator{ir.Imm32(0)};
    IR::U32 replacement{ir.Imm32(0)};
    switch (atoms.bc_rz) {
    case AtomsCasBcRz::Pair: {
        if (atoms.src_reg_b == IR::Reg::RZ) {
            throw NotImplementedException("ATOMS.CAS pair starts at RZ");
        }
        comparator = X(atoms.src_reg_b);
        const IR::Reg replacement_reg{
            atoms.src_reg_b == IR::Reg::R254 ? IR::Reg::RZ : atoms.src_reg_b + 1};
        replacement = X(replacement_reg);
        break;
    }
    case AtomsCasBcRz::ReplacementRz:
        comparator = X(atoms.src_reg_b);
        break;
    case AtomsCasBcRz::CompareRz:
        replacement = X(atoms.src_reg_b);
        break;
    case AtomsCasBcRz::Reserved:
        break;
    }

    const IR::U32 offset{AtomsCasOffset(*this, insn)};
    X(atoms.dest_reg, ir.SharedAtomicCompareExchange(offset, comparator, replacement));
}

} // namespace Shader::Maxwell
