// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_core/ns2_shader_spirv_compile.h"
#include <exception>
#include <memory>
#include <utility>
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"
#include "video_core/native_shader_capture.h"
#include "shader_recompiler/frontend/ampere/maxwell_environment.h"
#include "shader_recompiler/frontend/ampere/renderer_spirv_bridge.h"

namespace VideoCommon {
Shader::Ampere::NativeSpirvResult CompileCapturedNs2VertexSpirvFromReader(
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    Shader::Ampere::GuestShaderRead read,
    void* context,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache,
    bool exits_to_dispatcher) {
    using Shader::Ampere::NativeSpirvStatus;
    auto& registry = Service::Nvidia::Devices::NativeGpuProcessRegistry::Instance();
    const auto session = registry.CaptureTicket(process);
    if (!session) return {NativeSpirvStatus::InactiveSession, {}};
    // No autodetection, no fallback to Switch 1 shader bytes or cache keys.
    // Reject BEFORE GMMU capture: unmerged VertexA/compute stages cannot
    // silently appear as valid Vulkan VertexB modules.
    if (!read || !original)
        return {NativeSpirvStatus::InvalidImage, {}};
    if (original->ShaderStage() != Shader::Stage::VertexB)
        return {NativeSpirvStatus::UnsupportedStage, {}};
    try {
        auto captured = Shader::Ampere::CaptureNativeGraphicsShader(
            gpu_program_address, static_cast<u32>(original->ShaderStage()),
            read, context, cache);
        if (!registry.IsCurrent(*session))
            return {NativeSpirvStatus::InactiveSession, {}};
        if (!captured.Ready())
            return {NativeSpirvStatus::InvalidImage, {}};
        auto env = Shader::Ampere::NativeMaxwellEnvironment::CreateOwned(
            std::move(original), std::move(captured.image));
        if (!env)
            return {NativeSpirvStatus::InvalidImage, {}};
        // Only a complete, independently owned module escapes this call.
        // Existing Vulkan pipeline/layout construction is NOT overridden.
        auto compiled = Shader::Ampere::CompileReviewedNativeVertexSpirv(
            *env, host_info, profile, runtime_info, exits_to_dispatcher);
        // A teardown/reopen or owner re-registration during compilation must
        // never publish a module, even when the process address was reused.
        if (!registry.IsCurrent(*session))
            return {NativeSpirvStatus::InactiveSession, {}};
        return compiled;
    } catch (const std::exception&) {
        return {NativeSpirvStatus::CompilationFailed, {}};
    }
}

Shader::Ampere::NativeSpirvResult CompileCapturedNs2VertexSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache,
    bool exits_to_dispatcher) {
    return CompileCapturedNs2VertexSpirvFromReader(
        process, std::move(original), gpu_program_address,
        ReadNs2GpuShader, &memory, host_info, profile, runtime_info,
        cache, exits_to_dispatcher);
}
} // namespace VideoCommon
