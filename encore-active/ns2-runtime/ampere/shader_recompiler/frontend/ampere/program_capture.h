// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <vector>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/maxwell_image.h"

namespace Shader::Ampere {
class ProgramCaptureState {
public:
    enum class Result { Continue, Complete, InvalidTarget };
    static constexpr u32 MaximumInstructions = 65536;

    Result Observe(const Instruction& instruction, u32 index) {
        if (index < exit_prefix.size()) {
            exit_prefix[index] = instruction;
            if (index + 1 == exit_prefix.size()) {
                if (const auto target = StaticAtExitTarget(exit_prefix))
                    last_forward_target = std::max(last_forward_target, *target);
            }
        }
        const u32 opcode = instruction.Bits(0, 12);
        if (opcode == 0x944 || opcode == 0x947 || opcode == 0x945) {
            const u64 raw = instruction.words[1] | (u64{instruction.words[2] & 0x3ffff} << 32);
            // BRA.DIV (bit 91 and a uniform register) carries two mode bits below its word offset.
            const bool divergence = opcode == 0x947 && (instruction.words[2] & ~0x3ffffU) == 0x0b800000U &&
                                    (instruction.words[1] & 3) == 3;
            const s64 relative = opcode == 0x945 ? static_cast<s32>(instruction.words[1])
                                                : (static_cast<s64>(raw << 14) >> 14) - (divergence ? 3 : 0);
            const s64 target = s64{index} * 16 + 16 + relative;
            if (target < 0 || target % 16 || target >= MaximumInstructions * 16)
                return Result::InvalidTarget;
            if (opcode == 0x944 && instruction.Bits(12, 4) == 7) {
                last_call_target = std::max(last_call_target, static_cast<u32>(target / 16));
            } else {
                last_forward_target = std::max(last_forward_target, static_cast<u32>(target / 16));
            }
        }
        if (opcode == 0x94d && instruction.Bits(12, 4) == 7 && instruction.Bits(87, 4) == 7 &&
            !instruction.Bits(84, 2) && index >= last_forward_target) {
            if (!last_call_target) return Result::Complete;
            main_exited = true;
        }
        if (opcode == 0x950 && instruction.Bits(12, 4) == 7 && main_exited &&
            index >= last_call_target && index >= last_forward_target)
            return Result::Complete;
        return Result::Continue;
    }

private:
    std::array<Instruction, 5> exit_prefix{};
    u32 last_call_target{};
    u32 last_forward_target{};
    bool main_exited{};
};

// A BRX selects among its blocks through a constant-buffer table, so the furthest block can lie past the first
// EXIT that ends the linear capture. The capture is extended through it and the per-BRX target lists are
// reported to the environment. The tables are neither validated nor rejected (entries that are not source
// addresses are ignored) and the translators still decide what is acceptable.
struct CaptureIndirectBranch {
    u32 index;       // instruction index of the BRX
    u32 cbuf_index;  // constant bank holding the table
    u32 cbuf_offset; // byte offset of the first entry
    u32 entries;
};

struct CaptureIndirectTable {
    u32 branch{};
    u32 cbuf_index{};
    u32 cbuf_offset{};
    std::vector<u32> values;  // raw table words
    std::vector<u32> targets; // sorted distinct instruction indices inside the capture
};

struct CaptureIndirectResult {
    u32 base{};
    bool extended{};
    std::vector<CaptureIndirectTable> tables;
};

inline s64 CaptureRelativeOffset48(const Instruction& instruction) {
    const u64 raw = u64{instruction.words[1] & ~3U} | (u64{instruction.words[2] & 0x3ffffU} << 32);
    return static_cast<s64>(raw << 14) >> 14;
}

// Recognises, for every self-relative BRX, the selector sequence of the jump tables: a value clamped to
// [0, N-1] (IMNMX), scaled by 4 (SHF), loaded from a constant bank (LDC) and branched on. A BRX that does not
// match is simply not tracked.
inline std::vector<CaptureIndirectBranch> TrackCaptureIndirectBranches(std::span<const Instruction> code) {
    std::vector<CaptureIndirectBranch> result;
    for (u32 branch_index = 0; branch_index < code.size(); ++branch_index) {
        const auto& branch = code[branch_index];
        if (branch.Bits(0, 12) != 0x949)
            continue;
        if (branch.Bits(12, 4) != 7 || (branch.words[2] & ~0x3ffffU) != 0x03800000U ||
            s64{branch_index} * sizeof(Instruction) + sizeof(Instruction) + CaptureRelativeOffset48(branch) != 0)
            continue;
        const u32 branch_source = branch.Bits(24, 8);
        std::optional<u32> load_index;
        for (u32 index = branch_index; index-- > 0;) {
            const auto& instruction = code[index];
            if (instruction.Bits(0, 12) == 0xb82 && instruction.Bits(12, 4) == 7 &&
                instruction.Bits(16, 8) == branch_source && instruction.words[2] == 0x00000800 &&
                !(instruction.words[1] & ~((0x1fU << 22) | (0xffffU << 6))) && !((instruction.words[1] >> 6) & 3)) {
                load_index = index;
                break;
            }
        }
        if (!load_index)
            continue;
        const auto& load = code[*load_index];
        const u32 selector = load.Bits(24, 8);
        std::optional<u32> shift_index;
        for (u32 index = *load_index; index-- > 0;) {
            const auto& instruction = code[index];
            if (instruction.Bits(0, 12) == 0x819 && instruction.Bits(12, 4) == 7 &&
                instruction.Bits(16, 8) == selector && instruction.words[1] == 2 &&
                instruction.words[2] == 0x000006ff) {
                shift_index = index;
                break;
            }
        }
        if (!shift_index)
            continue;
        const u32 bounded_selector = code[*shift_index].Bits(24, 8);
        std::optional<u32> maximum;
        for (u32 index = *shift_index; index-- > 0;) {
            const auto& instruction = code[index];
            if (instruction.Bits(0, 12) == 0x817 && instruction.Bits(12, 4) == 7 &&
                instruction.Bits(16, 8) == bounded_selector && instruction.words[1] >= 1 &&
                instruction.words[1] <= 15 && instruction.words[2] == 0x03800000) {
                maximum = instruction.words[1];
                break;
            }
        }
        if (!maximum)
            continue;
        // CBUF tables cannot wrap the 64-KiB shader constant-buffer
        // address space. The last entry must be entirely mapped, with
        // U32 alignment; rejecting the motif here prevents a future GPU
        // reader from probing an unrelated bank or a wrapped offset.
        const u32 bank = (load.words[1] >> 22) & 0x1f;
        const u32 offset = (load.words[1] >> 6) & 0xffff;
        const u32 entries = *maximum + 1;
        if (bank >= 18 || (offset & 3U) ||
            offset > 0x10000U - entries * sizeof(u32))
            continue;
        result.push_back({.index = branch_index,
                          .cbuf_index = bank,
                          .cbuf_offset = offset,
                          .entries = entries});
    }
    return result;
}

// Reads the table of every tracked BRX in `native`, extends `native` (already captured through its first
// terminating EXIT) through the furthest in-range target and returns the per-BRX target indices.
// Table values are offsets from the program base: they include the `header_size` bytes before instruction 0.
// A non-zero entry below `header_size` can only be an offset from instruction 0, which selects that convention
// instead. When there is no table, every target is already inside the capture, or the tail cannot be read to a
// terminating EXIT, `native` and `capture` are left untouched, so shaders that capture correctly today are
// not affected.
template <typename ReadInstruction, typename ReadCbuf>
CaptureIndirectResult ResolveCaptureIndirectBranches(std::vector<Instruction>& native, ProgramCaptureState& capture,
                                                     u32 header_size, ReadInstruction&& read_instruction,
                                                     ReadCbuf&& read_cbuf) {
    const auto read_tables = [&] {
        std::vector<CaptureIndirectTable> tables;
        for (const auto& branch : TrackCaptureIndirectBranches(native)) {
            auto& table = tables.emplace_back();
            table.branch = branch.index;
            table.cbuf_index = branch.cbuf_index;
            table.cbuf_offset = branch.cbuf_offset;
            for (u32 entry = 0; entry < branch.entries; ++entry)
                table.values.push_back(read_cbuf(branch.cbuf_index, branch.cbuf_offset + entry * 4));
        }
        return tables;
    };
    CaptureIndirectResult result;
    result.tables = read_tables();
    if (result.tables.empty())
        return result;
    result.base = header_size;
    for (const auto& table : result.tables)
        for (const u32 value : table.values)
            if (value && value < header_size)
                result.base = 0;
    const auto entry_index = [&](u32 value) -> std::optional<u32> {
        if (!value || value < result.base || (value - result.base) % sizeof(Instruction))
            return {};
        const u32 index = (value - result.base) / sizeof(Instruction);
        if (index >= ProgramCaptureState::MaximumInstructions)
            return {};
        return index;
    };
    u32 furthest = 0;
    for (const auto& table : result.tables)
        for (const u32 value : table.values)
            if (const auto index = entry_index(value))
                furthest = std::max(furthest, *index);
    if (furthest >= native.size()) {
        const auto saved_native = native;
        const auto saved_capture = capture;
        bool complete = false;
        for (u32 index = static_cast<u32>(native.size()); index < ProgramCaptureState::MaximumInstructions;
             ++index) {
            const std::optional<Instruction> instruction = read_instruction(index);
            if (!instruction)
                break;
            native.push_back(*instruction);
            const auto status = capture.Observe(*instruction, index);
            if (status == ProgramCaptureState::Result::InvalidTarget)
                break;
            if (status == ProgramCaptureState::Result::Complete && native.size() > furthest) {
                complete = true;
                break;
            }
        }
        if (complete) {
            result.extended = true;
            result.tables = read_tables();
        } else {
            native = saved_native;
            capture = saved_capture;
        }
    }
    for (auto& table : result.tables) {
        for (const u32 value : table.values)
            if (const auto index = entry_index(value); index && *index < native.size())
                table.targets.push_back(*index);
        std::ranges::sort(table.targets);
        table.targets.erase(std::unique(table.targets.begin(), table.targets.end()), table.targets.end());
    }
    return result;
}
// Native branch-table words and Maxwell instruction addresses are different
// coordinate systems. In particular, a 16-byte Ampere instruction can lower
// to multiple 8-byte Maxwell words plus scheduling slots. A raw CBUF address
// must never be injected into Maxwell CFG or used for native BRX execution.
enum class CaptureIndirectValueOrigin {
    NativeCode,       // first 128-bit instruction is offset zero
    GraphicsHeader,   // values include the 0x80-byte native graphics SPH
};

struct CaptureTranslatedIndirectEntry {
    u32 raw_value{};
    u32 native_code_offset{};
    u32 maxwell_byte_offset{};
};
struct CaptureTranslatedIndirectTable {
    u32 branch{};
    u32 cbuf_index{};
    u32 cbuf_offset{};
    std::vector<CaptureTranslatedIndirectEntry> entries;
};

// Resolve *every* entry using the actual lowered-program map. Reject a
// mixed-origin, unmapped, misaligned, or beyond-capture target as a whole;
// do not publish a partially remapped branch table. Zero is a valid target
// only for the code-relative convention. This metadata alone cannot enable
// BRX: shader-visible LDC results must also be rewritten or compared against
// the original native values before a translated branch can execute.
[[nodiscard]] inline std::optional<std::vector<CaptureTranslatedIndirectTable>>
TranslateCaptureIndirectTargets(const CaptureIndirectResult& capture,
                                const MaxwellShaderImage& image,
                                CaptureIndirectValueOrigin origin,
                                u32 graphics_header_bytes = 0x80) {
    if (!image.ValidForCompiler() || capture.tables.empty() ||
        (origin == CaptureIndirectValueOrigin::GraphicsHeader &&
         graphics_header_bytes != 0x80))
        return std::nullopt;
    const u32 origin_bytes =
        origin == CaptureIndirectValueOrigin::GraphicsHeader
            ? graphics_header_bytes : 0;
    std::vector<CaptureTranslatedIndirectTable> result;
    result.reserve(capture.tables.size());
    for (const auto& table : capture.tables) {
        const u32 count = static_cast<u32>(table.values.size());
        if (count < 2 || count > 16 ||
            table.branch >= image.consumed_native_bytes / sizeof(Instruction) ||
            table.cbuf_index >= 18 || (table.cbuf_offset & 3U) ||
            table.cbuf_offset > 0x10000U - count * sizeof(u32) ||
            std::any_of(result.begin(), result.end(),
                        [&](const auto& prior) { return prior.branch == table.branch; }))
            return std::nullopt;
        auto& mapped = result.emplace_back();
        mapped.branch = table.branch;
        mapped.cbuf_index = table.cbuf_index;
        mapped.cbuf_offset = table.cbuf_offset;
        for (const u32 value : table.values) {
            if (value < origin_bytes || (value - origin_bytes) % sizeof(Instruction))
                return std::nullopt;
            const u32 native_offset = value - origin_bytes;
            if (native_offset >= image.consumed_native_bytes)
                return std::nullopt;
            const auto maxwell = image.TranslateNativeAddress(native_offset);
            if (!maxwell)
                return std::nullopt;
            mapped.entries.push_back({value, native_offset, *maxwell});
        }
    }
    return result;
}

} // namespace Shader::Ampere
