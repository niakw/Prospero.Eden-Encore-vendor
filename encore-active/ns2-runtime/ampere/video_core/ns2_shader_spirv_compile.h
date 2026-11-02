// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Explicit session-owned GPU capture -> Vulkan SPIR-V compilation entrypoint.
// Not hooked to normal Switch 1 / Encore PS5 graphics pipeline lookup.
#include <memory>
#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/renderer_spirv_bridge.h"
#include "shader_recompiler/frontend/ampere/capture.h"

namespace Tegra { class MemoryManager; struct NativeComputeLaunch; }
namespace Kernel { class KProcess; }
namespace Shader {
class Environment;
struct HostTranslateInfo;
struct Profile;
struct RuntimeInfo;
}
namespace Shader::Ampere { class NativeTranslationCache; }

namespace VideoCommon {
// Caller must provide the actual guest process associated with the NS2 NVDRV
// session and the exact GMMU object for that process. NULL, unrelated or
// destroyed sessions reject BEFORE any GPU memory access.
// Original shader environment, GPU VA, runtime metadata and device profile
// all come from the same active pipeline. NativeTranslationCache is
// caller-owned and must be invalidated on GMMU remaps/session teardown.
// The new stage-aware opt-in owner can compile standalone VertexB or Fragment,
// but requires the actual owner-supplied pipeline RuntimeInfo for that stage.
// This never selects an existing Switch 1 pipeline or submits Vulkan work.
[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2GraphicsSpirvFromReader(
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    Shader::Ampere::GuestShaderRead read,
    void* context,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    bool exits_to_dispatcher = false,
    Shader::Backend::Bindings initial_bindings = {});

[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2GraphicsSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    bool exits_to_dispatcher = false,
    Shader::Backend::Bindings initial_bindings = {});

// Process-generation-ticket guarded capture of BOTH native vertex stages.
 // Both GPU addresses are read through the identical explicit GMMU reader,
 // and the session ticket is checked after each capture and after compiling.
 // No VertexA-only emission and no Switch 1 Vulkan pipeline interception.
[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2MergedVertexSpirvFromReader(
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original_a,
    std::unique_ptr<Shader::Environment> original_b,
    u64 gpu_address_a,
    u64 gpu_address_b,
    Shader::Ampere::GuestShaderRead read,
    void* context,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    Shader::Backend::Bindings initial_bindings = {});

[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2MergedVertexSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original_a,
    std::unique_ptr<Shader::Environment> original_b,
    u64 gpu_address_a,
    u64 gpu_address_b,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    Shader::Backend::Bindings initial_bindings = {});

// Headerless QMD3 Compute compiler. Caller provides the authenticated
// process, exact GMMU code address, and an owned Compute Environment with
// actual QMD workgroup, buffers and bound texture metadata. No dispatch.
[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2ComputeSpirvFromReader(
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    Shader::Ampere::GuestShaderRead read,
    void* context,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    Shader::Backend::Bindings initial_bindings = {});

[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2ComputeSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    Shader::Backend::Bindings initial_bindings = {});

// Complete QMD3 -> real guest GMMU shader -> stage-correct SPIR-V handoff.
// The caller supplies a live Compute texture/TIC metadata owner, while the
// native QMD environment owns full 49-bit constant-buffer and workgroup state.
// The process ticket is verified before the compiler reads guest memory.
// This returns an owned SPIR-V module and bindings metadata, NOT a Vulkan
// pipeline or successful QMD dispatch.
[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2QmdComputeSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    const Tegra::NativeComputeLaunch& launch,
    std::unique_ptr<Shader::Environment> texture_metadata,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    Shader::Backend::Bindings initial_bindings = {});

// Original vertex-only ABI remains explicitly restricted to VertexB.
// Inject an explicitly bounded shader-memory reader for host qualification.
[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2VertexSpirvFromReader(
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    Shader::Ampere::GuestShaderRead read,
    void* context,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    bool exits_to_dispatcher = false);

[[nodiscard]] Shader::Ampere::NativeSpirvResult CompileCapturedNs2VertexSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    bool exits_to_dispatcher = false);
} // namespace VideoCommon
