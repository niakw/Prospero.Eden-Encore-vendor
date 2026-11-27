// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// P2.3: carry the complete native Compute atomic word span from real IR
// to the CURRENT QMD3 resource plan, not just a reflected SSBO index.
// Arbitrary dynamic byte offsets remain closed: the shared backend has
// no SPIR-V runtime bounds predicate. A 32-bit offset explicitly masked
// with an aligned AND or literal-sized UExtract, optionally shifted by
// a constant and plus at most one aligned base, is a bounded exception.
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
    // a masked / literal-extracted / constant-shifted byte offset.
    // Verifying the MAXIMUM protects every possible index on the
    // live QMD3 storage buffer.
    u32 offset{};
    u32 width{};
    friend bool operator==(const NativeAtomicSpan&, const NativeAtomicSpan&) = default;
};

// Read a genuinely literal mask from the exact U32 AND IR node; its
// other operand is allowed to be dynamic, but no guessed register range.
[[nodiscard]] inline std::optional<u32> NativeQmdAtomicLiteralAndMask(
    const Shader::IR::Value& raw) {
    const auto* instruction = raw.Resolve().TryInstRecursive();
    if (!instruction ||
        instruction->GetOpcode() != Shader::IR::Opcode::BitwiseAnd32)
        return std::nullopt;
    const auto lhs = instruction->Arg(0).Resolve();
    const auto rhs = instruction->Arg(1).Resolve();
    if (lhs.IsImmediate() == rhs.IsImmediate()) return std::nullopt;
    const auto constant = lhs.IsImmediate() ? lhs : rhs;
    if (constant.Type() != Shader::IR::Type::U32) return std::nullopt;
    return constant.U32();
}

// The shared SPIR-V backend emits OpBitFieldUExtract(base,offset,count).
// With literal offset/count, any dynamic U32 input yields a result in
// [0,2^count-1]. Validate the actual 32-bit bit-field extent: out-of-range
// SPIR-V operands are undefined and cannot establish an atomic bound.
[[nodiscard]] inline std::optional<u32> NativeQmdAtomicLiteralExtractMax(
    const Shader::IR::Value& raw) {
    const auto* instruction = raw.Resolve().TryInstRecursive();
    if (!instruction ||
        instruction->GetOpcode() != Shader::IR::Opcode::BitFieldUExtract)
        return std::nullopt;
    const auto offset = instruction->Arg(1).Resolve();
    const auto count = instruction->Arg(2).Resolve();
    if (!offset.IsImmediate() || !count.IsImmediate() ||
        offset.Type() != Shader::IR::Type::U32 ||
        count.Type() != Shader::IR::Type::U32)
        return std::nullopt;
    const u32 first = offset.U32();
    const u32 bits = count.U32();
    if (first >= 32 || !bits || bits > 32 - first)
        return std::nullopt;
    return bits == 32 ? std::numeric_limits<u32>::max()
                      : (u32{1} << bits) - 1U;
}

[[nodiscard]] inline std::optional<u32> NativeQmdAtomicIndexMaximum(
    const Shader::IR::Value& raw) {
    if (const auto mask = NativeQmdAtomicLiteralAndMask(raw))
        return mask;
    return NativeQmdAtomicLiteralExtractMax(raw);
}

// These are the EXACT IR offset patterns whose maximum can be proven
// without a dynamic SPIR-V bounds predicate:
//   x & constant_mask or BitFieldUExtract(x,literal_offset,literal_count)
//   either expression << constant_shift
// Both may be followed by ONE aligned constant IAdd32 at the call site.
// The shift must be a real U32 literal in [0,31], and all set bits must
// survive the 32-bit shift; a truncated mask would not be this proof.
[[nodiscard]] inline std::optional<u32> NativeQmdAtomicAlignedMask(
    const Shader::IR::Value& raw, u32 width) {
    const auto* instruction = raw.Resolve().TryInstRecursive();
    if (!instruction) return std::nullopt;
    u32 maximum{};
    if (instruction->GetOpcode() == Shader::IR::Opcode::BitwiseAnd32 ||
        instruction->GetOpcode() == Shader::IR::Opcode::BitFieldUExtract) {
        const auto bound = NativeQmdAtomicIndexMaximum(raw);
        if (!bound) return std::nullopt;
        maximum = *bound;
    } else if (instruction->GetOpcode() ==
               Shader::IR::Opcode::ShiftLeftLogical32) {
        const auto shift_value = instruction->Arg(1).Resolve();
        if (!shift_value.IsImmediate() ||
            shift_value.Type() != Shader::IR::Type::U32)
            return std::nullopt;
        const u32 shift = shift_value.U32();
        if (shift >= 32) return std::nullopt;
        const auto bound = NativeQmdAtomicIndexMaximum(
            instruction->Arg(0));
        if (!bound || *bound > (std::numeric_limits<u32>::max() >> shift))
            return std::nullopt;
        maximum = *bound << shift;
    } else {
        return std::nullopt;
    }
    // Every result uses only bit positions enabled by this mask. Its low
    // alignment bits are therefore zero for every possible register value.
    if (maximum & (width - 1U)) return std::nullopt;
    return maximum;
}

[[nodiscard]] inline std::optional<u32> NativeQmdAtomicMaximumByteOffset(
    const Shader::IR::Value& raw, u32 width) {
    if (width != 4 && width != 8) return std::nullopt;
    const auto value = raw.Resolve();
    if (value.IsImmediate()) {
        if (value.Type() != Shader::IR::Type::U32) return std::nullopt;
        return value.U32(); // checked for alignment and overflow by collector
    }
    u32 maximum{};
    if (const auto masked = NativeQmdAtomicAlignedMask(value, width)) {
        maximum = *masked;
    } else {
        const auto* const addition = value.TryInstRecursive();
        if (!addition || addition->GetOpcode() != Shader::IR::Opcode::IAdd32)
            return std::nullopt;
        const auto lhs = addition->Arg(0).Resolve();
        const auto rhs = addition->Arg(1).Resolve();
        // A single aligned constant translation after a bounded AND.
        // Arithmetic before/after this exact final addition is unproven.
        if (lhs.IsImmediate() == rhs.IsImmediate()) return std::nullopt;
        const auto constant = lhs.IsImmediate() ? lhs : rhs;
        if (constant.Type() != Shader::IR::Type::U32) return std::nullopt;
        const u32 base = constant.U32();
        if (base & (width - 1U)) return std::nullopt;
        const auto and_mask = NativeQmdAtomicAlignedMask(
            lhs.IsImmediate() ? rhs : lhs, width);
        if (!and_mask ||
            base > std::numeric_limits<u32>::max() - width ||
            *and_mask > std::numeric_limits<u32>::max() - width - base)
            return std::nullopt;
        maximum = base + *and_mask;
    }
    // The maximum *inclusive* byte offset plus full atomic width must
    // be representable without 32-bit wrap, before dispatch-time bounds.
    if (maximum > std::numeric_limits<u32>::max() - width)
        return std::nullopt;
    return maximum;
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
            // No general runtime-sized guard exists: only fixed offsets,
            // aligned AND/UExtract/shift bounds or non-wrapping base+bound
            // are admissible.
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
// proof above; the same live-size check therefore covers every valid
// runtime offset generated by AND, UExtract, shift or aligned base+bound.
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
        constexpr u64 gpu_limit = u64{1} << 49;
        // An aligned byte offset is insufficient if the SSBO's actual
        // GMMU base itself is misaligned. Check the full native address
        // and extent before a Vulkan atomic can be submitted.
        if (!storage.known_size || !storage.is_written ||
            !storage.gpu_address ||
            (storage.gpu_address & (span.width - 1U)) ||
            storage.gpu_address >= gpu_limit ||
            storage.bytes > gpu_limit - storage.gpu_address ||
            storage.bytes < span.width ||
            span.offset > storage.bytes - span.width)
            return false;
    }
    return true;
}
} // namespace Shader::Ampere
