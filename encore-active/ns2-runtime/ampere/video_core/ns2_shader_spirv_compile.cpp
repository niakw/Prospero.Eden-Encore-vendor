// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_core/ns2_shader_spirv_compile.h"
#include <exception>
#include <memory>
#include <utility>
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"
#include "video_core/native_shader_capture.h"
#include "shader_recompiler/frontend/ampere/maxwell_environment.h"
#include "shader_recompiler/frontend/ampere/native_qmd_compute_environment.h"
#include "shader_recompiler/frontend/ampere/renderer_spirv_bridge.h"
#include "shader_recompiler/frontend/ampere/native_graphics_stage_policy.h"

namespace VideoCommon {
Shader::Ampere::NativeSpirvResult CompileCapturedNs2ComputeSpirvFromReader(
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    Shader::Ampere::GuestShaderRead read,
    void* context,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache,
    Shader::Backend::Bindings initial_bindings) {
    using Shader::Ampere::NativeSpirvStatus;
    auto& registry = Service::Nvidia::Devices::NativeGpuProcessRegistry::Instance();
    const auto session = registry.CaptureTicket(process);
    if (!session) return {NativeSpirvStatus::InactiveSession, {}};
    if (!read || !original ||
        original->ShaderStage() != Shader::Stage::Compute)
        return {NativeSpirvStatus::UnsupportedStage, {}};
    try {
        auto captured = Shader::Ampere::CaptureNativeComputeShader(
            gpu_program_address, read, context, cache);
        if (!registry.IsCurrent(*session))
            return {NativeSpirvStatus::InactiveSession, {}};
        if (!captured.Ready())
            return {NativeSpirvStatus::InvalidImage, {}};
        auto env = Shader::Ampere::NativeMaxwellEnvironment::CreateComputeOwned(
            std::move(original), std::move(captured.image));
        if (!env)
            return {NativeSpirvStatus::InvalidImage, {}};
        auto compiled = Shader::Ampere::CompileReviewedNativeComputeSpirv(
            *env, host_info, profile, runtime_info, initial_bindings);
        if (!registry.IsCurrent(*session))
            return {NativeSpirvStatus::InactiveSession, {}};
        return compiled;
    } catch (const std::exception&) {
        return {NativeSpirvStatus::CompilationFailed, {}};
    }
}

Shader::Ampere::NativeSpirvResult CompileCapturedNs2ComputeSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache,
    Shader::Backend::Bindings initial_bindings) {
    return CompileCapturedNs2ComputeSpirvFromReader(
        process, std::move(original), gpu_program_address,
        ReadNs2GpuShader, &memory, host_info, profile, runtime_info,
        cache, initial_bindings);
}

Shader::Ampere::NativeSpirvResult CompileCapturedNs2QmdComputeSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    const Tegra::NativeComputeLaunch& launch,
    std::unique_ptr<Shader::Environment> texture_metadata,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache,
    Shader::Backend::Bindings initial_bindings) {
    // The new wrapper's constructor does not touch guest memory. Actual
    // GMMU reads remain inside the existing authenticated process-ticket
    // compiler entry point, which rejects stale and foreign sessions.
    try {
        auto original = std::make_unique<Shader::Ampere::NativeQmdComputeEnvironment>(
            launch, ReadNs2GpuShader, &memory, std::move(texture_metadata));
        return CompileCapturedNs2ComputeSpirv(
            memory, process, std::move(original), launch.program_address,
            host_info, profile, runtime_info, cache, initial_bindings);
    } catch (const std::exception&) {
        return {Shader::Ampere::NativeSpirvStatus::InvalidImage, {}};
    }
}

Shader::Ampere::NativeSpirvResult CompileCapturedNs2GraphicsSpirvFromReader(
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    Shader::Ampere::GuestShaderRead read,
    void* context,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache,
    bool exits_to_dispatcher,
    Shader::Backend::Bindings initial_bindings) {
    using Shader::Ampere::NativeSpirvStatus;
    auto& registry = Service::Nvidia::Devices::NativeGpuProcessRegistry::Instance();
    const auto session = registry.CaptureTicket(process);
    if (!session) return {NativeSpirvStatus::InactiveSession, {}};
    // Neither the old VertexB entry nor the new stage-aware entry can
    // autodetect games or borrow any Switch 1 shader bytes/cache keys.
    if (!read || !original)
        return {NativeSpirvStatus::InvalidImage, {}};
    if (!Shader::Ampere::IsStandaloneNativeGraphicsStage(original->ShaderStage()))
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
        auto compiled = Shader::Ampere::CompileReviewedNativeGraphicsSpirv(
            *env, host_info, profile, runtime_info, exits_to_dispatcher,
            initial_bindings);
        // A teardown/reopen or owner re-registration during compilation must
        // never publish a module, even when the process address was reused.
        if (!registry.IsCurrent(*session))
            return {NativeSpirvStatus::InactiveSession, {}};
        return compiled;
    } catch (const std::exception&) {
        return {NativeSpirvStatus::CompilationFailed, {}};
    }
}

Shader::Ampere::NativeSpirvResult CompileCapturedNs2MergedVertexSpirvFromReader(
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
    Shader::Ampere::NativeTranslationCache* cache,
    Shader::Backend::Bindings initial_bindings) {
    using Shader::Ampere::NativeSpirvStatus;
    auto& registry=Service::Nvidia::Devices::NativeGpuProcessRegistry::Instance();
    const auto session=registry.CaptureTicket(process);
    if (!session) return {NativeSpirvStatus::InactiveSession, {}};
    if (!read || !original_a || !original_b)
        return {NativeSpirvStatus::InvalidImage, {}};
    if (original_a->ShaderStage()!=Shader::Stage::VertexA ||
        original_b->ShaderStage()!=Shader::Stage::VertexB ||
        gpu_address_a==gpu_address_b)
        return {NativeSpirvStatus::UnsupportedStage, {}};
    try {
        const auto captured_a=Shader::Ampere::CaptureNativeGraphicsShader(
            gpu_address_a,static_cast<u32>(Shader::Stage::VertexA),
            read,context,cache);
        if (!registry.IsCurrent(*session))
            return {NativeSpirvStatus::InactiveSession, {}};
        if (!captured_a.Ready())
            return {NativeSpirvStatus::InvalidImage, {}};
        const auto captured_b=Shader::Ampere::CaptureNativeGraphicsShader(
            gpu_address_b,static_cast<u32>(Shader::Stage::VertexB),
            read,context,cache);
        if (!registry.IsCurrent(*session))
            return {NativeSpirvStatus::InactiveSession, {}};
        if (!captured_b.Ready())
            return {NativeSpirvStatus::InvalidImage, {}};
        auto env_a=Shader::Ampere::NativeMaxwellEnvironment::CreateOwned(
            std::move(original_a),captured_a.image);
        auto env_b=Shader::Ampere::NativeMaxwellEnvironment::CreateOwned(
            std::move(original_b),captured_b.image);
        if (!env_a || !env_b)
            return {NativeSpirvStatus::InvalidImage, {}};
        auto result=Shader::Ampere::CompileReviewedNativeMergedVertexSpirv(
            *env_a,*env_b,host_info,profile,runtime_info,initial_bindings);
        if (!registry.IsCurrent(*session))
            return {NativeSpirvStatus::InactiveSession, {}};
        return result;
    } catch (const std::exception&) {
        return {NativeSpirvStatus::CompilationFailed, {}};
    }
}

Shader::Ampere::NativeSpirvResult CompileCapturedNs2MergedVertexSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original_a,
    std::unique_ptr<Shader::Environment> original_b,
    u64 gpu_address_a,
    u64 gpu_address_b,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache,
    Shader::Backend::Bindings initial_bindings) {
    return CompileCapturedNs2MergedVertexSpirvFromReader(
        process,std::move(original_a),std::move(original_b),
        gpu_address_a,gpu_address_b,ReadNs2GpuShader,&memory,
        host_info,profile,runtime_info,cache,initial_bindings);
}

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
    if (original && original->ShaderStage() != Shader::Stage::VertexB)
        return {Shader::Ampere::NativeSpirvStatus::UnsupportedStage, {}};
    return CompileCapturedNs2GraphicsSpirvFromReader(
        process, std::move(original), gpu_program_address, read, context,
        host_info, profile, runtime_info, cache, exits_to_dispatcher);
}

Shader::Ampere::NativeSpirvResult CompileCapturedNs2GraphicsSpirv(
    Tegra::MemoryManager& memory,
    const Kernel::KProcess* process,
    std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address,
    const Shader::HostTranslateInfo& host_info,
    const Shader::Profile& profile,
    const Shader::RuntimeInfo& runtime_info,
    Shader::Ampere::NativeTranslationCache* cache,
    bool exits_to_dispatcher,
    Shader::Backend::Bindings initial_bindings) {
    return CompileCapturedNs2GraphicsSpirvFromReader(
        process, std::move(original), gpu_program_address,
        ReadNs2GpuShader, &memory, host_info, profile, runtime_info,
        cache, exits_to_dispatcher, initial_bindings);
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
