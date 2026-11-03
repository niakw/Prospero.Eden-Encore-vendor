// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QMD3 offers eight real Compute CBUF banks. The generic Maxwell collector
// creates fourteen for register-indexed loads. Narrow reflection ONLY after
// translating a reviewed NS2 Compute IR; never patch the shared collector or
// introduce fabricated guest uniform buffers.
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Ampere {
inline constexpr u32 kNativeQmdIndirectCbufBanks = 8;

[[nodiscard]] inline bool NormalizeNativeQmdIndirectCbufMetadata(Shader::Info& info) {
    if (!info.uses_cbuf_indirect) return true;
    // Each original slot must be a distinct, singleton Maxwell descriptor.
    // A direct CBUF at 14..17 or missing slot is not silently remapped.
    if (info.constant_buffer_descriptors.size() != Shader::Info::MAX_INDIRECT_CBUFS ||
        info.constant_buffer_mask != ((u32{1} << Shader::Info::MAX_INDIRECT_CBUFS) - 1U))
        return false;
    for (u32 bank = 0; bank < Shader::Info::MAX_INDIRECT_CBUFS; ++bank) {
        const auto& desc = info.constant_buffer_descriptors[bank];
        if (desc.index != bank || desc.count != 1)
            return false;
    }
    while (info.constant_buffer_descriptors.size() > kNativeQmdIndirectCbufBanks)
        info.constant_buffer_descriptors.pop_back();
    info.constant_buffer_mask = (u32{1} << kNativeQmdIndirectCbufBanks) - 1U;
    for (u32 bank = kNativeQmdIndirectCbufBanks;
         bank < Shader::Info::MAX_INDIRECT_CBUFS; ++bank)
        info.constant_buffer_used_sizes[bank] = 0;
    // The original 64-KiB used-size requirements for all eight possible
    // indirect banks stay intact and are checked against QMD3 at binding.
    return true;
}

[[nodiscard]] inline bool NormalizeNativeQmdIndirectCbufIR(Shader::IR::Program& program) {
    if (!program.info.uses_cbuf_indirect) return true;
    // The collector's all-bank declaration loses the distinction between
    // implicit indirect slots and explicit direct loads. Check the actual
    // IR first: a literal bank >=8 is not a QMD3 resource, even if another
    // dynamic load made it appear in the original fourteen-slot metadata.
    for (const Shader::IR::Block* const block : program.blocks) {
        for (const Shader::IR::Inst& inst : block->Instructions()) {
            switch (inst.GetOpcode()) {
            case Shader::IR::Opcode::GetCbufU8:
            case Shader::IR::Opcode::GetCbufS8:
            case Shader::IR::Opcode::GetCbufU16:
            case Shader::IR::Opcode::GetCbufS16:
            case Shader::IR::Opcode::GetCbufU32:
            case Shader::IR::Opcode::GetCbufF32:
            case Shader::IR::Opcode::GetCbufU32x2:
                if (inst.Arg(0).IsImmediate() &&
                    inst.Arg(0).U32() >= kNativeQmdIndirectCbufBanks)
                    return false;
                break;
            default:
                break;
            }
        }
    }
    return NormalizeNativeQmdIndirectCbufMetadata(program.info);
}
} // namespace Shader::Ampere
