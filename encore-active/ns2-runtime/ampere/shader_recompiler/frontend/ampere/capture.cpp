// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/capture.h"
#include <array>
#include <limits>
#include "shader_recompiler/frontend/ampere/program_capture.h"
#include "shader_recompiler/frontend/ampere/translation_cache.h"
#include "shader_recompiler/stage.h"

namespace Shader::Ampere {
namespace {
constexpr u64 NativeHeaderBytes = 0x80;
constexpr std::size_t MaxwellHeaderBytes = 0x50;
constexpr u32 MaxCapturedInstructions = 4096;
} // namespace

NativeCaptureResult CaptureNativeGraphicsShader(
    u64 gpu_base, u32 stage, GuestShaderRead read, void* context,
    NativeTranslationCache* cache) {
    NativeCaptureResult result;
    // Shader::Stage is the maintained compiler's stage domain, not a
    // native GPU enum or an unbounded cache discriminator. Compute needs
    // QMD metadata and must never pass through the graphics SPH path.
    if (stage > static_cast<u32>(Shader::Stage::VertexA) ||
        stage == static_cast<u32>(Shader::Stage::Compute))
        return result;
    if (!read || gpu_base > std::numeric_limits<u64>::max() -
                                 NativeHeaderBytes -
                                 u64{MaxCapturedInstructions} * sizeof(Instruction))
        return result;

    std::array<u8, NativeHeaderBytes> header{};
    if (!read(context, gpu_base, header.data(), header.size())) {
        result.status = NativeCaptureStatus::Unmapped;
        return result;
    }

    // No speculative read of entire sparse guest shader allocations. Each
    // 16-byte instruction is independently mapping-checked by the reader.
    std::vector<u8> source;
    source.reserve(MaxCapturedInstructions * sizeof(Instruction));
    ProgramCaptureState capture;
    for (u32 index = 0; index < MaxCapturedInstructions; ++index) {
        std::array<u8, sizeof(Instruction)> bytes{};
        const u64 addr = gpu_base + NativeHeaderBytes + u64{index} * sizeof(Instruction);
        if (!read(context, addr, bytes.data(), bytes.size())) {
            result.status = NativeCaptureStatus::Unmapped;
            result.first_error_offset = NativeHeaderBytes + index * sizeof(Instruction);
            return result;
        }
        Instruction inst{};
        for (u32 w = 0; w < 4; ++w) {
            for (u32 b = 0; b < 4; ++b)
                inst.words[w] |= u32{bytes[w * 4 + b]} << (8 * b);
        }
        // BRX uses a constant-buffer jump table that the standalone
        // adapter cannot resolve. Never accept a truncated linear capture
        // as a valid shader with arbitrary unseen branch destinations.
        if (inst.Bits(0, 12) == 0x949) {
            result.status = NativeCaptureStatus::UnresolvedIndirectBranch;
            result.first_error_offset = NativeHeaderBytes + index * sizeof(Instruction);
            return result;
        }
        const auto observed = capture.Observe(inst, index);
        if (observed == ProgramCaptureState::Result::InvalidTarget) {
            result.status = NativeCaptureStatus::InvalidBranch;
            result.first_error_offset = NativeHeaderBytes + index * sizeof(Instruction);
            return result;
        }
        source.insert(source.end(), bytes.begin(), bytes.end());
        if (observed != ProgramCaptureState::Result::Complete) continue;

        result.captured_native_bytes = static_cast<u32>(source.size());
        const auto maxwell_header =
            std::span<const u8>{header.data(), MaxwellHeaderBytes};
        result.image = cache
            ? cache->GetOrTranslate(maxwell_header, source, stage)
            : BuildMaxwellShaderImage(maxwell_header, source, stage);
        if (!result.image.Ready()) {
            result.status = NativeCaptureStatus::TranslationFailed;
            result.first_error_offset = NativeHeaderBytes +
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
        result.status = NativeCaptureStatus::Ready;
        return result;
    }
    result.status = NativeCaptureStatus::NoTerminator;
    result.captured_native_bytes = static_cast<u32>(source.size());
    return result;
}
} // namespace Shader::Ampere
