// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Resolve the canonical NVN/QMD3 *pointer-derived* global atomic forms to
// a genuine bound SSBO slot. This is a shared GPU operation, not a title fix.
// All other guest addresses remain unresolved and fail the existing native
// atomic gate; never infer a mapping from a numeric guest address alone.
#include <limits>
#include <optional>
#include <utility>

#include "shader_recompiler/frontend/ampere/native_atomic_span.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/ir/value.h"

namespace Shader::Ampere {
namespace NativeAtomic {
using Shader::IR::Inst;
using Shader::IR::Opcode;
using Shader::IR::Value;

struct Address {
    u32 bank{};
    u32 descriptor{};
    Value byte_offset{u32{0}};
};

[[nodiscard]] inline const Inst* Definition(const Value& value) {
    return value.Resolve().TryInstRecursive();
}

[[nodiscard]] inline std::optional<Address> Resolve(const Value& pointer) {
    Value base = pointer.Resolve();
    Value relative{u32{0}};
    if (const auto* const add = Definition(base);
        add && add->GetOpcode() == Opcode::IAdd64) {
        const Value left = add->Arg(0).Resolve();
        const Value right = add->Arg(1).Resolve();
        if (right.IsImmediate()) {
            if (right.Type() != Shader::IR::Type::U64 ||
                right.U64() > std::numeric_limits<u32>::max())
                return std::nullopt;
            relative = Value{static_cast<u32>(right.U64())};
        } else {
            // The IR conversion is an exact ZERO extension of a U32
            // byte offset. Never truncate an arbitrary U64 pointer.
            const auto* const convert = Definition(right);
            if (!convert || convert->GetOpcode() != Opcode::ConvertU64U32)
                return std::nullopt;
            relative = convert->Arg(0).Resolve();
            if (relative.Type() != Shader::IR::Type::U32) return std::nullopt;
        }
        base = left;
    }
    const auto* const packed = Definition(base);
    if (!packed || packed->GetOpcode() != Opcode::PackUint2x32)
        return std::nullopt;
    const auto* const vector = Definition(packed->Arg(0));
    if (!vector || vector->GetOpcode() != Opcode::GetCbufU32x2)
        return std::nullopt;
    const Value bank = vector->Arg(0).Resolve();
    const Value offset = vector->Arg(1).Resolve();
    if (!bank.IsImmediate() || bank.Type() != Shader::IR::Type::U32 ||
        !offset.IsImmediate() || offset.Type() != Shader::IR::Type::U32)
        return std::nullopt;
    return Address{bank.U32(), offset.U32(), relative};
}

[[nodiscard]] inline bool Rewrite(Shader::IR::Program& program) {
    static_assert(static_cast<u32>(Opcode::GlobalAtomicExchange32) -
                      static_cast<u32>(Opcode::GlobalAtomicIAdd32) ==
                  static_cast<u32>(Opcode::StorageAtomicExchange32) -
                      static_cast<u32>(Opcode::StorageAtomicIAdd32));
    const auto& info = program.info;
    for (auto* const block : program.blocks) {
        if (!block) return false;
        for (auto it = block->begin(); it != block->end();) {
            auto& inst = *it++;
            const auto op = inst.GetOpcode();
            const bool integer32 = op >= Opcode::GlobalAtomicIAdd32 &&
                                   op <= Opcode::GlobalAtomicExchange32;
            const bool integer64 = op >= Opcode::GlobalAtomicIAdd64 &&
                                   op <= Opcode::GlobalAtomicExchange64;
            if (!integer32 && !integer64) continue;
            const u32 width = integer64 ? 8U : 4U;
            const auto address = Resolve(inst.Arg(0));
            const auto max_offset = address ?
                NativeQmdAtomicMaximumByteOffset(address->byte_offset, width) :
                std::nullopt;
            if (!address || !max_offset || address->bank >= 8 ||
                !(info.constant_buffer_mask & (1U << address->bank)) ||
                (address->descriptor & 15U) ||
                (*max_offset & (width - 1U)) ||
                *max_offset > std::numeric_limits<u32>::max() - width)
                return false;
            bool declared = false;
            for (const auto& record : info.constant_buffer_descriptors) {
                if (record.count && record.index <= address->bank &&
                    address->bank - record.index < record.count)
                    declared = true;
            }
            if (!declared || info.constant_buffer_used_sizes[address->bank] <
                                 address->descriptor + 16U)
                return false;
            // Reflection is indexed by flattened SSBO slot, not by CBUF
            // bank. Bind only a real writable, in-order QMD3 descriptor.
            std::optional<u32> slot;
            for (u32 i = 0; i < info.storage_buffers_descriptors.size(); ++i) {
                const auto& rec = info.storage_buffers_descriptors[i];
                if (rec.cbuf_index == address->bank &&
                    rec.cbuf_offset == address->descriptor) {
                    if (slot) return false; // ambiguous alias
                    slot = i;
                }
            }
            if (!slot || *slot >= info.nvn_buffer_used.size() ||
                !info.nvn_buffer_used.test(*slot)) return false;
            const auto& desc = info.storage_buffers_descriptors[*slot];
            if (desc.count != 1 || !desc.is_written ||
                (info.nvn_buffer_base & 15U) ||
                info.nvn_buffer_base > 0x10000U - 16U ||
                *slot > (0x10000U - 16U - info.nvn_buffer_base) / 16U ||
                desc.cbuf_offset != info.nvn_buffer_base + *slot * 16U)
                return false;
            Opcode new_op{};
            if (integer32) {
                new_op = static_cast<Opcode>(
                    static_cast<u32>(Opcode::StorageAtomicIAdd32) +
                    static_cast<u32>(op) - static_cast<u32>(Opcode::GlobalAtomicIAdd32));
            } else {
                // Unlike 32-bit atomics, the 64-bit enum is not guaranteed
                // to have identical offsets. Map only real Vulkan atomics;
                // never translate unimplemented increment/decrement variants.
                switch (op) {
                case Opcode::GlobalAtomicIAdd64: new_op=Opcode::StorageAtomicIAdd64; break;
                case Opcode::GlobalAtomicSMin64: new_op=Opcode::StorageAtomicSMin64; break;
                case Opcode::GlobalAtomicUMin64: new_op=Opcode::StorageAtomicUMin64; break;
                case Opcode::GlobalAtomicSMax64: new_op=Opcode::StorageAtomicSMax64; break;
                case Opcode::GlobalAtomicUMax64: new_op=Opcode::StorageAtomicUMax64; break;
                case Opcode::GlobalAtomicAnd64: new_op=Opcode::StorageAtomicAnd64; break;
                case Opcode::GlobalAtomicOr64: new_op=Opcode::StorageAtomicOr64; break;
                case Opcode::GlobalAtomicXor64: new_op=Opcode::StorageAtomicXor64; break;
                case Opcode::GlobalAtomicExchange64: new_op=Opcode::StorageAtomicExchange64; break;
                default: return false;
                }
            }
            // Keep the original value and use-def edges. The genuine IR
            // block owns the new instruction until later SPIR-V emission.
            const Value replacement{&*block->PrependNewInst(
                Shader::IR::Block::InstructionList::s_iterator_to(inst), new_op,
                {Value{*slot}, address->byte_offset, inst.Arg(1)})};
            inst.ReplaceUsesWith(replacement);
            inst.Invalidate();
        }
    }
    return true;
}
} // namespace NativeAtomic
} // namespace Shader::Ampere
