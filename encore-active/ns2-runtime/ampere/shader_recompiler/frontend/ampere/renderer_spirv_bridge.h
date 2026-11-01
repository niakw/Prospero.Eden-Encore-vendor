// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Explicit, opt-in PS5 renderer-side compilation. This does NOT select
// a graphics pipeline, install a Vulkan module, or affect Switch 1 shaders.
#include <vector>
#include "common/common_types.h"

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
    [[nodiscard]] bool Ready() const noexcept {
        return status == NativeSpirvStatus::Ready && !words.empty();
    }
};

// Call only from an explicitly identified NS2 graphics session after the
// NS2 GPU mapping and shader SPH have been captured and verified.
// VertexA must be merged with VertexB by the actual Vulkan pipeline owner
// before SPIR-V emission; compute is a separate QMD path and is rejected.
[[nodiscard]] NativeSpirvResult CompileReviewedNativeVertexSpirv(
    NativeMaxwellEnvironment& env,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    bool exits_to_dispatcher = false);
} // namespace Shader::Ampere
