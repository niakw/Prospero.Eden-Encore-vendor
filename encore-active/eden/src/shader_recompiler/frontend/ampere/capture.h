// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Research-only, bounded guest GPU memory capture of native Ampere
// graphics shaders. The resulting image is not an executing GPU pipeline.
#include <cstddef>
#include <optional>
#include <span>
#include <vector>
#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/maxwell_image.h"

namespace Shader::Ampere {
class NativeTranslationCache;
// Native GMMU addresses are 49-bit on this GPU, independent of the host
// process pointer width. Check the full range BEFORE any mapped callback;
// size zero is never an admissible guest shader/CBUF memory request.
[[nodiscard]] constexpr bool NativeGpuRange49(u64 address,u64 bytes) noexcept {
    constexpr u64 limit = u64{1} << 49;
    return bytes && address < limit && bytes <= limit - address;
}
using GuestShaderRead = bool (*)(void* context, u64 gpu_address, void* buffer, std::size_t size);
using GuestCbufRead = std::optional<u32> (*)(void* context, u32 bank, u32 byte_offset);

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
    // Immutable original QMD program bytes kept until Vulkan dispatch.
    // Graphics still has its own SPH/CBUF owner; compute has no SPH.
    std::vector<u8> captured_compute_bytes{};
    // Works after image has been moved to the compiler: never use a
    // diagnostic shader hash as proof that guest code remained unchanged.
    [[nodiscard]] bool ComputeSourceMatches(
        u64 code_address, GuestShaderRead read, void* context) const;
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
// BRX requires both mapping-checked native shader reads and verified CBUF
// entries. The original entrypoint remains fail-closed without this callback.
[[nodiscard]] NativeCaptureResult CaptureNativeGraphicsShaderChecked(
    u64 gpu_base, u32 stage, GuestShaderRead read, void* context,
    GuestCbufRead read_cbuf, void* cbuf_context,
    NativeTranslationCache* cache = nullptr);

[[nodiscard]] NativeCaptureResult CaptureNativeComputeShader(
    u64 gpu_program_address, GuestShaderRead read, void* context,
    NativeTranslationCache* cache = nullptr);
} // namespace Shader::Ampere
