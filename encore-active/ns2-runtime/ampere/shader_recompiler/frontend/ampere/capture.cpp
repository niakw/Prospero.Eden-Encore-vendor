// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/capture.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>
#include "shader_recompiler/frontend/ampere/program_capture.h"
#include "shader_recompiler/frontend/ampere/translation_cache.h"
#include "shader_recompiler/stage.h"

namespace Shader::Ampere {
namespace {
constexpr u64 NativeHeaderBytes = 0x80;
constexpr std::size_t MaxwellHeaderBytes = 0x50;
constexpr u32 MaxCapturedInstructions = 4096;
constexpr std::size_t NativeShaderReadPage = 4096;
// Do not speculatively read a full guest allocation. A successful bounded
// read ahead may be reused only inside the same 4 KiB GPU page.
static_assert(NativeShaderReadPage % sizeof(Instruction) == 0);
} // namespace

NativeCaptureResult CaptureNativeShader(
    u64 gpu_base, u32 stage, GuestShaderRead read, void* context,
    NativeTranslationCache* cache, bool compute,
    GuestCbufRead read_cbuf, void* cbuf_context) {
    NativeCaptureResult result;
    // The graphics SPH is never borrowed for compute. Stage identities
    // and mapped read boundaries remain distinct across both paths.
    if (compute ? stage != static_cast<u32>(Shader::Stage::Compute)
                : stage > static_cast<u32>(Shader::Stage::VertexA) ||
                  stage == static_cast<u32>(Shader::Stage::Compute))
        return result;
    const u64 header_bytes = compute ? 0 : NativeHeaderBytes;
    // Only the first native word must fit at admission. Later branches and
    // sparse shader pages are validated individually during their actual
    // reads; a short shader near the 49-bit ceiling is still legitimate.
    if (!read || !NativeGpuRange49(
            gpu_base, header_bytes + sizeof(Instruction)))
        return result;

    // Every guest GPU read can fail concurrently with an unmap. Capture
    // must report Unmapped, not propagate exceptions to the GPU worker.
    const auto checked_read = [&](u64 address, void* dest, std::size_t bytes) {
        if (!NativeGpuRange49(address,bytes))
            return false;
        try {
            return read(context, address, dest, bytes);
        } catch (const std::exception&) {
            return false;
        }
    };
    std::array<u8, NativeHeaderBytes> header{};
    if (!compute && !checked_read(gpu_base, header.data(), header.size())) {
        result.status = NativeCaptureStatus::Unmapped;
        return result;
    }

    // No speculative read of entire sparse guest shader allocations. Each
    // 16-byte instruction is independently mapping-checked by the reader.
    std::vector<u8> source;
    source.reserve(MaxCapturedInstructions * sizeof(Instruction));
    ProgramCaptureState capture;
    std::vector<Instruction> native;
    native.reserve(MaxCapturedInstructions);
    bool saw_indirect = false;
    std::array<u8, NativeShaderReadPage> page{};
    u64 page_start{};
    std::size_t page_bytes{};
    bool read_ahead_unavailable = false;
    for (u32 index = 0; index < MaxCapturedInstructions; ++index) {
        std::array<u8, sizeof(Instruction)> bytes{};
        const u64 addr = gpu_base + header_bytes + u64{index} * sizeof(Instruction);
        // Preserve one-instruction reads for short programs. Beyond the first
        // two instructions, cache a successfully mapped span within one GPU
        // page. The same callback still performs the memory coherency and
        // mapping checks; failed prefetch is NEVER a mapping fault.
        bool fetched = false;
        if (page_bytes && addr >= page_start &&
            addr - page_start <= page_bytes &&
            sizeof(Instruction) <= page_bytes - (addr - page_start)) {
            const auto offset = static_cast<std::size_t>(addr - page_start);
            std::memcpy(bytes.data(), page.data() + offset, bytes.size());
            fetched = true;
        }
        if (!fetched && index >= 2 && !read_ahead_unavailable) {
            const auto page_remaining =
                NativeShaderReadPage - static_cast<std::size_t>(
                    addr & (NativeShaderReadPage - 1));
            const auto shader_remaining =
                std::size_t{MaxCapturedInstructions - index} * sizeof(Instruction);
            const auto batch = (std::min)(page_remaining, shader_remaining);
            const auto aligned_batch = batch - batch % sizeof(Instruction);
            if (aligned_batch >= 4 * sizeof(Instruction)) {
                if (checked_read(addr, page.data(), aligned_batch)) {
                    page_start = addr;
                    page_bytes = aligned_batch;
                    std::memcpy(bytes.data(), page.data(), bytes.size());
                    fetched = true;
                } else {
                    // Mapping may end inside this page, or the reader may
                    // reject bulk operations. Avoid repeating failed probes.
                    read_ahead_unavailable = true;
                    page_bytes = 0;
                }
            }
            // A short tail at a page boundary is NOT a failed bulk read:
            // retry from the next page after its 16-byte checked reads.
        }
        if (!fetched && !checked_read(addr, bytes.data(), bytes.size())) {
            result.status = NativeCaptureStatus::Unmapped;
            result.first_error_offset = header_bytes + index * sizeof(Instruction);
            return result;
        }
        Instruction inst{};
        for (u32 w = 0; w < 4; ++w) {
            for (u32 b = 0; b < 4; ++b)
                inst.words[w] |= u32{bytes[w * 4 + b]} << (8 * b);
        }
        // BRX and BRXU both use computed native-PC targets. The Maxwell
        // CFG reads their guest CBUF table as Maxwell addresses, which is
        // invalid without a complete native-PC -> Maxwell-PC remap AND a
        // matching shader-visible index transform. Do not publish a partial
        // linear shader or silently accept uniform BRXU either.
        const u32 opcode = inst.Bits(0, 12);
        if (opcode == 0x958 || (opcode == 0x949 && !read_cbuf)) {
            result.status = NativeCaptureStatus::UnresolvedIndirectBranch;
            result.first_error_offset = header_bytes + index * sizeof(Instruction);
            return result;
        }
        saw_indirect |= opcode == 0x949;
        const auto observed = capture.Observe(inst, index);
        if (observed == ProgramCaptureState::Result::InvalidTarget) {
            result.status = NativeCaptureStatus::InvalidBranch;
            result.first_error_offset = header_bytes + index * sizeof(Instruction);
            return result;
        }
        native.push_back(inst);
        source.insert(source.end(), bytes.begin(), bytes.end());
        if (observed != ProgramCaptureState::Result::Complete) continue;

        result.captured_native_bytes = static_cast<u32>(source.size());
        const auto maxwell_header =
            std::span<const u8>{header.data(), MaxwellHeaderBytes};
        // Only Geometry has a reviewed LEPC stage owner. Keep the true,
        // GMMU-validated native shader PC separate from Maxwell's 0x50
        // translated address map and from the shared NS1 path.
        const std::optional<u64> native_code_address =
            !compute && stage == static_cast<u32>(Shader::Stage::Geometry)
                ? std::optional<u64>{gpu_base + header_bytes} : std::nullopt;
        try {
            if (saw_indirect) {
                // BRX targets can jump beyond the first EXIT. Recover the
                // exact bytes through the same bounded, mapping-checked GMMU
                // reader, and authenticate every CBUF word twice.
                const auto read_instruction = [&](u32 native_index)
                    -> std::optional<Instruction> {
                    if (native_index >= MaxCapturedInstructions)
                        return {};
                    std::array<u8, sizeof(Instruction)> raw{};
                    const u64 location = gpu_base + header_bytes +
                                         u64{native_index} * sizeof(Instruction);
                    if (!checked_read(location, raw.data(), raw.size()))
                        return {};
                    Instruction decoded{};
                    for (u32 word = 0; word < 4; ++word)
                        for (u32 byte = 0; byte < 4; ++byte)
                            decoded.words[word] |= u32{raw[word*4+byte]} << (byte*8);
                    return decoded;
                };
                const auto checked_cbuf = [&](u32 bank, u32 offset)
                    -> std::optional<u32> {
                    try {
                        return read_cbuf(cbuf_context,bank,offset);
                    } catch (const std::exception&) {
                        return {};
                    }
                };
                // Games may encode CBUF jump targets from the beginning
                // of native code (0) or after the graphics header (0x80).
                // Never infer the convention from just one CBUF word:
                // resolve both independently and accept ONLY one complete,
                // mapping-checked interpretation. Two valid origins are
                // ambiguous and therefore must not publish a pipeline.
                const auto attempt = [&](CaptureIndirectValueOrigin origin) {
                    auto candidate_code = native;
                    auto candidate_state = capture;
                    auto candidate = ResolveCaptureIndirectBranchesChecked(
                        candidate_code, candidate_state, origin,
                        read_instruction, checked_cbuf);
                    return std::pair{std::move(candidate_code),
                                     std::move(candidate)};
                };
                auto [header_code, header_table] = attempt(
                    CaptureIndirectValueOrigin::GraphicsHeader);
                auto [code_code, code_table] = attempt(
                    CaptureIndirectValueOrigin::NativeCode);
                if (bool(header_table) == bool(code_table)) {
                    result.status = NativeCaptureStatus::UnresolvedIndirectBranch;
                    result.first_error_offset =
                        header_bytes + index * sizeof(Instruction);
                    return result;
                }
                const bool with_header = bool(header_table);
                auto resolved = with_header ? std::move(header_table)
                                            : std::move(code_table);
                native = with_header ? std::move(header_code)
                                     : std::move(code_code);
                const auto origin = with_header
                    ? CaptureIndirectValueOrigin::GraphicsHeader
                    : CaptureIndirectValueOrigin::NativeCode;
                const auto plans = MakeNativeIndirectBranchPlans(*resolved,native);
                if (!plans) {
                    result.status = NativeCaptureStatus::UnresolvedIndirectBranch;
                    result.first_error_offset =
                        header_bytes + index * sizeof(Instruction);
                    return result;
                }
                source.clear();
                for (const auto& instruction : native)
                    for (u32 word : instruction.words)
                        for (u32 byte = 0; byte < 4; ++byte)
                            source.push_back(static_cast<u8>(word >> (byte * 8)));
                result.captured_native_bytes = static_cast<u32>(source.size());
                // Do not reuse the shader-only LRU for a CBUF-dependent
                // program. The complete native table contents are part of
                // the image key and are revalidated before publication.
                result.image = BuildMaxwellShaderImage(
                    maxwell_header, source, stage, *plans, native_code_address);
                if (!result.image.Ready() ||
                    !TranslateCaptureIndirectTargets(
                        *resolved, result.image, origin)) {
                    result.status = NativeCaptureStatus::TranslationFailed;
                    result.image = {};
                    return result;
                }
                // A 4-KiB read-ahead cannot prove that the source bytes
                // stayed unchanged while CBUF tables and later shader blocks
                // were read. Compare every native instruction again through
                // the mapped GMMU callback before publishing this image.
                // This applies only to BRX (not the established NS1 path).
                for (u32 native_index = 0; native_index < native.size();
                     ++native_index) {
                    const auto current = read_instruction(native_index);
                    if (!current) {
                        result.first_error_offset = header_bytes +
                            native_index * sizeof(Instruction);
                        result.status = NativeCaptureStatus::Unmapped;
                        result.image = {};
                        return result;
                    }
                    if (current->words != native[native_index].words) {
                        result.first_error_offset = header_bytes +
                            native_index * sizeof(Instruction);
                        result.status = NativeCaptureStatus::TranslationFailed;
                        result.image = {};
                        return result;
                    }
                }
                if (!compute) {
                    std::array<u8, NativeHeaderBytes> live_header{};
                    if (!checked_read(gpu_base, live_header.data(),
                                      live_header.size())) {
                        result.status = NativeCaptureStatus::Unmapped;
                        result.image = {};
                        return result;
                    }
                    if (live_header != header) {
                        result.status = NativeCaptureStatus::TranslationFailed;
                        result.image = {};
                        return result;
                    }
                }
                // Read CBUF last, after the source/header recheck, so a
                // source mutation cannot leave a valid key for stale tables.
                for (const auto& table : resolved->tables)
                    for (u32 entry = 0; entry < table.values.size(); ++entry) {
                        const auto current = checked_cbuf(
                            table.cbuf_index, table.cbuf_offset + entry * 4);
                        if (!current || *current != table.values[entry]) {
                            result.status = NativeCaptureStatus::UnresolvedIndirectBranch;
                            result.image = {};
                            return result;
                        }
                    }
            } else {
                result.image = cache
                    ? cache->GetOrTranslate(maxwell_header, source, stage, native_code_address)
                    : BuildMaxwellShaderImage(maxwell_header, source, stage, {}, native_code_address);
            }
        } catch (const std::exception&) {
            // Failed allocation/cache/lowering is a capture failure, not a
            // partially valid module or an uncaught GPU worker exception.
            result.image = {};
            result.status = NativeCaptureStatus::TranslationFailed;
            result.first_error_offset = header_bytes +
                                        result.captured_native_bytes;
            return result;
        }
        if (!result.image.Ready()) {
            result.status = NativeCaptureStatus::TranslationFailed;
            result.first_error_offset = header_bytes +
                                        result.image.failed_native_offset;
            return result;
        }
        // The capture may extend beyond the actual translation unit;
        // never expose trailing unmapped shader data as a valid result.
        if (result.image.consumed_native_bytes != result.captured_native_bytes) {
            result.status = NativeCaptureStatus::TranslationFailed;
            result.image = {};
            return result;
        }
        if (!saw_indirect) {
            // The first two instructions were read separately, but the
            // remainder may have come from speculative page batches. A guest
            // can rewrite the same mapped VA while the translation cache
            // compiles. Reauthenticate the EXACT original native instruction
            // bytes before exposing any shader key or Vulkan pipeline.
            // BRX has a stricter source+CBUF replay above.
            std::array<u8, NativeShaderReadPage> verify{};
            for (std::size_t offset = 0; offset < source.size();) {
                const u64 address = gpu_base + header_bytes + offset;
                const std::size_t remaining_in_page =
                    NativeShaderReadPage - static_cast<std::size_t>(
                        address & (NativeShaderReadPage - 1));
                std::size_t bytes = (std::min)(
                    remaining_in_page, source.size() - offset);
                bytes -= bytes % sizeof(Instruction);
                // An unaligned guest code start can cross a page in one
                // 16-byte instruction; the checked GMMU callback owns the
                // corresponding full-span proof.
                if (!bytes) bytes = sizeof(Instruction);
                const auto mismatch = [&](std::size_t at, std::size_t length) {
                    return !std::equal(source.begin() + at,
                                       source.begin() + at + length,
                                       verify.begin());
                };
                if (checked_read(address, verify.data(), bytes)) {
                    if (mismatch(offset, bytes)) {
                        result.status = NativeCaptureStatus::TranslationFailed;
                        result.first_error_offset = header_bytes +
                            static_cast<u32>(offset);
                        result.image = {};
                        return result;
                    }
                } else {
                    // A page-batch rejection is not proof of a missing
                    // instruction. Some guest readers only accept 16 bytes.
                    // Recheck each exact word rather than refusing a valid
                    // shader or trusting data not successfully re-read.
                    for (std::size_t part = 0; part < bytes;
                         part += sizeof(Instruction)) {
                        if (!checked_read(address + part, verify.data(),
                                          sizeof(Instruction))) {
                            result.status = NativeCaptureStatus::Unmapped;
                            result.first_error_offset = header_bytes +
                                static_cast<u32>(offset + part);
                            result.image = {};
                            return result;
                        }
                        if (mismatch(offset + part, sizeof(Instruction))) {
                            result.status = NativeCaptureStatus::TranslationFailed;
                            result.first_error_offset = header_bytes +
                                static_cast<u32>(offset + part);
                            result.image = {};
                            return result;
                        }
                    }
                }
                offset += bytes;
            }
            if (!compute) {
                std::array<u8, NativeHeaderBytes> latest_header{};
                if (!checked_read(gpu_base, latest_header.data(),
                                  latest_header.size())) {
                    result.status = NativeCaptureStatus::Unmapped;
                    result.first_error_offset = 0;
                    result.image = {};
                    return result;
                }
                if (latest_header != header) {
                    result.status = NativeCaptureStatus::TranslationFailed;
                    result.first_error_offset = 0;
                    result.image = {};
                    return result;
                }
            }
        }
        result.status = NativeCaptureStatus::Ready;
        // Keep the verified guest Compute bytes across SPIR-V compilation
        // and Vulkan descriptor staging. Move, never duplicate, the bounded
        // (at most 64-KiB) shader capture into its dispatch-local owner.
        if (compute) result.captured_compute_bytes = std::move(source);
        return result;
    }
    result.status = NativeCaptureStatus::NoTerminator;
    result.captured_native_bytes = static_cast<u32>(source.size());
    return result;
}

bool NativeCaptureResult::ComputeSourceMatches(
    u64 code_address, GuestShaderRead read, void* context) const {
    // The image can already have been moved to the real SPIR-V compiler.
    // The immutable source and checked capture status remain owned here.
    if (status != NativeCaptureStatus::Ready || !read ||
        captured_compute_bytes.empty() ||
        captured_compute_bytes.size() != captured_native_bytes ||
        captured_compute_bytes.size() > 4096 * sizeof(Instruction) ||
        !NativeGpuRange49(code_address, captured_compute_bytes.size()))
        return false;
    std::array<u8, NativeShaderReadPage> scratch{};
    const auto safe_read = [&](u64 address, void* dst, std::size_t bytes) {
        if (!NativeGpuRange49(address, bytes)) return false;
        try {
            return read(context, address, dst, bytes);
        } catch (...) {
            return false;
        }
    };
    const auto matches = [&](std::size_t offset, std::size_t size) {
        return std::equal(captured_compute_bytes.begin() + offset,
                          captured_compute_bytes.begin() + offset + size,
                          scratch.begin());
    };
    for (std::size_t offset = 0; offset < captured_compute_bytes.size();) {
        const u64 address = code_address + offset;
        std::size_t bytes = (std::min)(
            NativeShaderReadPage - static_cast<std::size_t>(
                address & (NativeShaderReadPage - 1)),
            captured_compute_bytes.size() - offset);
        bytes -= bytes % sizeof(Instruction);
        if (!bytes) bytes = sizeof(Instruction);
        if (safe_read(address, scratch.data(), bytes)) {
            if (!matches(offset, bytes)) return false;
        } else {
            // Preserve legitimate exact-instruction readers and reject
            // any unmapped/changed word without trusting speculative bulk.
            for (std::size_t part = 0; part < bytes;
                 part += sizeof(Instruction)) {
                if (!safe_read(address + part, scratch.data(),
                               sizeof(Instruction)) ||
                    !matches(offset + part, sizeof(Instruction)))
                    return false;
            }
        }
        offset += bytes;
    }
    return true;
}

NativeCaptureResult CaptureNativeGraphicsShader(
    u64 gpu_base, u32 stage, GuestShaderRead read, void* context,
    NativeTranslationCache* cache) {
    return CaptureNativeShader(gpu_base, stage, read, context, cache, false,
                               nullptr, nullptr);
}

NativeCaptureResult CaptureNativeGraphicsShaderChecked(
    u64 gpu_base, u32 stage, GuestShaderRead read, void* context,
    GuestCbufRead read_cbuf, void* cbuf_context,
    NativeTranslationCache* cache) {
    if (!read_cbuf) return {};
    return CaptureNativeShader(gpu_base, stage, read, context, cache, false,
                               read_cbuf, cbuf_context);
}

NativeCaptureResult CaptureNativeComputeShader(
    u64 gpu_program_address, GuestShaderRead read, void* context,
    NativeTranslationCache* cache) {
    return CaptureNativeShader(gpu_program_address,
        static_cast<u32>(Shader::Stage::Compute), read, context, cache, true,
        nullptr, nullptr);
}
} // namespace Shader::Ampere
