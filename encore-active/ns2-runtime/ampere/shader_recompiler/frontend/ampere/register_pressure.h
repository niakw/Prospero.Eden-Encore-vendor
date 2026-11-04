// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <vector>
#include "shader_recompiler/frontend/ampere/instruction.h"

namespace Shader::Ampere {
struct RegisterPressure {
    Instruction instruction;
    std::vector<u64> prefix, suffix;
    bool valid = true;
};
inline u64 NativeRegisterCopy(u32 destination_bank, u32 destination, u32 source_bank, u32 source) {
    return 0x5c98000000070000ULL | (u64{10} << 44) | destination |
        (u64{destination_bank} << 8) | (u64{source_bank} << 10) | (u64{source} << 20);
}
// A vector instruction encodes only its first GPR. The generic scalar
// operand scanner in LowerToMaxwell cannot see implicit following registers.
// Detect a low base that crosses into the compiler-owned scratch region so
// that it must pass through the full contiguous-register spill validator.
inline bool NativeVectorCrossesScratch(const Instruction& input, u32 reserved) {
    const auto crosses = [&](u32 bit, u32 count) {
        const u32 reg = input.Bits(bit, 8);
        return count > 1 && reg != 255 && reg < reserved && reg + count > reserved;
    };
    const u32 opcode = input.Bits(0, 12);
    const u32 width = input.Bits(73, 3);
    const u32 count = width == 6 ? 4 : width == 5 ? 2 : 1;
    switch (opcode) {
    case 0x225: return crosses(16, 2) || crosses(64, 2);
    case 0x625: return crosses(16, 2);
    case 0x825: return crosses(16, 2) || crosses(64, 2);
    case 0x83b: return crosses(16, input.Bits(72, 2) == 1 ? 2 : 4);
    case 0x23c: {
        const bool wide = input.Bits(75, 1);
        return crosses(16, 2) || crosses(24, wide ? 4 : 2) ||
               crosses(32, wide ? 2 : 1) || crosses(64, 2);
    }
    case 0x388: return crosses(32, count);
    case 0x984: return crosses(16, count);
    case 0x381: return crosses(16, count) || crosses(24, 2);
    case 0x386: return crosses(32, count) || crosses(24, 2);
    case 0x981: return crosses(16, count) ||
                       (input.Bits(90, 1) && crosses(24, 2));
    case 0x986: return crosses(32, count) ||
                       (input.Bits(90, 1) && crosses(24, 2));
    default: return false;
    }
}
// Preserve high guest registers in SSA while borrowing aligned low quartets
// for one native instruction. The expanded instruction owns its temporary bank.
inline RegisterPressure PrepareRegisterPressure(const Instruction& input, u32 reserved) {
    RegisterPressure result{input, {}, {}, true};
    struct Operand { u32 bit, count; };
    std::vector<Operand> operands;
    const auto operand = [&](u32 bit, u32 count = 1) { operands.push_back({bit, count}); };
    const u32 opcode = input.Bits(0, 12);
    switch (opcode) {
    case 0x802: case 0xb08: case 0xd08: case 0x919: operand(16); break;
    case 0x308: case 0x245: operand(16); operand(32); break;
    case 0x433: case 0x633: operand(16); operand(24); break;
    case 0x233: case 0x208: case 0x220: case 0x221:
        operand(16); operand(24); operand(32); break;
    case 0x223: // FFMA uses a third real source register at bit 64.
        operand(16); operand(24); operand(32); operand(64); break;
    case 0x80b: operand(24); break;
    case 0x20b: operand(24); operand(32); break;
    case 0x820: case 0x421: case 0x817:
        operand(16); operand(24); break;
    case 0x812: case 0x823: case 0x824: case 0x831: case 0x424: case 0x624:
        operand(16); operand(24); operand(64); break;
    case 0x230: case 0x231:
        operand(16); operand(24); operand(32); operand(64); break;
    case 0x819:
        operand(16); operand(24); operand(64); break;
    case 0x211:
        operand(16); operand(24); operand(32); operand(64); break;
    case 0xa11:
        operand(16); operand(24); operand(64); break;
    case 0x225: case 0x625: case 0x825:
        operand(16, 2); operand(24);
        if (opcode == 0x225) { operand(32); operand(64, 2); }
        if (opcode == 0x625) operand(64);
        if (opcode == 0x825) operand(64, 2);
        break;
    case 0x83b:
        operand(16, input.Bits(72, 2) == 1 ? 2 : 4); operand(24); break;
    case 0x23c:
        operand(16, 2); operand(24, input.Bits(75, 1) ? 4 : 2);
        operand(32, input.Bits(75, 1) ? 2 : 1); operand(64, 2); break;
    case 0x388: case 0x984: {
        const u32 width = input.Bits(73, 3), count = width == 6 ? 4 : width == 5 ? 2 : 1;
        operand(opcode == 0x388 ? 32 : 16, count); operand(24); break;
    }
    case 0x387: case 0x983:
        operand(opcode == 0x387 ? 32 : 16); operand(24); break;
    case 0x381: case 0x386: case 0x981: case 0x986: {
        const u32 width = input.Bits(73, 3), count = width == 6 ? 4 : width == 5 ? 2 : 1;
        operand(opcode == 0x386 || opcode == 0x986 ? 32 : 16, count);
        // Register-address forms 0x381/0x386 require a 64-bit pair.
        // Other forms use the actual extended-address bit at 90, not
        // the size-mode bit at 72. Include both halves in the spill plan.
        operand(24, opcode == 0x381 || opcode == 0x386 ||
                          input.Bits(90, 1) ? 2 : 1);
        break;
    }
    default:
        result.valid = false; return result;
    }
    std::array<bool, 256> used{};
    std::array<bool, 64> groups{};
    for (const auto [bit, count] : operands) {
        const u32 reg = input.Bits(bit, 8);
        if (reg == 255) continue;
        if (reg + count > 255) { result.valid = false; return result; }
        for (u32 component = 0; component < count; ++component) {
            used[reg + component] = true;
            if (reg + component >= reserved) groups[(reg + component) / 4] = true;
        }
    }
    std::array<u32, 64> mapped{};
    for (u32 group = 0; group < groups.size(); ++group) {
        if (!groups[group]) continue;
        u32 borrow = reserved;
        while (borrow >= 4) {
            borrow -= 4;
            if (!used[borrow] && !used[borrow + 1] && !used[borrow + 2] && !used[borrow + 3]) break;
        }
        if (used[borrow] || used[borrow + 1] || used[borrow + 2] || used[borrow + 3]) {
            result.valid = false; return result;
        }
        mapped[group] = borrow;
        for (u32 component = 0; component < 4; ++component) {
            const u32 low = borrow + component, high = group * 4 + component;
            used[low] = true;
            result.prefix.push_back(NativeRegisterCopy(2, low, 0, low));
            result.prefix.push_back(NativeRegisterCopy(0, low, 1, high));
            result.suffix.push_back(NativeRegisterCopy(1, high, 0, low));
            result.suffix.push_back(NativeRegisterCopy(0, low, 2, low));
        }
    }
    // A multi-register operand is encoded by one base register. The
    // Maxwell instruction implicitly reads/writes the following registers;
    // rewriting just the base is safe only when every component remains
    // contiguous after a spill. In particular, a pair crossing reserved
    // or two borrowed quartets would otherwise silently address the wrong
    // physical GPRs.
    const auto physical = [&](u32 guest) {
        return groups[guest / 4] ? mapped[guest / 4] + guest % 4 : guest;
    };
    for (const auto [bit, count] : operands) {
        const u32 reg = input.Bits(bit, 8);
        if (reg == 255 || count == 1) continue;
        const u32 base = physical(reg);
        if (base + count > 255) {
            result.valid = false;
            return result;
        }
        for (u32 part = 1; part < count; ++part) {
            if (physical(reg + part) != base + part) {
                result.valid = false;
                return result;
            }
        }
    }
    for (const auto [bit, count] : operands) {
        const u32 reg = input.Bits(bit, 8);
        if (reg == 255 || !groups[reg / 4]) continue;
        const u32 replacement = mapped[reg / 4] + reg % 4;
        const u32 word = bit / 32, shift = bit % 32;
        result.instruction.words[word] = (result.instruction.words[word] & ~(255U << shift)) | (replacement << shift);
    }
    return result;
}
} // namespace Shader::Ampere
