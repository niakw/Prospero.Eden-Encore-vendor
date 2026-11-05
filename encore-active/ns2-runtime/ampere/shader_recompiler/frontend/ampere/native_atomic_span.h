// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// P2.3: carry the complete native Compute atomic word span from real IR
// to the CURRENT QMD3 resource plan, not just a reflected SSBO index.
// Arbitrary dynamic byte offsets remain closed: the shared backend has
// no SPIR-V runtime bounds predicate. A 32-bit offset explicitly masked
// with an aligned constant AND is a provably bounded exception.
// No Maxwell / Switch 1 code includes this header.
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Ampere {
struct NativeAtomicSpan {
    u32 binding{};
    // Immediate byte offset or a proven inclusive U32 upper bound of
    // a masked dynamic byte offset. Verifying the MAXIMUM protects every
    // possible index on the live QMD3 storage buffer.
    u32 offset{};
    u32 width{};
    friend bool operator==(const NativeAtomicSpan&, const NativeAtomicSpan&) = default;
};

// A dynamic atomic is safe WITHOUT a SPIR-V runtime bounds predicate when
// its final value is a 32-bit AND with a constant, word-aligned bit mask.
// Every possible result of (dynamic & mask) lies in [0,mask] and retains
// zero low alignment bits. This is a proof over the actual emitted operand,
// not an estimate based on a register name, game, or previously sized SSBO.
// Other arithmetic (including additions, PHIs and unknown register ranges)
// remains rejected: it could overflow or address arbitrary GPU memory.
[[nodiscard]] inline std::optional<u32> NativeQmdAtomicMaximumByteOffset(
    const Shader::IR::Value& raw, u32 width) {
    if (width != 4 && width != 8) return std::nullopt;
    const auto value = raw.Resolve();
    if (value.IsImmediate()) {
        if (value.Type() != Shader::IR::Type::U32) return std::nullopt;
        return value.U32();
    }
    const auto* const instruction = value.TryInstRecursive();
    if (!instruction ||
        instruction->GetOpcode() != Shader::IR::Opcode::BitwiseAnd32)
        return std::nullopt;
    const auto lhs = instruction->Arg(0).Resolve();
    const auto rhs = instruction->Arg(1).Resolve();
    // Exactly one input must be a real U32 literal. The other operand may
    // be any 32-bit guest value, including a computed register expression.
    if (lhs.IsImmediate() == rhs.IsImmediate()) return std::nullopt;
    const auto constant = lhs.IsImmediate() ? lhs : rhs;
    if (constant.Type() != Shader::IR::Type::U32) return std::nullopt;
    const u32 mask = constant.U32();
    if ((mask & (width - 1U)) ||
        mask > std::numeric_limits<u32>::max() - width)
        return std::nullopt;
    return mask;
}

[[nodiscard]] inline std::optional<std::vector<NativeAtomicSpan>>
CollectNativeQmdAtomicSpans(const Shader::IR::Program& program) {
    using Shader::IR::Opcode;
    std::vector<NativeAtomicSpan> spans;
    for (const auto* block : program.blocks) {
        if (!block) return std::nullopt;
        for (const auto& inst : block->Instructions()) {
            const Opcode op = inst.GetOpcode();
            if (op < Opcode::GlobalAtomicIAdd32 || op > Opcode::StorageAtomicMaxF32x2)
                continue;
            // Unresolved GlobalAtomic has no SPIR-V emitter; no false proof.
            if (op < Opcode::StorageAtomicIAdd32)
                return std::nullopt;
            // The shared U32x2 path is a non-atomic load/store fallback.
            if (op >= Opcode::StorageAtomicIAdd32x2 &&
                op <= Opcode::StorageAtomicExchange32x2)
                return std::nullopt;
            const auto binding = inst.Arg(0);
            const auto offset = inst.Arg(1);
            // No general runtime-sized guard exists: only fixed offsets
            // or conservatively bounded, aligned AND masks are admissible.
            // The resource planner checks the worst possible byte span,
            // not just one observed invocation's offset.
            if (!binding.IsImmediate() ||
                binding.Type() != Shader::IR::Type::U32)
                return std::nullopt;
            const bool wide = op >= Opcode::StorageAtomicIAdd64 &&
                              op <= Opcode::StorageAtomicExchange64;
            const u32 width = wide ? 8U : 4U;
            const auto maximum_offset =
                NativeQmdAtomicMaximumByteOffset(offset, width);
            if (!maximum_offset) return std::nullopt;
            const u32 slot = binding.U32();
            const u32 byte_offset = *maximum_offset;
            if (slot >= program.info.storage_buffers_descriptors.size() ||
                (byte_offset & (width - 1U)) ||
                byte_offset > std::numeric_limits<u32>::max() - width)
                return std::nullopt;
            const auto& desc = program.info.storage_buffers_descriptors[slot];
            if (!desc.is_written || desc.count != 1)
                return std::nullopt;
            spans.push_back({slot, byte_offset, width});
            if (spans.size() > 65536)
                return std::nullopt; // bounded untrusted guest shader IR
        }
    }
    return spans;
}

// Each span.offset is a maximum (not a sampled dynamic value) after the
// proof above; the same live-size check therefore covers every valid runtime
// offset generated by the AND, including the buffer's last aligned word.
// Plan is a real, read-back QMD3 binding vector: known_size, gpu_address,
// bytes and is_written originate from authenticated guest descriptor words.
// Reject a smaller live buffer before the renderer can submit an atomic.
// The guest may rewrite the descriptor: the existing epoch/same-binding
// checks MUST still execute before Vulkan submission.
template <typename StorageBindings>
[[nodiscard]] bool NativeQmdAtomicSpansFitBindings(
    std::span<const NativeAtomicSpan> spans, const StorageBindings& bindings) noexcept {
    for (const auto& span : spans) {
        if (span.binding >= bindings.size() ||
            (span.width != 4 && span.width != 8) ||
            (span.offset & (span.width - 1U)))
            return false;
        const auto& storage = bindings[span.binding];
        if (!storage.known_size || !storage.is_written ||
            !storage.gpu_address || storage.bytes < span.width ||
            span.offset > storage.bytes - span.width)
            return false;
    }
    return true;
}
} // namespace Shader::Ampere
