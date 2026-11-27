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

// Maxwell IR may pack an already-vectorized CBUF address, or assemble the
// exact same 64-bit address from two independent scalar CBUF words. Accept
// either *only when* both halves are contiguous and in one real CBUF bank.
[[nodiscard]] inline std::optional<std::pair<u32, u32>> DescriptorPointer(
    const Value& raw) {
    const auto* const packed = Definition(raw);
    if (!packed || packed->GetOpcode() != Opcode::PackUint2x32)
        return std::nullopt;
    const auto* const vector = Definition(packed->Arg(0));
    if (!vector) return std::nullopt;
    if (vector->GetOpcode() == Opcode::GetCbufU32x2) {
        const auto bank = vector->Arg(0).Resolve();
        const auto offset = vector->Arg(1).Resolve();
        if (!bank.IsImmediate() || bank.Type() != Shader::IR::Type::U32 ||
            !offset.IsImmediate() || offset.Type() != Shader::IR::Type::U32)
            return std::nullopt;
        return std::pair{bank.U32(), offset.U32()};
    }
    if (vector->GetOpcode() != Opcode::CompositeConstructU32x2)
        return std::nullopt;
    const auto* const low = Definition(vector->Arg(0));
    const auto* const high = Definition(vector->Arg(1));
    if (!low || !high || low->GetOpcode() != Opcode::GetCbufU32 ||
        high->GetOpcode() != Opcode::GetCbufU32)
        return std::nullopt;
    const Value l_bank = low->Arg(0).Resolve();
    const Value h_bank = high->Arg(0).Resolve();
    const Value l_offset = low->Arg(1).Resolve();
    const Value h_offset = high->Arg(1).Resolve();
    if (!l_bank.IsImmediate() || !h_bank.IsImmediate() ||
        !l_offset.IsImmediate() || !h_offset.IsImmediate() ||
        l_bank.Type() != Shader::IR::Type::U32 ||
        h_bank.Type() != Shader::IR::Type::U32 ||
        l_offset.Type() != Shader::IR::Type::U32 ||
        h_offset.Type() != Shader::IR::Type::U32 ||
        l_bank.U32() != h_bank.U32() ||
        l_offset.U32() > std::numeric_limits<u32>::max() - 4U ||
        h_offset.U32() != l_offset.U32() + 4U)
        return std::nullopt;
    return std::pair{l_bank.U32(), l_offset.U32()};
}

[[nodiscard]] inline std::optional<Address> Resolve(const Value& pointer) {
    Value base = pointer.Resolve();
    Value relative{u32{0}};
    if (const auto* const add = Definition(base);
        add && add->GetOpcode() == Opcode::IAdd64) {
        const Value left = add->Arg(0).Resolve();
        const Value right = add->Arg(1).Resolve();
        // IAdd64 commutes: both legitimate compiler operand orders must
        // resolve to the same authenticated descriptor and byte offset.
        const bool left_pointer = DescriptorPointer(left).has_value();
        const bool right_pointer = DescriptorPointer(right).has_value();
        if (left_pointer == right_pointer) return std::nullopt;
        base = left_pointer ? left : right;
        const Value increment = left_pointer ? right : left;
        if (increment.IsImmediate()) {
            if (increment.Type() != Shader::IR::Type::U64 ||
                increment.U64() > std::numeric_limits<u32>::max())
                return std::nullopt;
            relative = Value{static_cast<u32>(increment.U64())};
        } else {
            // A 64-bit guest displacement is admitted only as an exact
            // zero-extension of an independently bounded U32 value.
            const auto* const convert = Definition(increment);
            if (!convert || convert->GetOpcode() != Opcode::ConvertU64U32)
                return std::nullopt;
            relative = convert->Arg(0).Resolve();
            if (relative.Type() != Shader::IR::Type::U32) return std::nullopt;
        }
    }
    const auto descriptor = DescriptorPointer(base);
    if (!descriptor) return std::nullopt;
    return Address{descriptor->first, descriptor->second, relative};
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
            const bool compare_exchange32 =
                op == Opcode::GlobalAtomicCompareExchange32;
            // These seven float operations all use the real shared CAS
            // emitter on a single 32-bit word: scalar F32 and two packed
            // half lanes, whether the guest presents F16x2 or F32x2 input.
            // Verify the CAS helper uses OpPackHalf2x16/OpUnpackHalf2x16;
            // never treat F32x2 as an unprotected 8-byte storage vector.
            const bool float_cas = op == Opcode::GlobalAtomicAddF32 ||
                op == Opcode::GlobalAtomicAddF16x2 ||
                op == Opcode::GlobalAtomicAddF32x2 ||
                op == Opcode::GlobalAtomicMinF16x2 ||
                op == Opcode::GlobalAtomicMinF32x2 ||
                op == Opcode::GlobalAtomicMaxF16x2 ||
                op == Opcode::GlobalAtomicMaxF32x2;
            if (!integer32 && !integer64 && !float_cas && !compare_exchange32)
                continue;
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
            } else if (compare_exchange32) {
                new_op = Opcode::StorageAtomicCompareExchange32;
            } else if (float_cas) {
                // These all use the genuine shared SPIR-V CAS helper;
                // they never degrade to a non-atomic load/store.
                switch (op) {
                case Opcode::GlobalAtomicAddF32: new_op=Opcode::StorageAtomicAddF32; break;
                case Opcode::GlobalAtomicAddF16x2: new_op=Opcode::StorageAtomicAddF16x2; break;
                case Opcode::GlobalAtomicAddF32x2: new_op=Opcode::StorageAtomicAddF32x2; break;
                case Opcode::GlobalAtomicMinF16x2: new_op=Opcode::StorageAtomicMinF16x2; break;
                case Opcode::GlobalAtomicMinF32x2: new_op=Opcode::StorageAtomicMinF32x2; break;
                case Opcode::GlobalAtomicMaxF16x2: new_op=Opcode::StorageAtomicMaxF16x2; break;
                case Opcode::GlobalAtomicMaxF32x2: new_op=Opcode::StorageAtomicMaxF32x2; break;
                default: return false;
                }
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
            // CAS carries BOTH comparator and replacement value; dropping
            // either converts a conditional write into a different atomic.
            const Value replacement{&*block->PrependNewInst(
                Shader::IR::Block::InstructionList::s_iterator_to(inst), new_op,
                compare_exchange32 ?
                    std::initializer_list<Value>{Value{*slot}, address->byte_offset,
                                                 inst.Arg(1), inst.Arg(2)} :
                    std::initializer_list<Value>{Value{*slot}, address->byte_offset,
                                                 inst.Arg(1)})};
            // ReplaceUsesWith first clears the old operands and turns this
            // very instruction into an IR::Identity pointing to the new
            // atomic result. Invalidating it a second time would silently
            // discard the guest-visible returned *old value* and break all
            // instructions that consume the atomic return value.
            inst.ReplaceUsesWith(replacement);
        }
    }
    return true;
}
} // namespace NativeAtomic
} // namespace Shader::Ampere
