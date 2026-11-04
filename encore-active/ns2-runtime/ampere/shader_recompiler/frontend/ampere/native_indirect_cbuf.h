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

// Scalar oracle for the exact native SPIR-V global access condition.
// SPIR-V applies the same unsigned byte-span and alignment predicate
// before constructing an OpAccessChain. This is a host-side regression
// witness only: actual guest addresses are checked by emitted SPIR-V.
[[nodiscard]] constexpr bool NativeQmdGlobalAccessWithinStorage(
    u64 guest_base, u32 storage_bytes, u64 address, u32 access_bytes) noexcept {
    constexpr u64 limit = u64{1} << 49;
    if (!guest_base || guest_base >= limit ||
        storage_bytes > limit - guest_base ||
        (access_bytes != 4 && access_bytes != 8 && access_bytes != 16) ||
        storage_bytes < access_bytes || address < guest_base)
        return false;
    const u64 offset = address - guest_base;
    return offset <= storage_bytes - access_bytes &&
           (offset & (access_bytes - 1U)) == 0;
}


// The common Maxwell translator adds NVN global SSBO records only when
// uses_global_memory is set. A genuine native Compute program may WRITE
// globals without reading them, so stores_global_memory alone is not enough
// for its SPIR-V accessor and QMD3 descriptor plan to agree.
//
// Infer only the documented NVN Compute driver CBUF layout (bank 0, byte
// offsets 0x310 + 16 * slot) and only when the real shader reflection proves
// a contiguous set of live slots. Do not invent an absent QMD CBUF, guess
// SSBO sizes, or mix this layout with unrelated reflected buffers.
// This must run on the native Compute IR before SPIR-V emission; NS1 never
// calls it.
[[nodiscard]] inline bool NormalizeNativeQmdGlobalStoreOnlyInfo(
    Shader::Info& info, u32 enabled_cbuf_mask) {
    if (!info.stores_global_memory || info.uses_global_memory)
        return true;
    if ((enabled_cbuf_mask & ~u32{0xff}) ||
        !(enabled_cbuf_mask & 1U) || !(info.constant_buffer_mask & 1U) ||
        !info.storage_buffers_descriptors.empty() ||
        !info.nvn_buffer_used.any())
        return false;
    bool bank_zero_declared{};
    for (const auto& cbuf : info.constant_buffer_descriptors) {
        if (cbuf.index == 0 && cbuf.count == 1)
            bank_zero_declared = true;
    }
    if (!bank_zero_declared)
        return false;
    u32 slot_count{};
    for (u32 i = 0; i < info.nvn_buffer_used.size(); ++i) {
        if (!info.nvn_buffer_used.test(i))
            continue;
        // NVN global accessors index the FLATTENED array by slot, not
        // the original hardware ID. A gap would bind the wrong buffer.
        if (i != slot_count)
            return false;
        ++slot_count;
    }
    if (!slot_count || slot_count > Shader::Info::MAX_SSBOS ||
        0x310U + slot_count * 16U > 0x10000U)
        return false;
    for (u32 i = 0; i < slot_count; ++i) {
        info.storage_buffers_descriptors.push_back({
            .cbuf_index = 0,
            .cbuf_offset = 0x310U + i * 16U,
            .count = 1,
            .is_written = true,
        });
    }
    info.constant_buffer_used_sizes[0] = std::max(
        info.constant_buffer_used_sizes[0], 0x310U + slot_count * 16U);
    info.uses_global_memory = true;
    info.used_constant_buffer_types |= Shader::IR::Type::U32 |
                                       Shader::IR::Type::U32x2;
    return true;
}


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

// A U32/U64 load near the final word may straddle the 64-KiB hardware
// window even when its start lies inside a 16-byte SPIR-V vector.
// Native U8 still permits the last byte. Do not extend the guest CBUF.
[[nodiscard]] constexpr u32 NativeQmdStaticCbufReadExtent(
    Shader::IR::Opcode op, u32 byte_offset) noexcept {
    const u32 width = (op == Shader::IR::Opcode::GetCbufU8 ||
                       op == Shader::IR::Opcode::GetCbufS8) ? 1U :
                      (op == Shader::IR::Opcode::GetCbufU16 ||
                       op == Shader::IR::Opcode::GetCbufS16) ? 2U :
                       op == Shader::IR::Opcode::GetCbufU32x2 ? 8U : 4U;
    // U32x2 can start at the second word of a backing U32x2 vector.
    // The native SPIR-V emitter fetches two adjacent, independently
    // bounded 8-byte vectors and assembles the actual guest words. It
    // never rounds offset 4/12 to an unrelated 64-bit uniform value.
    // 32-bit word alignment is required; sub-word native pair loads
    // remain unproven and must fail closed.
    if (op == Shader::IR::Opcode::GetCbufU32x2 && (byte_offset & 3U))
        return 0U;
    // Only the wide pair requires a new uniform vector at the next
    // 8-/16-byte boundary. Other Maxwell-compatible sub-word loads
    // retain their original aligned-read extent, without expanding
    // CBUF residency for unrelated U8/U16/U32 instructions.
    const u32 last_reflected_byte =
        op == Shader::IR::Opcode::GetCbufU32x2
            ? byte_offset + width - 1U : byte_offset;
    return byte_offset <= 0x10000U - width
        ? NativeQmdStaticCbufReadExtent(last_reflected_byte) : 0U;
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
        // Reflection without uses_cbuf_indirect must NEVER hide an actual
        // register-selected CBUF operation: the SPIR-V backend would otherwise
        // call an uncreated indirect-load function. Also check the 64-KiB
        // offset domain for statically addressed CBUFs in this path.
        u32 referenced_mask{};
        std::array<u32, kNativeQmdIndirectCbufBanks> required_sizes{};
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
                    const auto bank_value = inst.Arg(0);
                    const auto offset_value = inst.Arg(1);
                    if (!bank_value.IsImmediate()) return false;
                    const u32 bank = bank_value.U32();
                    if (bank >= kNativeQmdIndirectCbufBanks ||
                        !(enabled_mask & (u32{1} << bank)))
                        return false;
                    const u32 extent = offset_value.IsImmediate()
                        ? NativeQmdStaticCbufReadExtent(inst.GetOpcode(),offset_value.U32())
                        : 0x10000U;
                    if (!extent) return false;
                    referenced_mask |= u32{1} << bank;
                    required_sizes[bank] = (std::max)(required_sizes[bank], extent);
                    break;
                }
                default:
                    break;
                }
            }
        }
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
        // Every immediate guest load needs a reflected and bound UBO.
        // A QMD enable bit alone cannot authenticate an unbound bank.
        if (declared_mask != program.info.constant_buffer_mask ||
            (referenced_mask & ~declared_mask))
            return false;
        for (u32 bank = 0; bank < kNativeQmdIndirectCbufBanks; ++bank)
            program.info.constant_buffer_used_sizes[bank] =
                (std::max)(program.info.constant_buffer_used_sizes[bank],
                           required_sizes[bank]);
        return true;
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
                const u32 extent = NativeQmdStaticCbufReadExtent(inst.GetOpcode(),offset.U32());
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
