// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QMD3 offers eight real Compute CBUF banks. The generic Maxwell collector
// creates fourteen for register-indexed loads. Narrow reflection ONLY after
// translating a reviewed NS2 Compute IR; never patch the shared collector or
// introduce fabricated guest uniform buffers.
#include <algorithm>
#include <array>
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Ampere {
inline constexpr u32 kNativeQmdIndirectCbufBanks = 8;

// The same opt-in shader_recompiler library services Switch 1 and Switch 2.
// Only reviewed native Ampere callers may request stricter CBUF SPIR-V
// bounds; selecting the NS2 source overlay must never rewrite Maxwell's
// established shader behavior. The scope follows the synchronous shader
// emission on its calling thread, and restores the previous value even
// when EmitSPIRV throws. Other compiler workers are independent.
inline thread_local bool native_spirv_cbuf_bounds_active = false;
[[nodiscard]] inline bool NativeSpirvCbufBoundsActive() noexcept {
    return native_spirv_cbuf_bounds_active;
}
class NativeSpirvCbufBoundsScope {
public:
    NativeSpirvCbufBoundsScope() noexcept
        : previous{native_spirv_cbuf_bounds_active} {
        native_spirv_cbuf_bounds_active = true;
    }
    ~NativeSpirvCbufBoundsScope() {
        native_spirv_cbuf_bounds_active = previous;
    }
    NativeSpirvCbufBoundsScope(const NativeSpirvCbufBoundsScope&) = delete;
    NativeSpirvCbufBoundsScope& operator=(const NativeSpirvCbufBoundsScope&) = delete;
private:
    bool previous;
};

// SPIR-V can emit a 16-byte uniform-vector load even for a narrower source
// opcode when descriptor aliasing is unavailable. Conservatively round a
// statically addressed CBUF read to the complete 16-byte record. Zero means
// an invalid offset outside the hardware's 64-KiB shader-CBUF window.
[[nodiscard]] constexpr u32 NativeQmdStaticCbufReadExtent(u32 byte_offset) noexcept {
    return byte_offset < 0x10000U ? (byte_offset | 15U) + 1U : 0U;
}

[[nodiscard]] inline bool NormalizeNativeQmdIndirectCbufMetadata(Shader::Info& info,
                                                                u32 enabled_mask = 0xffU) {
    if (!info.uses_cbuf_indirect) return true;
    if (!enabled_mask || (enabled_mask & ~u32{0xff})) return false;
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
    // QMD3's enable mask can be sparse. Drop disabled banks from reflection
    // and Vulkan layout; never invent a guest CBUF to fill a missing slot.
    for (u32 bank = Shader::Info::MAX_INDIRECT_CBUFS; bank != 0; --bank) {
        const u32 index = bank - 1;
        if (index >= kNativeQmdIndirectCbufBanks ||
            !(enabled_mask & (u32{1} << index))) {
            info.constant_buffer_descriptors.erase(
                info.constant_buffer_descriptors.begin() + index);
            info.constant_buffer_used_sizes[index] = 0;
        }
    }
    info.constant_buffer_mask = enabled_mask;
    // Preserve the original 64-KiB size requirement for each live bank.
    // Shorter dynamic-offset backing still fails closed until bound checks
    // have their own native SPIR-V proof.
    return true;
}

[[nodiscard]] inline bool NormalizeNativeQmdIndirectCbufIR(Shader::IR::Program& program,
                                                          u32 enabled_mask = 0xffU) {
    // LDC/ULDC with an immediate bank is also a QMD3 launch dependency.
    // The generic Maxwell IR permits banks 0..17, but QMD3 exposes 0..7.
    // Refuse a missing/disabled bank BEFORE emitting SPIR-V; it must never
    // become a valid-looking native shader whose Vulkan binding will fail.
    if (enabled_mask & ~u32{0xff}) return false;
    if (!program.info.uses_cbuf_indirect) {
        u32 declared_mask{};
        for (const auto& desc : program.info.constant_buffer_descriptors) {
            if (!desc.count || desc.index >= kNativeQmdIndirectCbufBanks ||
                desc.count > kNativeQmdIndirectCbufBanks - desc.index)
                return false;
            for (u32 i = 0; i < desc.count; ++i) {
                const u32 bit = u32{1} << (desc.index + i);
                if ((declared_mask & bit) || !(enabled_mask & bit))
                    return false;
                declared_mask |= bit;
            }
        }
        return declared_mask == program.info.constant_buffer_mask;
    }
    // Check all actual CBUF loads before narrowing the generic fourteen-bank
    // reflection. A dynamic bank selector can touch any enabled slot; the
    // offset may be narrowed only if EVERY load uses a literal byte offset.
    // The backend may load an aligned 16-byte vector for a narrow operation.
    std::array<u32, kNativeQmdIndirectCbufBanks> read_extents{};
    bool has_register_indexed_read = false;
    bool all_offsets_static = true;
    for (const Shader::IR::Block* const block : program.blocks) {
        for (const Shader::IR::Inst& inst : block->Instructions()) {
            switch (inst.GetOpcode()) {
            case Shader::IR::Opcode::GetCbufU8:
            case Shader::IR::Opcode::GetCbufS8:
            case Shader::IR::Opcode::GetCbufU16:
            case Shader::IR::Opcode::GetCbufS16:
            case Shader::IR::Opcode::GetCbufU32:
            case Shader::IR::Opcode::GetCbufF32:
            case Shader::IR::Opcode::GetCbufU32x2: {
                const auto index = inst.Arg(0);
                const auto offset = inst.Arg(1);
                if (index.IsImmediate()) {
                    const u32 bank = index.U32();
                    if (bank >= kNativeQmdIndirectCbufBanks ||
                        !(enabled_mask & (u32{1} << bank)))
                        return false;
                } else {
                    has_register_indexed_read = true;
                }
                if (!offset.IsImmediate()) {
                    all_offsets_static = false;
                    break;
                }
                const u32 extent = NativeQmdStaticCbufReadExtent(offset.U32());
                if (!extent) return false;
                if (index.IsImmediate()) {
                    auto& current = read_extents[index.U32()];
                    current = (std::max)(current, extent);
                } else {
                    for (u32 bank = 0; bank < kNativeQmdIndirectCbufBanks; ++bank)
                        if (enabled_mask & (u32{1} << bank))
                            read_extents[bank] = (std::max)(read_extents[bank], extent);
                }
                break;
            }
            default:
                break;
            }
        }
    }
    if (!NormalizeNativeQmdIndirectCbufMetadata(program.info, enabled_mask))
        return false;
    const auto& info = program.info;
    // NVN global memory, image and storage handle readers use CBUF regions
    // outside the explicit GetCbuf IR opcodes. Keep the full 64-KiB
    // conservative requirement for these layouts and dynamic offsets.
    if (!has_register_indexed_read || !all_offsets_static ||
        info.uses_global_memory || info.stores_global_memory ||
        !info.storage_buffers_descriptors.empty() ||
        !info.texture_buffer_descriptors.empty() ||
        !info.image_buffer_descriptors.empty() ||
        !info.texture_descriptors.empty() ||
        !info.image_descriptors.empty())
        return true;
    for (u32 bank = 0; bank < kNativeQmdIndirectCbufBanks; ++bank) {
        if (!(enabled_mask & (u32{1} << bank))) continue;
        if (!read_extents[bank]) return false;
        program.info.constant_buffer_used_sizes[bank] = read_extents[bank];
    }
    return true;
}
} // namespace Shader::Ampere
