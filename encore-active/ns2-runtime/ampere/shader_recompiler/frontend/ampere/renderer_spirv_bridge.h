// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Explicit, opt-in PS5 renderer-side compilation. This does NOT select
// a graphics pipeline, install a Vulkan module, or affect Switch 1 shaders.
#include <vector>
#include <optional>
#include "common/common_types.h"
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/shader_info.h"

namespace Shader {
struct HostTranslateInfo;
struct Profile;
struct RuntimeInfo;
}
namespace Shader::Ampere {
class NativeMaxwellEnvironment;

// A complete, owned SPIR-V module only; never retain the guest memory,
// CFG, IR pools or borrowed runtime-info beyond a single compiler call.
enum class NativeSpirvStatus : u8 {
    Ready,
    UnsupportedStage,
    InvalidImage,
    InactiveSession,
    CompilationFailed,
    InvalidModule,
};
struct NativeSpirvResult {
    NativeSpirvStatus status{NativeSpirvStatus::InvalidImage};
    std::vector<u32> words{};
    // GraphicsPipeline needs resource descriptors and the cumulative binding
    // index after each emitted shader; SPIR-V alone is insufficient.
    std::optional<Shader::Info> info{};
    Shader::Backend::Bindings next_bindings{};
    [[nodiscard]] bool PipelineMetadataReady() const noexcept {
        return Ready() && info.has_value();
    }
    [[nodiscard]] bool Ready() const noexcept {
        return status == NativeSpirvStatus::Ready && !words.empty();
    }
};

// Call only from an explicitly identified NS2 graphics session after the
// NS2 GPU mapping and shader SPH have been captured and verified.
// Only a fully specified standalone VertexB or Fragment stage can be compiled.
// The actual caller must provide its real pipeline RuntimeInfo, profile and
// host metadata; compiling does not publish a VkShaderModule or VkPipeline.
// VertexA requires merge with VertexB; tessellation/geometry require stage
// interfaces; compute has a separate QMD path. All remain fail-closed.
[[nodiscard]] NativeSpirvResult CompileReviewedNativeGraphicsSpirv(
    NativeMaxwellEnvironment& env,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    bool exits_to_dispatcher = false,
    Shader::Backend::Bindings initial_bindings = {});

// Compile a genuine two-program vertex pipeline. The caller must supply
// both owned, stage-matched environments and the real runtime pipeline
// metadata. Unmerged VertexA may never escape as an independent module.
[[nodiscard]] NativeSpirvResult CompileReviewedNativeMergedVertexSpirv(
    NativeMaxwellEnvironment& vertex_a,
    NativeMaxwellEnvironment& vertex_b,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Backend::Bindings initial_bindings = {});

// Keep the original vertex-only API strict for existing research callers.
[[nodiscard]] NativeSpirvResult CompileReviewedNativeVertexSpirv(
    NativeMaxwellEnvironment& env,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    bool exits_to_dispatcher = false);
} // namespace Shader::Ampere
