// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_core/native_shader_capture.h"
#include <limits>
#include "shader_recompiler/frontend/ampere/maxwell_environment.h"
#include "video_core/memory_manager.h"
#ifdef EDEN_NS2_VULKAN_SHADER_BRIDGE_EXPERIMENTAL
#include "shader_recompiler/frontend/ampere/translation_cache.h"
#include "core/hle/service/nvdrv/devices/native_gpu_channel_registry.h"
#endif

#ifdef EDEN_NS2_VULKAN_SHADER_BRIDGE_EXPERIMENTAL
namespace Service::Nvidia::Devices {
std::shared_ptr<Shader::Ampere::NativeTranslationCache>
NativeGpuChannelRegistry::AcquireGraphicsTranslationCache(
    const void* graphics_engine, const Ticket& ticket) {
    if (!graphics_engine || !ticket.process || !ticket.generation ||
        !ticket.channel_generation) return {};
    const std::scoped_lock lock{mutex};
    const auto it = engines.find(graphics_engine);
    if (it == engines.end() || !it->second.subchannels ||
        it->second.ticket.process != ticket.process ||
        it->second.ticket.generation != ticket.generation ||
        it->second.ticket.channel_generation != ticket.channel_generation)
        return {};
    const auto channel = channels.find(it->second.channel);
    if (channel == channels.end() ||
        channel->second.process != ticket.process ||
        channel->second.generation != ticket.generation ||
        channel->second.channel_generation != ticket.channel_generation ||
        !NativeGpuProcessRegistry::Instance().IsCurrent(
            {ticket.process, ticket.generation}))
        return {};
    // 4 MiB / 32 images per active graphics engine; full native code bytes
    // and the stage are checked by NativeTranslationCache before any hit.
    // Unbind or subchannel retirement drops this shared cache. In-flight
    // readers may finish but the renderer rechecks the engine epoch.
    if (!it->second.translation_cache)
        it->second.translation_cache =
            std::make_shared<Shader::Ampere::NativeTranslationCache>(
                4 * 1024 * 1024, 32);
    return it->second.translation_cache;
}
} // namespace Service::Nvidia::Devices
#endif

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
