// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_core/native_shader_capture.h"
#include <limits>
#include "shader_recompiler/frontend/ampere/maxwell_environment.h"
#include "video_core/memory_manager.h"

namespace VideoCommon {
bool ReadNs2GpuShader(void* context, u64 address, void* dest, std::size_t size) {
    auto* const memory = static_cast<Tegra::MemoryManager*>(context);
    if (!memory || !dest || !size ||
        address > std::numeric_limits<u64>::max() - size ||
        !memory->IsFullyMappedRange(address, size))
        return false;
    memory->ReadBlock(address, dest, size);
    return true;
}

Shader::Ampere::NativeCaptureResult CaptureNs2GraphicsShaderFromGpu(
    Tegra::MemoryManager& memory, u64 gpu_program_address, u32 stage,
    Shader::Ampere::NativeTranslationCache* cache) {
    // The GMMU mapping check is repeated for EVERY individual native
    // instruction. A fault aborts capture without populating a shader image.
    return Shader::Ampere::CaptureNativeGraphicsShader(
        gpu_program_address, stage, ReadNs2GpuShader, &memory, cache);
}

std::unique_ptr<Shader::Environment> CreateNs2GraphicsShaderEnvironment(
    Tegra::MemoryManager& memory, std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address, u32 stage,
    Shader::Ampere::NativeTranslationCache* cache) {
    // Creating the environment requires BOTH a fully verified translation
    // and a caller-provided graphics context. Never publish invalid partials.
    // Reject stage mismatches before even touching guest GMMU mappings:
    // expensive capture cannot make a graphics context from another stage.
    if (!original || stage != static_cast<u32>(original->ShaderStage()))
        return {};
    auto captured = CaptureNs2GraphicsShaderFromGpu(
        memory, gpu_program_address, stage, cache);
    if (!captured.Ready()) return {};
    return Shader::Ampere::NativeMaxwellEnvironment::CreateOwned(
        std::move(original), std::move(captured.image));
}
} // namespace VideoCommon
