// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_core/native_shader_capture.h"
#include <exception>
#include <limits>
#include "shader_recompiler/frontend/ampere/maxwell_environment.h"
#include "video_core/memory_manager.h"
#include "video_core/engines/maxwell_3d.h"
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

std::shared_ptr<Shader::Ampere::NativeTranslationCache>
NativeGpuChannelRegistry::AcquireComputeTranslationCache(
    const void* compute_engine, const Ticket& ticket) {
    if (!compute_engine || !ticket.process || !ticket.generation ||
        !ticket.channel_generation) return {};
    const std::scoped_lock lock{mutex};
    const auto owner = compute_engines.find(compute_engine);
    if (owner == compute_engines.end() || !owner->second.subchannels ||
        owner->second.ticket.process != ticket.process ||
        owner->second.ticket.generation != ticket.generation ||
        owner->second.ticket.channel_generation != ticket.channel_generation)
        return {};
    const auto channel = channels.find(owner->second.channel);
    if (channel == channels.end() ||
        channel->second.process != ticket.process ||
        channel->second.generation != ticket.generation ||
        channel->second.channel_generation != ticket.channel_generation ||
        !NativeGpuProcessRegistry::Instance().IsCurrent(
            {ticket.process, ticket.generation}))
        return {};
    // Independent native Compute epoch: never borrow the graphics shader
    // translation cache or let a channel rebind reuse stale shader data.
    // Bounded by both 4 MiB of translated code and 32 exact-byte entries.
    if (!owner->second.translation_cache)
        owner->second.translation_cache =
            std::make_shared<Shader::Ampere::NativeTranslationCache>(
                4 * 1024 * 1024, 32);
    return owner->second.translation_cache;
}
} // namespace Service::Nvidia::Devices
#endif

namespace VideoCommon {
std::optional<u32> ReadNs2GraphicsCbuf(void* context, u32 bank, u32 offset) {
    const auto* owner = static_cast<const Ns2GraphicsCbufContext*>(context);
    if (!owner || !owner->memory || !owner->engine || (offset & 3U) ||
        owner->stage_index >= owner->engine->state.shader_stages.size())
        return {};
    const auto& buffers = owner->engine->state
                              .shader_stages[owner->stage_index].const_buffers;
    if (bank >= buffers.size()) return {};
    const auto& cbuf = buffers[bank];
    if (!cbuf.enabled || !cbuf.address || cbuf.size < sizeof(u32) ||
        offset > cbuf.size - sizeof(u32) ||
        cbuf.address > std::numeric_limits<u64>::max() - offset)
        return {};
    const u64 address = cbuf.address + offset;
    try {
        if (!owner->memory->IsFullyMappedRange(address, sizeof(u32)))
            return {};
        u32 value{};
        owner->memory->ReadBlock(address, &value, sizeof(value));
        return value;
    } catch (const std::exception&) {
        return {};
    }
}

bool ReadNs2GpuShader(void* context, u64 address, void* dest, std::size_t size) {
    auto* const memory = static_cast<Tegra::MemoryManager*>(context);
    if (!memory || !dest || !size ||
        address > std::numeric_limits<u64>::max() - size)
        return false;
    try {
        if (!memory->IsFullyMappedRange(address, size))
            return false;
        // Mappings may disappear between admission and the actual read.
        // A failed/partial copy never publishes a native shader image.
        memory->ReadBlock(address, dest, size);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

Shader::Ampere::NativeCaptureResult CaptureNs2GraphicsShaderFromGpu(
    Tegra::MemoryManager& memory, u64 gpu_program_address, u32 stage,
    Shader::Ampere::NativeTranslationCache* cache,
    const Ns2GraphicsCbufContext* cbuf) {
    // The GMMU mapping check is repeated for EVERY individual native
    // instruction. A fault aborts capture without populating a shader image.
    try {
        if (cbuf) {
            if (cbuf->memory != &memory) return {};
            return Shader::Ampere::CaptureNativeGraphicsShaderChecked(
                gpu_program_address, stage, ReadNs2GpuShader, &memory,
                ReadNs2GraphicsCbuf, const_cast<Ns2GraphicsCbufContext*>(cbuf),
                cache);
        }
        return Shader::Ampere::CaptureNativeGraphicsShader(
            gpu_program_address, stage, ReadNs2GpuShader, &memory, cache);
    } catch (const std::exception&) {
        // A failure before shader publication must not unwind through the
        // 3D engine's CurrentGraphicsPipeline() capture phase.
        Shader::Ampere::NativeCaptureResult failed{};
        failed.status = Shader::Ampere::NativeCaptureStatus::TranslationFailed;
        return failed;
    }
}

std::unique_ptr<Shader::Environment> CreateNs2GraphicsShaderEnvironment(
    Tegra::MemoryManager& memory, std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address, u32 stage,
    Shader::Ampere::NativeTranslationCache* cache,
    const Ns2GraphicsCbufContext* cbuf) {
    // Creating the environment requires BOTH a fully verified translation
    // and a caller-provided graphics context. Never publish invalid partials.
    // Reject stage mismatches before even touching guest GMMU mappings:
    // expensive capture cannot make a graphics context from another stage.
    if (!original || stage != static_cast<u32>(original->ShaderStage()))
        return {};
    try {
        auto captured = CaptureNs2GraphicsShaderFromGpu(
            memory, gpu_program_address, stage, cache, cbuf);
        if (!captured.Ready()) return {};
        return Shader::Ampere::NativeMaxwellEnvironment::CreateOwned(
            std::move(original), std::move(captured.image));
    } catch (const std::exception&) {
        // A valid captured instruction stream is not enough if allocating
        // the owned compiler environment fails during a GMMU/session change.
        return {};
    }
}
} // namespace VideoCommon
