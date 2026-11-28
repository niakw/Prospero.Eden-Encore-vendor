// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class Size : u64 {
    U8,
    S8,
    U16,
    S16,
    B32,
    B64,
    B128,
};

IR::U32 Offset(TranslatorVisitor& v, u64 insn) {
    union {
        u64 raw;
        BitField<8, 8, IR::Reg> offset_reg;
        BitField<20, 24, u64> absolute_offset;
        BitField<20, 24, s64> relative_offset;
    } const encoding{insn};

    if (encoding.offset_reg == IR::Reg::RZ) {
        return v.ir.Imm32(static_cast<u32>(encoding.absolute_offset));
    } else {
        const s32 relative{static_cast<s32>(encoding.relative_offset.Value())};
        return v.ir.IAdd(v.X(encoding.offset_reg), v.ir.Imm32(relative));
    }
}

std::pair<IR::U32, IR::U32> WordOffset(TranslatorVisitor& v, u64 insn) {
    const IR::U32 offset{Offset(v, insn)};
    if (offset.IsImmediate()) {
        return {v.ir.Imm32(offset.U32() / 4), offset};
    } else {
        return {v.ir.ShiftRightArithmetic(offset, v.ir.Imm32(2)), offset};
    }
}

std::pair<int, bool> GetSize(u64 insn) {
    union {
        u64 raw;
        BitField<48, 3, Size> size;
    } const encoding{insn};

    switch (encoding.size) {
    case Size::U8:
        return {8, false};
    case Size::S8:
        return {8, true};
    case Size::U16:
        return {16, false};
    case Size::S16:
        return {16, true};
    case Size::B32:
        return {32, false};
    case Size::B64:
        return {64, false};
    case Size::B128:
        return {128, false};
    default:
        throw NotImplementedException("Invalid size {}", encoding.size.Value());
    }
}

IR::Reg Reg(u64 insn) {
    union {
        u64 raw;
        BitField<0, 8, IR::Reg> reg;
    } const encoding{insn};

    return encoding.reg;
}

IR::U32 ByteOffset(IR::IREmitter& ir, const IR::U32& offset) {
    return ir.BitwiseAnd(ir.ShiftLeftLogical(offset, ir.Imm32(3)), ir.Imm32(24));
}

IR::U32 ShortOffset(IR::IREmitter& ir, const IR::U32& offset) {
    return ir.BitwiseAnd(ir.ShiftLeftLogical(offset, ir.Imm32(3)), ir.Imm32(16));
}

// Bound the ORIGINAL byte offset to (allocation size - following word bytes).
// Checking a wrapped offset + N would incorrectly admit addresses near UINT32_MAX.
IR::U1 LocalInBounds(TranslatorVisitor& v, const IR::U32& offset, u32 max_start) {
    return v.ir.ILessThan(offset, v.ir.Imm32(max_start), false);
}

// Check the address before emitting a read: a Select of an unsafe LoadLocal
// still emits the read, and an empty local allocation has no safe word 0.
IR::U32 LoadLocal(TranslatorVisitor& v, const IR::U32& word_offset, const IR::U32& offset,
                  u32 byte_advance = 0) {
    const u32 local_size{v.env.LocalMemorySize()};
    const u32 max_start{byte_advance < local_size ? local_size - byte_advance : 0};
    if (max_start == 0 || (offset.IsImmediate() && offset.U32() >= max_start)) {
        return v.ir.Imm32(0);
    }
    // Once a literal byte address passes the bound check, neither the
    // dynamic compare nor either Select is needed in the compiled shader IR.
    // Dynamic offsets retain their safe-address clamp before LoadLocal.
    if (offset.IsImmediate()) {
        return v.ir.LoadLocal(word_offset);
    }
    const IR::U1 in_bounds{LocalInBounds(v, offset, max_start)};
    const IR::U32 safe_word_offset{v.ir.Select(in_bounds, word_offset, v.ir.Imm32(0))};
    return IR::U32{v.ir.Select(in_bounds, v.ir.LoadLocal(safe_word_offset), v.ir.Imm32(0))};
}

// An out-of-bounds dynamic store writes the old contents of scratch word 0
// back to itself. Out-of-bounds immediate tail words are simply dropped.
void WriteLocal(TranslatorVisitor& v, const IR::U32& word_offset, const IR::U32& offset,
                const IR::U32& value, u32 byte_advance = 0) {
    const u32 local_size{v.env.LocalMemorySize()};
    const u32 max_start{byte_advance < local_size ? local_size - byte_advance : 0};
    if (max_start == 0 || (offset.IsImmediate() && offset.U32() >= max_start)) {
        return;
    }
    if (offset.IsImmediate()) {
        v.ir.WriteLocal(word_offset, value);
        return;
    }
    const IR::U1 in_bounds{LocalInBounds(v, offset, max_start)};
    const IR::U32 safe_word_offset{v.ir.Select(in_bounds, word_offset, v.ir.Imm32(0))};
    const IR::U32 safe_value{v.ir.Select(in_bounds, value, v.ir.LoadLocal(safe_word_offset))};
    v.ir.WriteLocal(safe_word_offset, safe_value);
}
} // Anonymous namespace

void TranslatorVisitor::LDL(u64 insn) {
    const auto [word_offset, offset]{WordOffset(*this, insn)};
    const IR::U32 word{LoadLocal(*this, word_offset, offset)};
    const IR::Reg dest{Reg(insn)};
    const auto [bit_size, is_signed]{GetSize(insn)};
    switch (bit_size) {
    case 8: {
        const IR::U32 bit{ByteOffset(ir, offset)};
        X(dest, ir.BitFieldExtract(word, bit, ir.Imm32(8), is_signed));
        break;
    }
    case 16: {
        const IR::U32 bit{ShortOffset(ir, offset)};
        X(dest, ir.BitFieldExtract(word, bit, ir.Imm32(16), is_signed));
        break;
    }
    case 32:
    case 64:
    case 128:
        if (!IR::IsAligned(dest, static_cast<size_t>(bit_size / 32))) {
            throw NotImplementedException("Unaligned destination register {}", dest);
        }
        X(dest, word);
        for (int i = 1; i < bit_size / 32; ++i) {
            const IR::U32 sub_word_offset{ir.IAdd(word_offset, ir.Imm32(i))};
            X(dest + i, LoadLocal(*this, sub_word_offset, offset, static_cast<u32>(i * 4)));
        }
        break;
    }
}

void TranslatorVisitor::LDS(u64 insn) {
    const IR::U32 offset{Offset(*this, insn)};
    const IR::Reg dest{Reg(insn)};
    const auto [bit_size, is_signed]{GetSize(insn)};
    const IR::Value value{ir.LoadShared(bit_size, is_signed, offset)};
    switch (bit_size) {
    case 8:
    case 16:
    case 32:
        X(dest, IR::U32{value});
        break;
    case 64:
    case 128:
        if (!IR::IsAligned(dest, static_cast<size_t>(bit_size / 32))) {
            throw NotImplementedException("Unaligned destination register {}", dest);
        }
        for (int element = 0; element < bit_size / 32; ++element) {
            X(dest + element, IR::U32{ir.CompositeExtract(value, static_cast<size_t>(element))});
        }
        break;
    }
}

void TranslatorVisitor::STL(u64 insn) {
    const auto [word_offset, offset]{WordOffset(*this, insn)};
    if (offset.IsImmediate()) {
        // TODO: Support storing out of bounds at runtime
        if (offset.U32() >= env.LocalMemorySize()) {
            LOG_WARNING(Shader, "Storing local memory at {:#x} with a size of {:#x}, dropping",
                        offset.U32(), env.LocalMemorySize());
            return;
        }
    }
    const IR::Reg reg{Reg(insn)};
    const IR::U32 src{X(reg)};
    const int bit_size{GetSize(insn).first};
    switch (bit_size) {
    case 8: {
        const IR::U32 bit{ByteOffset(ir, offset)};
        const IR::U32 value{ir.BitFieldInsert(LoadLocal(*this, word_offset, offset), src, bit, ir.Imm32(8))};
        WriteLocal(*this, word_offset, offset, value);
        break;
    }
    case 16: {
        const IR::U32 bit{ShortOffset(ir, offset)};
        const IR::U32 value{ir.BitFieldInsert(LoadLocal(*this, word_offset, offset), src, bit, ir.Imm32(16))};
        WriteLocal(*this, word_offset, offset, value);
        break;
    }
    case 32:
    case 64:
    case 128:
        if (!IR::IsAligned(reg, static_cast<size_t>(bit_size / 32))) {
            throw NotImplementedException("Unaligned source register");
        }
        WriteLocal(*this, word_offset, offset, src);
        for (int i = 1; i < bit_size / 32; ++i) {
            WriteLocal(*this, ir.IAdd(word_offset, ir.Imm32(i)), offset, X(reg + i),
                       static_cast<u32>(i * 4));
        }
        break;
    }
}

void TranslatorVisitor::STS(u64 insn) {
    const IR::U32 offset{Offset(*this, insn)};
    const IR::Reg reg{Reg(insn)};
    const int bit_size{GetSize(insn).first};
    switch (bit_size) {
    case 8:
    case 16:
    case 32:
        ir.WriteShared(bit_size, offset, X(reg));
        break;
    case 64:
        if (!IR::IsAligned(reg, 2)) {
            throw NotImplementedException("Unaligned source register {}", reg);
        }
        ir.WriteShared(64, offset, ir.CompositeConstruct(X(reg), X(reg + 1)));
        break;
    case 128: {
        if (!IR::IsAligned(reg, 2)) {
            throw NotImplementedException("Unaligned source register {}", reg);
        }
        const IR::Value vector{ir.CompositeConstruct(X(reg), X(reg + 1), X(reg + 2), X(reg + 3))};
        ir.WriteShared(128, offset, vector);
        break;
    }
    }
}

} // namespace Shader::Maxwell
