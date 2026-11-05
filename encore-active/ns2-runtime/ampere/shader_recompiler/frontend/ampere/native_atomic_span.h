// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// P2.3: carry the complete native Compute atomic word span from real IR
// to the CURRENT QMD3 resource plan, not just a reflected SSBO index.
// Dynamic byte offsets remain closed until a real SPIR-V bounds predicate
// precedes OpAtomic; the shared backend currently has no such guard.
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
    u32 offset{};
    u32 width{};
    friend bool operator==(const NativeAtomicSpan&, const NativeAtomicSpan&) = default;
};

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
            // No emitted runtime-sized bounds guard exists for an indirect
            // offset. Do not publish an atomic with a guessed GPU range.
            if (!binding.IsImmediate() || !offset.IsImmediate())
                return std::nullopt;
            const bool wide = op >= Opcode::StorageAtomicIAdd64 &&
                              op <= Opcode::StorageAtomicExchange64;
            const u32 width = wide ? 8U : 4U;
            const u32 slot = binding.U32();
            const u32 byte_offset = offset.U32();
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
