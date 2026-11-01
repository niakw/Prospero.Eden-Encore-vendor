// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Research-only, bounded guest GPU memory capture of native Ampere
// graphics shaders. The resulting image is not an executing GPU pipeline.
#include <cstddef>
#include <span>
#include <vector>
#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/maxwell_image.h"

namespace Shader::Ampere {
class NativeTranslationCache;
using GuestShaderRead = bool (*)(void* context, u64 gpu_address, void* buffer, std::size_t size);

enum class NativeCaptureStatus : u8 {
    Ready,
    InvalidInput,
    Unmapped,
    InvalidBranch,
    UnresolvedIndirectBranch,
    NoTerminator,
    TranslationFailed,
};
struct NativeCaptureResult {
    NativeCaptureStatus status{NativeCaptureStatus::InvalidInput};
    u32 captured_native_bytes{};
    u32 first_error_offset{};
    MaxwellShaderImage image{};
    [[nodiscard]] bool Ready() const {
        return status == NativeCaptureStatus::Ready && image.Ready();
    }
};

// Graphics path: the 0x80-byte native shader header precedes the 16-byte
// instructions. Its first 0x50 bytes are the Maxwell-compatible header.
// Compute instead starts at the actual QMD program address, without a
// native graphics header. Workgroup/CBUF state is supplied separately by
// the authenticated QMD owner; capture never executes GPU work.
[[nodiscard]] NativeCaptureResult CaptureNativeGraphicsShader(
    u64 gpu_base, u32 stage, GuestShaderRead read, void* context,
    NativeTranslationCache* cache = nullptr);

// Headerless Ampere compute instructions, starting at the QMD program
// address. The generated 0x50-byte Maxwell prefix is synthetic and zeroed;
// it is never read from the guest GMMU or mistaken for graphics SPH data.
[[nodiscard]] NativeCaptureResult CaptureNativeComputeShader(
    u64 gpu_program_address, GuestShaderRead read, void* context,
    NativeTranslationCache* cache = nullptr);
} // namespace Shader::Ampere
