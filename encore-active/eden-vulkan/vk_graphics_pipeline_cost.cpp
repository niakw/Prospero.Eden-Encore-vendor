// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include "common/scope_exit.h"
#include <exception>
#include "common/thread_worker.h"
#include "dev_vulkan.h"
#include "performance.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <fmt/format.h>

#include "video_core/renderer_vulkan/pipeline_helper.h"

#include "common/bit_field.h"
#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/pipeline_statistics.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/shader_notify.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/gpu_logging/gpu_logging.h"
#include "common/settings.h"

#if defined(_MSC_VER) && defined(NDEBUG)
#define LAMBDA_FORCEINLINE [[msvc::forceinline]]
#else
#define LAMBDA_FORCEINLINE
#endif

namespace Vulkan {
#ifdef PS5_NATIVE
// Graphics first-use builds and cold shader translation share this counter.
// Export it from Vulkan, not the anonymous namespace, for native LTO.
extern std::atomic<unsigned> pending_first_use_compute_builds;
std::atomic<unsigned> pending_first_use_graphics_builds{0};
#endif
namespace {
using boost::container::small_vector;
using boost::container::static_vector;
using Shader::ImageBufferDescriptor;
using Shader::Backend::SPIRV::RENDERAREA_LAYOUT_OFFSET;
using Shader::Backend::SPIRV::RESCALING_LAYOUT_DOWN_FACTOR_OFFSET;
using Shader::Backend::SPIRV::RESCALING_LAYOUT_WORDS_OFFSET;
using Tegra::Texture::TexturePair;
using VideoCore::Surface::PixelFormat;
using VideoCore::Surface::PixelFormatFromDepthFormat;
using VideoCore::Surface::PixelFormatFromRenderTargetFormat;

constexpr size_t NUM_STAGES = Maxwell::MaxShaderStage;
// The shared first-use graphics counter is defined at Vulkan namespace scope.

// Descriptor-heavy titles can exceed small_vector's inline capacity on
// thousands of draws per second. Keep the spare heap capacity on the GPU
// owner thread instead of allocating/freeing it while the guest texture and
// buffer caches are locked. Small layouts retain their original stack path.
// Reentrant Configure() calls receive independent scratch storage.
struct GraphicsDescriptorScratch {
    boost::container::small_vector<VideoCommon::ImageViewInOut, 64> views;
    boost::container::small_vector<VideoCommon::SamplerId, 64> samplers;
    bool in_use{};
};

// Two per-thread scratch slots cover ordinary and once-nested Configure.
// A third recursive call retains the original safe heap allocation fallback.
thread_local GraphicsDescriptorScratch graphics_descriptor_scratch;
thread_local GraphicsDescriptorScratch graphics_descriptor_nested_scratch;

class GraphicsDescriptorScratchLease {
public:
    GraphicsDescriptorScratchLease() {
        if (!graphics_descriptor_scratch.in_use) {
            current = &graphics_descriptor_scratch;
            current->in_use = true;
        } else if (!graphics_descriptor_nested_scratch.in_use) {
            current = &graphics_descriptor_nested_scratch;
            current->in_use = true;
        } else {
            nested = std::make_unique<GraphicsDescriptorScratch>();
            current = nested.get();
        }
        current->views.clear();
        current->samplers.clear();
    }

    ~GraphicsDescriptorScratchLease() {
        if (!nested) current->in_use = false;
    }

    GraphicsDescriptorScratchLease(const GraphicsDescriptorScratchLease&) = delete;
    GraphicsDescriptorScratchLease& operator=(const GraphicsDescriptorScratchLease&) = delete;

    auto& Views() noexcept { return current->views; }
    auto& Samplers() noexcept { return current->samplers; }

private:
    std::unique_ptr<GraphicsDescriptorScratch> nested;
    GraphicsDescriptorScratch* current{};
};

DescriptorLayoutBuilder MakeBuilder(const Device& device, std::span<const Shader::Info> infos) {
    DescriptorLayoutBuilder builder{device};
    for (size_t index = 0; index < infos.size(); ++index) {
        static constexpr std::array stages{
            VK_SHADER_STAGE_VERTEX_BIT,
            VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
            VK_SHADER_STAGE_GEOMETRY_BIT,
            VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        builder.Add(infos[index], stages.at(index));
    }
    return builder;
}

template <class StencilFace>
VkStencilOpState GetStencilFaceState(const StencilFace& face) {
    return {
        .failOp = MaxwellToVK::StencilOp(face.ActionStencilFail()),
        .passOp = MaxwellToVK::StencilOp(face.ActionDepthPass()),
        .depthFailOp = MaxwellToVK::StencilOp(face.ActionDepthFail()),
        .compareOp = MaxwellToVK::ComparisonOp(face.TestFunc()),
        .compareMask = 0,
        .writeMask = 0,
        .reference = 0,
    };
}

bool SupportsPrimitiveRestart(VkPrimitiveTopology topology) {
    static constexpr std::array unsupported_topologies{
        VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
        VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
        // VK_PRIMITIVE_TOPOLOGY_QUAD_LIST_EXT,
    };
    return std::ranges::find(unsupported_topologies, topology) == unsupported_topologies.end();
}

bool IsLine(VkPrimitiveTopology topology) {
    static constexpr std::array line_topologies{
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
        // VK_PRIMITIVE_TOPOLOGY_LINE_LOOP_EXT,
    };
    return std::ranges::find(line_topologies, topology) != line_topologies.end();
}

VkViewportSwizzleNV UnpackViewportSwizzle(u16 swizzle) {
    union Swizzle {
        u32 raw;
        BitField<0, 3, Maxwell::ViewportSwizzle> x;
        BitField<4, 3, Maxwell::ViewportSwizzle> y;
        BitField<8, 3, Maxwell::ViewportSwizzle> z;
        BitField<12, 3, Maxwell::ViewportSwizzle> w;
    };
    const Swizzle unpacked{swizzle};
    return VkViewportSwizzleNV{
        .x = MaxwellToVK::ViewportSwizzle(unpacked.x),
        .y = MaxwellToVK::ViewportSwizzle(unpacked.y),
        .z = MaxwellToVK::ViewportSwizzle(unpacked.z),
        .w = MaxwellToVK::ViewportSwizzle(unpacked.w),
    };
}

PixelFormat DecodeFormat(u8 encoded_format) {
    const auto format{static_cast<Tegra::RenderTargetFormat>(encoded_format)};
    if (format == Tegra::RenderTargetFormat::NONE) {
        return PixelFormat::Invalid;
    }
    return PixelFormatFromRenderTargetFormat(format);
}

RenderPassKey MakeRenderPassKey(const FixedPipelineState& state, const Device& device) {
    RenderPassKey key{};
    std::ranges::transform(state.color_formats, key.color_formats.begin(), DecodeFormat);
    if (state.depth_enabled != 0) {
        const auto depth_format{static_cast<Tegra::DepthFormat>(state.depth_format.Value())};
        key.depth_format = PixelFormatFromDepthFormat(depth_format);
    } else {
        key.depth_format = PixelFormat::Invalid;
    }
    key.samples = MaxwellToVK::MsaaMode(state.msaa_mode);
    const bool has_color = std::ranges::any_of(key.color_formats, [](PixelFormat format) {
        return format != PixelFormat::Invalid;
    });
    key.resolve_color =
        key.samples != VK_SAMPLE_COUNT_1_BIT && has_color && device.IsTiler();
    return key;
}

size_t NumAttachments(const FixedPipelineState& state) {
    size_t num{};
    for (size_t index = 0; index < Maxwell::NumRenderTargets; ++index) {
        const auto format{static_cast<Tegra::RenderTargetFormat>(state.color_formats[index])};
        if (format != Tegra::RenderTargetFormat::NONE) {
            num = index + 1;
        }
    }
    return num;
}

template <typename Spec>
bool Passes(const std::array<vk::ShaderModule, NUM_STAGES>& modules,
            const std::array<Shader::Info, NUM_STAGES>& stage_infos) {
    for (size_t stage = 0; stage < NUM_STAGES; ++stage) {
        if (!Spec::enabled_stages[stage] && modules[stage]) {
            return false;
        }
        const auto& info{stage_infos[stage]};
        if constexpr (!Spec::has_storage_buffers) {
            if (!info.storage_buffers_descriptors.empty()) {
                return false;
            }
        }
        if constexpr (!Spec::has_texture_buffers) {
            if (!info.texture_buffer_descriptors.empty()) {
                return false;
            }
        }
        if constexpr (!Spec::has_image_buffers) {
            if (!info.image_buffer_descriptors.empty()) {
                return false;
            }
        }
        if constexpr (!Spec::has_images) {
            if (!info.image_descriptors.empty()) {
                return false;
            }
        }
    }
    return true;
}

using ConfigureFuncPtr = bool (*)(GraphicsPipeline*, bool);

template <typename Spec, typename... Specs>
ConfigureFuncPtr FindSpec(const std::array<vk::ShaderModule, NUM_STAGES>& modules,
                          const std::array<Shader::Info, NUM_STAGES>& stage_infos) {
    if constexpr (sizeof...(Specs) > 0) {
        if (!Passes<Spec>(modules, stage_infos)) {
            return FindSpec<Specs...>(modules, stage_infos);
        }
    }
    return GraphicsPipeline::MakeConfigureSpecFunc<Spec>();
}

struct SimpleVertexFragmentSpec {
    static constexpr std::array<bool, 5> enabled_stages{true, false, false, false, true};
    static constexpr bool has_storage_buffers = false;
    static constexpr bool has_texture_buffers = false;
    static constexpr bool has_image_buffers = false;
    static constexpr bool has_images = false;
};

struct SimpleVertexSpec {
    static constexpr std::array<bool, 5> enabled_stages{true, false, false, false, false};
    static constexpr bool has_storage_buffers = false;
    static constexpr bool has_texture_buffers = false;
    static constexpr bool has_image_buffers = false;
    static constexpr bool has_images = false;
};

struct SimpleStorageSpec {
    static constexpr std::array<bool, 5> enabled_stages{true, false, false, false, true};
    static constexpr bool has_storage_buffers = true;
    static constexpr bool has_texture_buffers = false;
    static constexpr bool has_image_buffers = false;
    static constexpr bool has_images = false;
};

struct SimpleImageSpec {
    static constexpr std::array<bool, 5> enabled_stages{true, false, false, false, true};
    static constexpr bool has_storage_buffers = false;
    static constexpr bool has_texture_buffers = false;
    static constexpr bool has_image_buffers = false;
    static constexpr bool has_images = true;
};

struct DefaultSpec {
    static constexpr std::array<bool, 5> enabled_stages{true, true, true, true, true};
    static constexpr bool has_storage_buffers = true;
    static constexpr bool has_texture_buffers = true;
    static constexpr bool has_image_buffers = true;
    static constexpr bool has_images = true;
};

ConfigureFuncPtr ConfigureFunc(const std::array<vk::ShaderModule, NUM_STAGES>& modules,
                               const std::array<Shader::Info, NUM_STAGES>& infos) {
    return FindSpec<SimpleVertexSpec, SimpleVertexFragmentSpec, SimpleStorageSpec, SimpleImageSpec,
                    DefaultSpec>(modules, infos);
}
} // Anonymous namespace

// TODO(crueter): This is the worst-formatted code I have EVER seen
GraphicsPipeline::GraphicsPipeline(
    Scheduler& scheduler_, BufferCache& buffer_cache_, TextureCache& texture_cache_,
    vk::PipelineCache& pipeline_cache_, VideoCore::ShaderNotify* shader_notify,
    const Device& device_, DescriptorPool& descriptor_pool,
    GuestDescriptorQueue& guest_descriptor_queue_, DescriptorBufferRing& descriptor_buffer_ring_,
    Common::ThreadWorker* worker_thread,
    PipelineStatistics* pipeline_statistics, RenderPassCache& render_pass_cache,
    const GraphicsPipelineCacheKey& key_, std::array<vk::ShaderModule, NUM_STAGES> stages,
    const std::array<const Shader::Info*, NUM_STAGES>& infos)
    : key{key_}, device{device_}, texture_cache{texture_cache_}, buffer_cache{buffer_cache_},
      pipeline_cache(pipeline_cache_), scheduler{scheduler_},
      guest_descriptor_queue{guest_descriptor_queue_},
      descriptor_buffer_ring{descriptor_buffer_ring_}, spv_modules{std::move(stages)} {
    if (shader_notify) {
        shader_notify->MarkShaderBuilding();
    }
    // Layout creation and descriptor setup below can throw before a worker
    // owns the build. Return the shader notification if setup/queueing fails.
    bool build_handed_off = false;
    SCOPE_EXIT {
        if (!build_handed_off && shader_notify) {
            shader_notify->MarkShaderComplete();
        }
    };
    for (size_t stage = 0; stage < NUM_STAGES; ++stage) {
        const Shader::Info* const info{infos[stage]};
        if (!info) {
            continue;
        }
        stage_infos[stage] = *info;
        // Immutable shader-stage metadata: avoid repeated empty descriptor
        // walks while both guest buffer/texture caches are locked.
        // BindHostStageBuffers walks only uniform, storage, texture buffers.
        stage_has_host_buffer_bindings[stage] = info->constant_buffer_mask != 0 ||
            !info->storage_buffers_descriptors.empty() ||
            !info->texture_buffer_descriptors.empty() ||
            !info->image_buffer_descriptors.empty();
        stage_has_image_bindings[stage] =
            !info->texture_buffer_descriptors.empty() ||
            !info->image_buffer_descriptors.empty() ||
            !info->texture_descriptors.empty() ||
            !info->image_descriptors.empty();
        enabled_uniform_buffer_masks[stage] = info->constant_buffer_mask;
        std::ranges::copy(info->constant_buffer_used_sizes, uniform_buffer_sizes[stage].begin());
        num_image_elements += Shader::NumDescriptors(info->texture_buffer_descriptors);
        num_image_elements += Shader::NumDescriptors(info->image_buffer_descriptors);
        // Descriptor ranges are immutable for this compiled pipeline.
        // These two stage cursor offsets used to be rescanned per draw
        // while both guest GPU caches were locked.
        const size_t sampled_views = Shader::NumDescriptors(info->texture_descriptors);
        const size_t storage_views = Shader::NumDescriptors(info->image_descriptors);
        stage_sampled_view_counts[stage] = sampled_views;
        stage_storage_view_counts[stage] = storage_views;
        num_textures += sampled_views;
        num_image_elements += sampled_views;
        num_image_elements += storage_views;
        num_descriptor_entries += NumDescriptorEntries(*info);
    }
    fragment_has_color0_output = stage_infos[NUM_STAGES - 1].stores_frag_color[0];

    DescriptorLayoutBuilder builder{MakeBuilder(device, stage_infos)};
    uses_push_descriptor = builder.CanUsePushDescriptor();
    uses_descriptor_buffer = builder.CanUseDescriptorBuffer() && descriptor_buffer_ring.IsValid();
    descriptor_set_layout =
        builder.CreateDescriptorSetLayout(uses_push_descriptor, uses_descriptor_buffer);
    if (uses_descriptor_buffer) {
        descriptor_buffer_layout = builder.MakeDescriptorBufferLayout(*descriptor_set_layout);
        if (!descriptor_buffer_ring.CanAllocate(descriptor_buffer_layout.size)) {
            LOG_WARNING(Render_Vulkan,
                        "Graphics pipeline {:016X} needs {} descriptor bytes per draw, falling back "
                        "to sets",
                        key.Hash(), descriptor_buffer_layout.size);
            uses_descriptor_buffer = false;
            descriptor_buffer_layout = {};
            descriptor_set_layout = builder.CreateDescriptorSetLayout(uses_push_descriptor);
        }
    }

    const VkDescriptorSetLayout set_layout{*descriptor_set_layout};
    pipeline_layout = builder.CreatePipelineLayout(set_layout);
    if (!uses_descriptor_buffer) {
        descriptor_update_template =
            builder.CreateTemplate(set_layout, *pipeline_layout, uses_push_descriptor);
        if (!uses_push_descriptor) {
            descriptor_allocator =
                descriptor_pool.Allocator(device, scheduler, set_layout, stage_infos);
        }
    }

    auto func{[this, shader_notify, &render_pass_cache, pipeline_statistics, fast_first_runtime = (worker_thread != nullptr)] {
#ifdef PS5_NATIVE
        ::Eden::Performance::ProbeSchedulerOnce("VkPipelineBuilder");
#endif
        // Always release waiters, even when render-pass acquisition, shader
        // validation, fast-first optimisation scheduling or statistics throws.
        // A failed pipeline stays null and is rejected by the draw path.
        SCOPE_EXIT {
            {
                std::scoped_lock lock{build_mutex};
                is_built.store(true, std::memory_order_release);
            }
            build_condvar.notify_all();
            if (shader_notify) {
                shader_notify->MarkShaderComplete();
            }
        };
        try {
            const VkRenderPass render_pass{render_pass_cache.Get(MakeRenderPassKey(key.state, device))};
            Validate();
            if (::Eden::DevVulkan::fast_first_pipelines && fast_first_runtime) {
                MakePipeline(render_pass, VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT);
#ifdef PS5_NATIVE
                // This speculative rebuild is not required by the first-use
                // pipeline. Admission/OOM failures must not turn an already
                // valid gameplay pipeline into a failed builder invocation.
                try {
                    QueueOptimizedPipeline(render_pass);
                } catch (const std::exception& error) {
                    LOG_WARNING(Render_Vulkan, "Optional pipeline rebuild queue rejected: {}", error.what());
                } catch (...) {
                    LOG_WARNING(Render_Vulkan, "Optional pipeline rebuild queue rejected: unknown exception");
                }
#else
                QueueOptimizedPipeline(render_pass);
#endif
            } else {
                MakePipeline(render_pass);
            }
            if (pipeline_statistics && pipeline) {
                pipeline_statistics->Collect(device, *pipeline);
            }
        } catch (const std::exception& exception) {
            LOG_CRITICAL(Render_Vulkan, "Graphics pipeline build failed: {}", exception.what());
        } catch (...) {
            LOG_CRITICAL(Render_Vulkan, "Graphics pipeline build failed: unknown exception");
        }
    }};
    // Complete all potentially throwing pipeline configuration before
    // publishing the builder lambda that captures this pipeline by pointer.
    // The worker must never see a partially constructed ConfigureFunc.
    configure_func = ConfigureFunc(spv_modules, stage_infos);
    if (worker_thread) {
#ifdef PS5_NATIVE
        if (::Eden::DevVulkan::fast_first_pipelines) {
            // Cold first-use compilation has priority over speculative optimization.
            // Publish BEFORE enqueuing, including work waiting in the builder queue.
            pending_first_use_graphics_builds.fetch_add(1, std::memory_order_release);
            try {
                worker_thread->QueueWork([build = std::move(func)]() mutable {
                    SCOPE_EXIT {
                        pending_first_use_graphics_builds.fetch_sub(1, std::memory_order_release);
                    };
                    build();
                });
            } catch (...) {
                pending_first_use_graphics_builds.fetch_sub(1, std::memory_order_release);
                throw;
            }
        } else
#endif
        {
            worker_thread->QueueWork(std::move(func));
        }
        // QueueWork returned successfully; the worker now owns completion,
        // including paths where pipeline validation or compilation fails.
        build_handed_off = true;
    } else {
        // Synchronous func() owns the completion callback from this point.
        build_handed_off = true;
        func();
    }
}

void GraphicsPipeline::AddTransition(GraphicsPipeline* transition) {
#ifdef PS5_NATIVE
    // Both appends are optional memoization. The canonical graphics cache
    // already owns the target and will resolve a missing edge on demand.
    try {
        transition_keys.push_back(transition->key);
    } catch (...) {
        return;
    }
#else
    transition_keys.push_back(transition->key);
#endif
#ifdef PS5_NATIVE
    // Both arrays must describe the same transition index. An allocation
    // failure while appending the pointer must not leave an orphaned key:
    // Next() would later index past transitions and corrupt a guest draw.
    try {
        transitions.push_back(transition);
    } catch (...) {
        // Transition memoization is optional. The primary graphics cache
        // already owns the pipeline; a later lookup can use its slow path.
        // Do not abort a guest draw solely because this edge could not fit.
        transition_keys.pop_back();
        return;
    }
#else
    transitions.push_back(transition);
#endif
}

#ifdef PS5_NATIVE
void GraphicsPipeline::PreflightDescriptorScratch() {
    // Called by the GPU owner before PrepareDraw acquires the buffer/texture
    // cache pair. The immutable pipeline descriptor counts are safe here.
    // Nested Configure calls use their own scratch and fall back to reserve
    // under the lock; never corrupt an in-flight parent view list.
    // Nested Configure reserves its private thread-local slot before the GPU
    // cache mutexes. Depth >= 3 still has the existing safe fallback.
    auto& scratch = graphics_descriptor_scratch.in_use
                        ? graphics_descriptor_nested_scratch
                        : graphics_descriptor_scratch;
    if (scratch.in_use) return;
    if (scratch.views.capacity() < num_image_elements)
        scratch.views.reserve(num_image_elements);
    if (scratch.samplers.capacity() < num_textures)
        scratch.samplers.reserve(num_textures);
    // Allocate the per-pipeline descriptor snapshot BEFORE the buffer/texture
    // cache mutex pair. ConfigureDraw still reads, compares and publishes the
    // actual guest descriptors only after taking both locks.
    if (num_descriptor_entries != 0 && descriptor_set_layout &&
        (uses_descriptor_buffer || !uses_push_descriptor) &&
        last_descriptor_payload.capacity() < num_descriptor_entries) {
        last_descriptor_payload.reserve(num_descriptor_entries);
    }
}
#endif

template <typename Spec>
bool GraphicsPipeline::ConfigureImpl(bool is_indexed) {
    // Reuse storage even for common <=64-descriptor draws: constructing two
    // inline small_vectors for every draw still churned ~1 KiB of stack and
    // capacity metadata while both guest GPU cache locks were held.
    GraphicsDescriptorScratchLease scratch;
    auto& views = scratch.Views();
    auto& samplers = scratch.Samplers();
    views.reserve(num_image_elements);
    samplers.reserve(num_textures);

    // No TIC/TSC entries are read by a shader without image/image-buffer
    // descriptors. Leave dirty table state pending until a later textured
    // pipeline needs it; framebuffer bindings and shader invalidations are
    // independent of this table synchronization.
    if (num_image_elements != 0) {
        ::Eden::Performance::CachePhaseTimer cost{
            ::Eden::Performance::CachePhase::TextureDescriptorSync};
        texture_cache.SynchronizeDescriptors(false);
    }

    ::Eden::Performance::CachePhaseTimer stage_texture_phase{
        ::Eden::Performance::CachePhase::StageTextureHandles};
    buffer_cache.SetUniformBuffersState(enabled_uniform_buffer_masks, &uniform_buffer_sizes);

    const auto& regs{maxwell3d->regs};
    const bool via_header_index{regs.sampler_binding == Maxwell::SamplerBinding::ViaHeaderBinding};
    // Sampler table mapping is stable while the paired guest cache locks are
    // held. Cache repeated handles only for this draw: never reuse an ID after
    // a later guest descriptor invalidation, channel switch or pipeline change.
    // A bounded direct-mapped table avoids per-descriptor hash allocations.
    struct SamplerMemo {
        u32 handle{};
        VideoCommon::SamplerId id{};
        bool valid{};
    };
    std::array<SamplerMemo, 16> sampler_memo{};
    const auto resolve_sampler{[&](u32 handle) -> VideoCommon::SamplerId {
        auto& entry = sampler_memo[handle & 15U];
        if (!entry.valid || entry.handle != handle) {
            entry.id = texture_cache.GetSamplerId(handle, false);
            entry.handle = handle;
            entry.valid = true;
        }
        return entry.id;
    }};
    const auto config_stage{[&](size_t stage) LAMBDA_FORCEINLINE {
        const Shader::Info& info{stage_infos[stage]};
        buffer_cache.UnbindGraphicsStorageBuffers(stage);
        if constexpr (Spec::has_storage_buffers) {
            size_t ssbo_index{};
            for (const auto& desc : info.storage_buffers_descriptors) {
                ASSERT(desc.count == 1);
                buffer_cache.BindGraphicsStorageBuffer(stage, ssbo_index, desc.cbuf_index,
                                                       desc.cbuf_offset, desc.is_written);
                ++ssbo_index;
            }
        }
        const auto& cbufs{maxwell3d->state.shader_stages[stage].const_buffers};
        const auto read_handle{[&](const auto& desc, u32 index) {
            ASSERT(cbufs[desc.cbuf_index].enabled);
            const u32 index_offset{index << desc.size_shift};
            const u32 offset{desc.cbuf_offset + index_offset};
            const GPUVAddr addr{cbufs[desc.cbuf_index].address + offset};
            if constexpr (std::is_same_v<decltype(desc), const Shader::TextureDescriptor&> ||
                          std::is_same_v<decltype(desc), const Shader::TextureBufferDescriptor&>) {
                if (desc.has_secondary) {
                    ASSERT(cbufs[desc.secondary_cbuf_index].enabled);
                    const u32 second_offset{desc.secondary_cbuf_offset + index_offset};
                    const GPUVAddr separate_addr{cbufs[desc.secondary_cbuf_index].address +
                                                 second_offset};
                    const u32 lhs_raw{gpu_memory->Read<u32>(addr) << desc.shift_left};
                    const u32 rhs_raw{gpu_memory->Read<u32>(separate_addr)
                                      << desc.secondary_shift_left};
                    const u32 raw{lhs_raw | rhs_raw};
                    return TexturePair(raw, via_header_index);
                }
            }
            return TexturePair(gpu_memory->Read<u32>(addr), via_header_index);
        }};
        const auto add_image{[&](const auto& desc, bool blacklist) LAMBDA_FORCEINLINE {
            for (u32 index = 0; index < desc.count; ++index) {
                const auto handle{read_handle(desc, index)};
                views.push_back({
                    .index = handle.first,
                    .blacklist = blacklist,
                    .id = {}
                });
            }
        }};
        if constexpr (Spec::has_texture_buffers) {
            for (const auto& desc : info.texture_buffer_descriptors) {
                add_image(desc, false);
            }
        }
        if constexpr (Spec::has_image_buffers) {
            for (const auto& desc : info.image_buffer_descriptors) {
                add_image(desc, false);
            }
        }
        for (const auto& desc : info.texture_descriptors) {
            for (u32 index = 0; index < desc.count; ++index) {
                const auto handle{read_handle(desc, index)};
                views.push_back({handle.first});
                samplers.push_back(resolve_sampler(handle.second));
            }
        }
        if constexpr (Spec::has_images) {
            for (const auto& desc : info.image_descriptors) {
                add_image(desc, desc.is_written);
            }
        }

        return true;
    }};
    if constexpr (Spec::enabled_stages[0]) {
        config_stage(0);
    }
    if constexpr (Spec::enabled_stages[1]) {
        config_stage(1);
    }
    if constexpr (Spec::enabled_stages[2]) {
        config_stage(2);
    }
    if constexpr (Spec::enabled_stages[3]) {
        config_stage(3);
    }
    if constexpr (Spec::enabled_stages[4]) {
        config_stage(4);
    }
    ASSERT(views.size() == num_image_elements);
    ASSERT(samplers.size() == num_textures);
    stage_texture_phase.Stop();
    // FillImageViews resets the image-deletion/retry latch even for no images.
    // Keep that state transition for graphics as for compute; no TIC is resolved.
    { ::Eden::Performance::CachePhaseTimer cost{
        ::Eden::Performance::CachePhase::TextureFillImageViews};
        texture_cache.FillImageViews(std::span(views.data(), views.size()), false,
                                     Spec::has_images);
    }

    ::Eden::Performance::CachePhaseTimer bind_texture_phase{
        ::Eden::Performance::CachePhase::BindTextureBuffers};
    VideoCommon::ImageViewInOut* texture_buffer_it{views.data()};
    const auto bind_stage_info{[&](size_t stage) LAMBDA_FORCEINLINE {
        size_t index{};
        const auto add_buffer{[&](const auto& desc) {
            constexpr bool is_image = std::is_same_v<decltype(desc), const ImageBufferDescriptor&>;
            for (u32 i = 0; i < desc.count; ++i) {
                bool is_written{false};
                if constexpr (is_image) {
                    is_written = desc.is_written;
                }
                ImageView& image_view{texture_cache.GetImageView(texture_buffer_it->id)};
                PixelFormat format{image_view.format};
                if constexpr (is_image) {
                    if (const auto explicit_format{PixelFormatFromImageFormat(desc.format)}) {
                        format = *explicit_format;
                    }
                }
                buffer_cache.BindGraphicsTextureBuffer(stage, index, image_view.GpuAddr(),
                                                       image_view.BufferSize(), format,
                                                       is_written, is_image);
                ++index;
                ++texture_buffer_it;
            }
        }};
        buffer_cache.UnbindGraphicsTextureBuffers(stage);

        const Shader::Info& info{stage_infos[stage]};
        if constexpr (Spec::has_texture_buffers) {
            for (const auto& desc : info.texture_buffer_descriptors) {
                add_buffer(desc);
            }
        }
        if constexpr (Spec::has_image_buffers) {
            for (const auto& desc : info.image_buffer_descriptors) {
                add_buffer(desc);
            }
        }
        // Stage descriptor counts were recorded during construction,
        // outside the paired guest buffer/texture cache lock.
        texture_buffer_it += stage_sampled_view_counts[stage];
        if constexpr (Spec::has_images) {
            texture_buffer_it += stage_storage_view_counts[stage];
        }
    }};
    if constexpr (Spec::enabled_stages[0]) {
        bind_stage_info(0);
    }
    if constexpr (Spec::enabled_stages[1]) {
        bind_stage_info(1);
    }
    if constexpr (Spec::enabled_stages[2]) {
        bind_stage_info(2);
    }
    if constexpr (Spec::enabled_stages[3]) {
        bind_stage_info(3);
    }
    if constexpr (Spec::enabled_stages[4]) {
        bind_stage_info(4);
    }

    bind_texture_phase.Stop();
    if (regs.transform_feedback_enabled != 0) {
        scheduler.RequestOutsideRenderPassOperationContext();
    }

    { ::Eden::Performance::CachePhaseTimer cost{::Eden::Performance::CachePhase::GraphicsBufferUpdate};
        buffer_cache.UpdateGraphicsBuffers(is_indexed);
    }
    buffer_cache.BindHostGeometryBuffers(is_indexed);

    { ::Eden::Performance::CachePhaseTimer cost{::Eden::Performance::CachePhase::DescriptorQueueAcquire};
        // Exact zero descriptor count: Acquire(0) otherwise reserves 1024
        // entries and can stall the GPU worker under both guest cache locks.
        if (num_descriptor_entries == 0) {
            guest_descriptor_queue.AcquireKnownEmpty(uses_descriptor_buffer);
        } else {
            guest_descriptor_queue.Acquire(scheduler, num_descriptor_entries, uses_descriptor_buffer);
        }
    }

    ::Eden::Performance::CachePhaseTimer stage_descriptor_phase{
        ::Eden::Performance::CachePhase::BindStageDescriptors};
    RescalingPushConstant rescaling;
    RenderAreaPushConstant render_area;
    const VideoCommon::SamplerId* samplers_it{samplers.data()};
    const VideoCommon::ImageViewInOut* views_it{views.data()};
    const auto prepare_stage{[&](size_t stage) LAMBDA_FORCEINLINE {
        // Detailed-only 1/16 sampling: separate host buffer synchronization
        // from image descriptor push work inside the existing paired locks.
        // Skip three guaranteed-empty bitset walks under paired GPU locks.
        // Image descriptors are handled independently and remain untouched.
        if (stage_has_host_buffer_bindings[stage]) {
            ::Eden::Performance::CachePhaseTimer phase{
                ::Eden::Performance::CachePhase::StageHostBuffers};
            buffer_cache.BindHostStageBuffers(stage);
        }
        const auto& info{stage_infos[stage]};
        // No image or texture-buffer descriptors: PushImageDescriptors
        // cannot change view cursors, descriptor queue or rescale constants.
        // Uniform/storage buffer bindings above must still execute.
        if (stage_has_image_bindings[stage]) {
            ::Eden::Performance::CachePhaseTimer phase{
                ::Eden::Performance::CachePhase::StageImageDescriptors};
            PushImageDescriptors(texture_cache, guest_descriptor_queue, info, rescaling,
                                 samplers_it, views_it);
        }
        if (info.uses_render_area) {
            render_area.uses_render_area = true;
            render_area.words = {static_cast<float>(regs.surface_clip.width),
                                 static_cast<float>(regs.surface_clip.height)};
        }
    }};
    if constexpr (Spec::enabled_stages[0]) {
        prepare_stage(0);
    }
    if constexpr (Spec::enabled_stages[1]) {
        prepare_stage(1);
    }
    if constexpr (Spec::enabled_stages[2]) {
        prepare_stage(2);
    }
    if constexpr (Spec::enabled_stages[3]) {
        prepare_stage(3);
    }
    if constexpr (Spec::enabled_stages[4]) {
        prepare_stage(4);
    }
    if (buffer_cache.any_buffer_uploaded) {
        buffer_cache.runtime.PostCopyBarrier();
        buffer_cache.any_buffer_uploaded = false;
    }
    stage_descriptor_phase.Stop();
    { ::Eden::Performance::CachePhaseTimer cost{
        ::Eden::Performance::CachePhase::RefreshRenderTargets};
        texture_cache.UpdateRenderTargets(false);
    }
    { ::Eden::Performance::CachePhaseTimer cost{
        ::Eden::Performance::CachePhase::CheckFeedbackLoop};
        texture_cache.CheckFeedbackLoop(std::span<const VideoCommon::ImageViewInOut>{views.data(),
                                                                                     views.size()});
    }
    // The first-use VkPipeline may fail while a separately rebuilt optimized
    // variant succeeds. Keep that recovered handle eligible for guest draws.
    if (IsBuilt() && !pipeline && !optimized_ready.load(std::memory_order_acquire)) {
        ::Eden::Performance::RecordGraphicsDrawDrop(
            ::Eden::Performance::GraphicsDrawDrop::BuiltPipelineFailed);
        return false;
    }
    { ::Eden::Performance::CachePhaseTimer cost{
        ::Eden::Performance::CachePhase::CommitGraphicsDraw};
        return ConfigureDraw(rescaling, render_area);
    }
}

bool GraphicsPipeline::ConfigureDraw(const RescalingPushConstant& rescaling,
                                     const RenderAreaPushConstant& render_area) {
    const void* const descriptor_data{guest_descriptor_queue.UpdateData()};

    VkDeviceSize descriptor_buffer_offset{};
    u32 descriptor_buffer_chunk{};
    if (descriptor_set_layout && uses_descriptor_buffer) {
        const auto* const entries = static_cast<const DescriptorUpdateEntry*>(descriptor_data);
        const bool reuse_allocation =
            last_descriptor_buffer_generation == descriptor_buffer_ring.CurrentGeneration() &&
            last_descriptor_payload.size() == num_descriptor_entries &&
            (num_descriptor_entries == 0 ||
             std::memcmp(last_descriptor_payload.data(), entries,
                         num_descriptor_entries * sizeof(DescriptorUpdateEntry)) == 0);
        if (reuse_allocation) {
            descriptor_buffer_offset = last_descriptor_buffer_offset;
            descriptor_buffer_chunk = last_descriptor_buffer_chunk;
            descriptor_buffer_ring.TouchFrame(scheduler);
        } else {
            const DescriptorBufferRing::Allocation alloc{
                descriptor_buffer_ring.Allocate(scheduler, descriptor_buffer_layout.size)};
            if (!alloc.host) {
                ::Eden::Performance::RecordGraphicsDrawDrop(
                    ::Eden::Performance::GraphicsDrawDrop::DescriptorBufferFull);
                LOG_DEBUG(Render_Vulkan, "Failed to reserve descriptor memory, skipping draw");
                return false;
            }
            WriteDescriptorBuffer(device, descriptor_buffer_layout, entries, alloc.host);
            descriptor_buffer_offset = alloc.offset;
            descriptor_buffer_chunk = alloc.chunk;
            last_descriptor_buffer_offset = alloc.offset;
            last_descriptor_buffer_chunk = alloc.chunk;
            last_descriptor_buffer_generation = alloc.generation;
            // Empty shader layouts have no descriptor payload to copy.
            if (num_descriptor_entries != 0) {
                last_descriptor_payload.assign(entries, entries + num_descriptor_entries);
            } else {
                last_descriptor_payload.clear();
            }
        }
    }

    scheduler.RequestRenderpass(texture_cache.GetFramebuffer());
    // Match the builder's release publication before using its pipeline handle.
    // A ready fast path skips the queued wait, so relaxed is insufficient here.
    if (!is_built.load(std::memory_order_acquire)) {
#ifdef PS5_NATIVE
        // The Vulkan worker processes command chunks in FIFO order. Once a
        // readiness wait precedes the first draw, later draws for this same
        // pipeline are already ordered behind it. GPU-owner thread only.
        if (!first_use_wait_recorded) {
#endif
        // Wait for the pipeline to be built
        scheduler.Record([this](vk::CommandBuffer) {
            // Builder published the pipeline before this ordered GPU callback.
            // Acquire its completion without taking a redundant build mutex.
            // An unfinished build still follows the original condition wait.
            if (is_built.load(std::memory_order_acquire)) return;
#ifdef PS5_NATIVE
            // Publication can race the initial fast acquire. The build owner
            // holds this mutex when publishing, so a false result here means
            // the condition wait must actually release the lock.
            // Include contention acquiring build_mutex in the keyed
            // diagnostic; a genuinely blocked condvar wait must not omit
            // the GPU worker's preceding mutex delay.
            ::Eden::Performance::PipelineWaitTrace keyed_wait;
            std::unique_lock lock{build_mutex};
            keyed_wait.MarkLockAcquired();
            if (is_built.load(std::memory_order_acquire)) return;
#endif
            auto timer = Eden::Performance::VulkanTimer(16);
            // Only unfinished builds contribute a worker wait; no clock in quiet mode.
            ::Eden::Performance::CachePhaseTimer pipeline_ready_wait{
                ::Eden::Performance::CachePhase::GraphicsPipelineWait,
                ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
#ifndef PS5_NATIVE
            std::unique_lock lock{build_mutex};
#endif
            build_condvar.wait(lock, [this] { return is_built.load(std::memory_order::relaxed); });
#ifdef PS5_NATIVE
            lock.unlock();
            pipeline_ready_wait.Stop();
            timer.reset(); // Exclude diagnostic print I/O from VulkanTimer(16).
            keyed_wait.Complete("graphics", key.Hash(), CanExecuteDraw());
#endif
        });
#ifdef PS5_NATIVE
        // Commit only after the callback was successfully enqueued.
        first_use_wait_recorded = true;
        }
#endif
    }
    const bool is_rescaling{texture_cache.IsRescaling()};
    const bool update_rescaling{scheduler.UpdateRescaling(is_rescaling)};
    // Publish a ready optimized variant at the next draw even if this
    // logical pipeline stays selected across multiple command buffers.
    const bool optimized_at_record = optimized_ready.load(std::memory_order_acquire);
    const bool bind_pipeline{scheduler.UpdateGraphicsPipeline(this, optimized_at_record)};
    const bool bind_descriptor_buffer{
        descriptor_set_layout && uses_descriptor_buffer &&
        scheduler.UpdateDescriptorBufferChunk(descriptor_buffer_chunk)};

    // Log graphics pipeline binding
    if (bind_pipeline && GPU::Logging::IsActive() &&
        Settings::values.gpu_log_vulkan_calls.GetValue()) {
        const std::string pipeline_info = fmt::format("hash={:#016x}", key.Hash());
        GPU::Logging::GPULogger::GetInstance().LogPipelineBind(false, pipeline_info);
    }

    bool update_descriptors = true;
    if (descriptor_set_layout && !uses_push_descriptor && !uses_descriptor_buffer) {
        const auto* const entries = static_cast<const DescriptorUpdateEntry*>(descriptor_data);
        update_descriptors =
            bind_pipeline || last_descriptor_payload.size() != num_descriptor_entries ||
            (num_descriptor_entries != 0 &&
             std::memcmp(last_descriptor_payload.data(), entries,
                         num_descriptor_entries * sizeof(DescriptorUpdateEntry)) != 0);
#ifndef PS5_NATIVE
        // The legacy desktop path preserves its existing early cache memo.
        if (update_descriptors) {
            if (num_descriptor_entries != 0) {
                last_descriptor_payload.assign(entries, entries + num_descriptor_entries);
            } else {
                last_descriptor_payload.clear();
            }
        }
#endif
    }
    scheduler.Record([this, descriptor_data, bind_pipeline, optimized_at_record, update_descriptors,
                      descriptor_buffer_offset, descriptor_buffer_chunk, bind_descriptor_buffer,
                      rescaling_data = rescaling.Data(), is_rescaling, update_rescaling,
                      uses_render_area = render_area.uses_render_area,
                      render_area_data = render_area.words](vk::CommandBuffer cmdbuf) {
        if (bind_descriptor_buffer) {
            const VkDescriptorBufferBindingInfoEXT binding_info{
                descriptor_buffer_ring.BindingInfo(descriptor_buffer_chunk)};
            cmdbuf.BindDescriptorBuffersEXT(binding_info);
        }
        // The worker must verify validity on EVERY draw: a failed pipeline
        // remains selected by the guest scheduler after the first attempted
        // bind. Otherwise subsequent draws can accidentally reuse a previous
        // shader while the optimized rebuild is still missing.
        const bool use_optimized = optimized_ready.load(std::memory_order_acquire);
#ifdef PS5_NATIVE
        // Reset BEFORE any early failure. Availability observed by a later
        // Draw callback is insufficient: its ConfigureDraw may already have
        // returned without binding anything when the rebuild was still cold.
        worker_draw_bind_valid = false;
#endif
        if (!pipeline && !use_optimized) {
            ::Eden::Performance::RecordGraphicsDrawDrop(
                ::Eden::Performance::GraphicsDrawDrop::WorkerPipelineFailed);
            return;
        }
        // Optimization may publish between GPU-owner recording and ordered
        // Vulkan-worker execution. Rebind the newly published handle in this
        // callback even when the owner recorded no pipeline-key transition.
        if (bind_pipeline || (use_optimized != optimized_at_record)) {
            cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                use_optimized ? *optimized_pipeline : *pipeline);
        }
#ifdef PS5_NATIVE
        // This callback precedes its Draw/DrawIndirect in scheduler FIFO.
        // A late optimized-ready signal cannot retroactively validate an
        // earlier skipped BindPipeline.
        worker_draw_bind_valid = true;
#endif
        cmdbuf.PushConstants(*pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS,
                             RESCALING_LAYOUT_WORDS_OFFSET, sizeof(rescaling_data),
                             rescaling_data.data());
        if (update_rescaling) {
            const f32 config_down_factor{Settings::values.resolution_info.down_factor};
            const f32 scale_down_factor{is_rescaling ? config_down_factor : 1.0f};
            cmdbuf.PushConstants(*pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS,
                                 RESCALING_LAYOUT_DOWN_FACTOR_OFFSET, sizeof(scale_down_factor),
                                 &scale_down_factor);
        }
        if (uses_render_area) {
            cmdbuf.PushConstants(*pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS,
                                 RENDERAREA_LAYOUT_OFFSET, sizeof(render_area_data),
                                 &render_area_data);
        }
        if (!descriptor_set_layout) {
            return;
        }
        if (uses_descriptor_buffer) {
            const u32 buffer_index{};
            cmdbuf.SetDescriptorBufferOffsetsEXT(VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline_layout,
                                                 0, buffer_index, descriptor_buffer_offset);
        } else if (uses_push_descriptor) {
            cmdbuf.PushDescriptorSetWithTemplateKHR(*descriptor_update_template, *pipeline_layout,
                                                    0, descriptor_data);
        } else if (update_descriptors) {
            const VkDescriptorSet descriptor_set{descriptor_allocator.Commit()};
            const vk::Device& dev{device.GetLogical()};
            dev.UpdateDescriptorSet(descriptor_set, *descriptor_update_template, descriptor_data);
            cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline_layout, 0,
                                      descriptor_set, nullptr);
        }
    });
#ifdef PS5_NATIVE
    // Descriptor-set updates are encoded by the Vulkan worker. If Record
    // throws before accepting the draw, retaining the old memo forces the
    // next HUD draw to publish its changed descriptors instead of reusing
    // an unsubmitted descriptor set.
    if (descriptor_set_layout && !uses_push_descriptor && !uses_descriptor_buffer &&
        update_descriptors) {
        const auto* const entries = static_cast<const DescriptorUpdateEntry*>(descriptor_data);
        if (num_descriptor_entries != 0) {
            last_descriptor_payload.assign(entries, entries + num_descriptor_entries);
        } else {
            last_descriptor_payload.clear();
        }
    }
#endif
    return true;
}


namespace {
#ifdef PS5_NATIVE
// Only first-use pipelines are mandatory. Cap queued + running speculative
// driver rebuilds so HUD shader bursts cannot accumulate unbounded work.
constexpr unsigned kMaxQueuedOptionalOptimizations = 16;
std::atomic<unsigned> pending_optional_optimizations{0};

bool ReserveOptionalOptimizationSlot() noexcept {
    unsigned pending = pending_optional_optimizations.load(std::memory_order_relaxed);
    while (pending < kMaxQueuedOptionalOptimizations) {
        if (pending_optional_optimizations.compare_exchange_weak(
                pending, pending + 1, std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}
#endif
Common::ThreadWorker& OptimizeWorker() {
    static Common::ThreadWorker worker(1, "VkPipelineOptimize");
    return worker;
}
} // namespace

GraphicsPipeline::~GraphicsPipeline() {
    // The initial Vulkan builder also captures 'this'. It can still be in
    // the first-use queue when the pipeline cache evicts this object.
    // Join its completion BEFORE reading optimize_state, which the builder
    // may create when it schedules an optional optimized rebuild.
    if (!is_built.load(std::memory_order_acquire)) {
        std::unique_lock build_lock{build_mutex};
        build_condvar.wait(build_lock, [this] {
            return is_built.load(std::memory_order_acquire);
        });
    }
    const auto state = optimize_state;
    if (!state) return;
    std::unique_lock lock{state->mutex};
    state->cancelled = true;
    state->done.wait(lock, [&] { return !state->running; });
}

void GraphicsPipeline::QueueOptimizedPipeline(VkRenderPass render_pass) {
#ifdef PS5_NATIVE
    // A skipped OPTIONAL rebuild keeps the already-built first-use pipeline.
    // Reserve before allocations so an overloaded worker uses no extra memory.
    if (!ReserveOptionalOptimizationSlot()) return;
    bool optional_slot_handed_off = false;
    SCOPE_EXIT {
        if (!optional_slot_handed_off) {
            pending_optional_optimizations.fetch_sub(1, std::memory_order_release);
        }
    };
#endif
    optimize_state = std::make_shared<OptimizeState>();
    const auto state = optimize_state;
    // An accepted optimization is in flight even while queued. Otherwise the
    // owner can be destroyed before the worker begins, leaving a dangling 'this'.
    {
        std::scoped_lock lock{state->mutex};
        state->running = true;
    }
    try {
        OptimizeWorker().QueueWork([this, state, render_pass] {
        // Always release a pending/active job, including early cancellation and exceptions.
        struct CompletionGuard {
            const std::shared_ptr<OptimizeState>& state;
            ~CompletionGuard() {
                {
                    std::scoped_lock lock{state->mutex};
                    state->running = false;
                }
                state->done.notify_all();
#ifdef PS5_NATIVE
                // Every accepted job releases its slot, including cancellation.
                pending_optional_optimizations.fetch_sub(1, std::memory_order_release);
#endif
            }
        } completion{state};
#ifdef PS5_NATIVE
        ::Eden::Performance::ProbeSchedulerOnce("VkPipelineOptimize");
        // R444: Speculative rebuilds must not compete with first-use shaders.
        // The prior 48 ms deadline could expire while cold gameplay pipelines
        // were still compiling, adding a second Vulkan compiler at the worst
        // moment. This background-only wait takes no GPU/guest locks.
        // Checking cancellation on each cycle guarantees prompt title teardown
        // even when a first-use shader worker remains stuck.
        // A momentarily empty builder queue is not an idle GPU frame.
        // Require a brief QUIET period before any OPTIONAL driver rebuild:
        // new HUD shaders appearing in a burst reset the countdown. This
        // sleep is confined to the optimizer worker, never the GPU owner.
        constexpr auto quiet_window = std::chrono::milliseconds(16);
        auto quiet_since = std::chrono::steady_clock::now();
        for (;;) {
            const bool cold_build_pending =
                pending_first_use_graphics_builds.load(std::memory_order_acquire) != 0 ||
                pending_first_use_compute_builds.load(std::memory_order_acquire) != 0;
            if (cold_build_pending) {
                quiet_since = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - quiet_since >= quiet_window) {
                break;
            }
            {
                std::scoped_lock lock{state->mutex};
                if (state->cancelled) return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
#endif
        {
            std::scoped_lock lock{state->mutex};
            if (state->cancelled) return;
        }
        try {
            MakePipeline(render_pass, 0, &optimized_pipeline);
            // A non-throwing driver failure may still produce a null handle.
            // Keep using the working first-use pipeline in that case.
            if (optimized_pipeline) {
                optimized_ready.store(true, std::memory_order_release);
            }
        } catch (const std::exception& exception) {
            // Keep the already-built, valid first-use pipeline after any
            // optimization failure. Completion must always release shutdown.
            LOG_WARNING(Render_Vulkan, "Optimized pipeline rebuild failed: {}", exception.what());
        } catch (...) {
            LOG_WARNING(Render_Vulkan, "Optimized pipeline rebuild failed: unknown exception");
        }
        });
#ifdef PS5_NATIVE
        // The queued worker now owns the slot. The caller's failure guard
        // handles allocation/admission exceptions before this handoff.
        optional_slot_handed_off = true;
#endif
    } catch (...) {
        // QueueWork failed before the worker accepted ownership. Do not strand
        // a pending flag that would make the destructor wait forever.
        {
            std::scoped_lock lock{state->mutex};
            state->running = false;
        }
        state->done.notify_all();
        throw;
    }
}

void GraphicsPipeline::MakePipeline(VkRenderPass render_pass, VkPipelineCreateFlags extra_flags,
                                    vk::Pipeline* target) {
    FixedPipelineState::DynamicState dynamic{};
    if (!key.state.extended_dynamic_state) {
        dynamic = key.state.dynamic_state;
    } else {
        dynamic.raw1 = key.state.dynamic_state.raw1;
    }
    static_vector<VkVertexInputBindingDescription, 32> vertex_bindings;
    static_vector<VkVertexInputBindingDivisorDescriptionEXT, 32> vertex_binding_divisors;
    static_vector<VkVertexInputAttributeDescription, 32> vertex_attributes;
    if (!key.state.dynamic_vertex_input) {
        const size_t num_vertex_arrays = (std::min)(
            Maxwell::NumVertexArrays, static_cast<size_t>(device.GetMaxVertexInputBindings()));
        for (size_t index = 0; index < num_vertex_arrays; ++index) {
            const bool instanced = key.state.binding_divisors[index] != 0;
            const auto rate =
                instanced ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            vertex_bindings.push_back({
                .binding = static_cast<u32>(index),
                .stride = key.state.vertex_strides[index],
                .inputRate = rate,
            });
            if (instanced) {
                vertex_binding_divisors.push_back({
                    .binding = static_cast<u32>(index),
                    .divisor = key.state.binding_divisors[index],
                });
            }
        }
        for (size_t index = 0; index < key.state.attributes.size(); ++index) {
            const auto& attribute = key.state.attributes[index];
            if (!attribute.enabled || !stage_infos[0].loads.Generic(index)) {
                continue;
            }
            vertex_attributes.push_back({
                .location = static_cast<u32>(index),
                .binding = attribute.buffer,
                .format = MaxwellToVK::VertexFormat(device, attribute.Type(), attribute.Size()),
                .offset = attribute.offset,
            });
        }
    }
    ASSERT(vertex_attributes.size() <= device.GetMaxVertexInputAttributes());

    VkPipelineVertexInputStateCreateInfo vertex_input_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = static_cast<u32>(vertex_bindings.size()),
        .pVertexBindingDescriptions = vertex_bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes.size()),
        .pVertexAttributeDescriptions = vertex_attributes.data(),
    };
    const VkPipelineVertexInputDivisorStateCreateInfoEXT input_divisor_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT,
        .pNext = nullptr,
        .vertexBindingDivisorCount = static_cast<u32>(vertex_binding_divisors.size()),
        .pVertexBindingDivisors = vertex_binding_divisors.data(),
    };
    if (!vertex_binding_divisors.empty()) {
        vertex_input_ci.pNext = &input_divisor_ci;
    }
    const bool has_tess_stages = spv_modules[1] || spv_modules[2];
    auto input_assembly_topology = MaxwellToVK::PrimitiveTopology(device, key.state.topology);
    if (input_assembly_topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST) {
        if (!has_tess_stages) {
            LOG_WARNING(Render_Vulkan, "Patch topology used without tessellation, using points");
            input_assembly_topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        }
    } else {
        if (has_tess_stages) {
            // The Vulkan spec requires patch list IA topology be used with tessellation
            // shader stages. Forcing it fixes a crash on some drivers
            LOG_WARNING(Render_Vulkan,
                        "Patch topology not used with tessellation, using patch list");
            input_assembly_topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        }
    }
    const VkPipelineInputAssemblyStateCreateInfo input_assembly_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = input_assembly_topology,
        .primitiveRestartEnable =
        // MoltenVK/Metal always has primitive restart enabled and cannot disable it
        device.IsMoltenVK() ? VK_TRUE :
        (dynamic.primitive_restart_enable != 0 &&
                ((input_assembly_topology != VK_PRIMITIVE_TOPOLOGY_PATCH_LIST &&
                  device.IsTopologyListPrimitiveRestartSupported()) ||
                 SupportsPrimitiveRestart(input_assembly_topology) ||
                 (input_assembly_topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST &&
                  device.IsPatchListPrimitiveRestartSupported()))
            ? VK_TRUE
            : VK_FALSE),
    };
    const VkPipelineTessellationStateCreateInfo tessellation_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .patchControlPoints = key.state.patch_control_points_minus_one.Value() + 1,
    };
    std::array<VkViewportSwizzleNV, Maxwell::NumViewports> swizzles;
    std::ranges::transform(key.state.viewport_swizzles, swizzles.begin(), UnpackViewportSwizzle);
    VkPipelineViewportSwizzleStateCreateInfoNV swizzle_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_SWIZZLE_STATE_CREATE_INFO_NV,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = Maxwell::NumViewports,
        .pViewportSwizzles = swizzles.data(),
    };
    VkPipelineViewportDepthClipControlCreateInfoEXT ndc_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT,
        .pNext = nullptr,
        .negativeOneToOne = key.state.ndc_minus_one_to_one.Value() != 0 ? VK_TRUE : VK_FALSE,
    };
    const u32 num_viewports = std::min<u32>(device.GetMaxViewports(), Maxwell::NumViewports);
    VkPipelineViewportStateCreateInfo viewport_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = num_viewports,
        .pViewports = nullptr,
        .scissorCount = num_viewports,
        .pScissors = nullptr,
    };
    if (device.IsNvViewportSwizzleSupported()) {
        swizzle_ci.pNext = std::exchange(viewport_ci.pNext, &swizzle_ci);
    }
    if (device.IsExtDepthClipControlSupported()) {
        ndc_info.pNext = std::exchange(viewport_ci.pNext, &ndc_info);
    }
    VkPipelineRasterizationStateCreateInfo rasterization_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable =
        static_cast<VkBool32>(dynamic.depth_clamp_disabled == 0 ? VK_TRUE : VK_FALSE),
        .rasterizerDiscardEnable =
        static_cast<VkBool32>(dynamic.rasterize_enable == 0 ? VK_TRUE : VK_FALSE),
        .polygonMode =
        MaxwellToVK::PolygonMode(FixedPipelineState::UnpackPolygonMode(key.state.polygon_mode)),
        .cullMode = static_cast<VkCullModeFlags>(
            dynamic.cull_enable ? MaxwellToVK::CullFace(dynamic.CullFace()) : VK_CULL_MODE_NONE),
        .frontFace = MaxwellToVK::FrontFace(dynamic.FrontFace()),
        .depthBiasEnable = (dynamic.depth_bias_enable != 0 ? VK_TRUE : VK_FALSE),
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
        // TODO(alekpop): Transfer from regs
    };
    const bool smooth_lines_supported =
        device.IsExtLineRasterizationSupported() && device.SupportsSmoothLines();
    const bool stippled_lines_supported =
        device.IsExtLineRasterizationSupported() && device.SupportsStippledRectangularLines();
    VkPipelineRasterizationLineStateCreateInfoEXT line_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT,
        .pNext = nullptr,
        .lineRasterizationMode = key.state.smooth_lines != 0 && smooth_lines_supported
                                     ? VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT
                                     : VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT,
        .stippledLineEnable =
            (dynamic.line_stipple_enable && stippled_lines_supported) ? VK_TRUE : VK_FALSE,
        .lineStippleFactor = key.state.line_stipple_factor,
        .lineStipplePattern = static_cast<uint16_t>(key.state.line_stipple_pattern),
    };
    VkPipelineRasterizationConservativeStateCreateInfoEXT conservative_raster{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .conservativeRasterizationMode = key.state.conservative_raster_enable != 0
                                             ? VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT
                                             : VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT,
        .extraPrimitiveOverestimationSize = 0.0f,
    };
    const bool supports_provoking_first_mode = device.SupportsProvokingVertexFirstMode();
    const bool supports_provoking_last_mode = device.SupportsProvokingVertexLastMode();
    const bool requested_provoking_last_mode = key.state.provoking_vertex_last != 0;
    const VkProvokingVertexModeEXT provoking_vertex_mode =
        requested_provoking_last_mode
            ? (supports_provoking_last_mode ? VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT
                                            : VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT)
            : (supports_provoking_first_mode ? VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT
                                             : VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT);
    VkPipelineRasterizationProvokingVertexStateCreateInfoEXT provoking_vertex{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT,
        .pNext = nullptr,
        .provokingVertexMode = provoking_vertex_mode,
    };

    if (IsLine(input_assembly_topology) && device.IsExtLineRasterizationSupported()) {
        line_state.pNext = std::exchange(rasterization_ci.pNext, &line_state);
    }
    if (device.IsExtConservativeRasterizationSupported()) {
        conservative_raster.pNext = std::exchange(rasterization_ci.pNext, &conservative_raster);
    }
    if (device.IsExtProvokingVertexSupported()) {
        provoking_vertex.pNext = std::exchange(rasterization_ci.pNext, &provoking_vertex);
    }

    const bool supports_alpha_output = fragment_has_color0_output;
    const bool alpha_to_one_supported = device.SupportsAlphaToOne();
    const VkPipelineMultisampleStateCreateInfo multisample_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = MaxwellToVK::MsaaMode(key.state.msaa_mode),
        .sampleShadingEnable = Settings::values.sample_shading.GetValue() > 0 ? VK_TRUE : VK_FALSE,
        .minSampleShading = f32(Settings::values.sample_shading.GetValue()) / 100.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable =
            supports_alpha_output && key.state.alpha_to_coverage_enabled != 0 ? VK_TRUE : VK_FALSE,
        .alphaToOneEnable = supports_alpha_output && alpha_to_one_supported &&
                           key.state.alpha_to_one_enabled != 0 ? VK_TRUE : VK_FALSE,
    };
    const VkPipelineDepthStencilStateCreateInfo depth_stencil_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = dynamic.depth_test_enable,
        .depthWriteEnable = dynamic.depth_write_enable,
        .depthCompareOp = dynamic.depth_test_enable
                              ? MaxwellToVK::ComparisonOp(dynamic.DepthTestFunc())
                              : VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = dynamic.depth_bounds_enable && device.IsDepthBoundsSupported(),
        .stencilTestEnable = dynamic.stencil_enable,
        .front = GetStencilFaceState(dynamic.front),
        .back = GetStencilFaceState(dynamic.back),
        .minDepthBounds = f32(key.state.depth_bounds_min),
        .maxDepthBounds = f32(key.state.depth_bounds_max),
    };
    if (dynamic.depth_bounds_enable && !device.IsDepthBoundsSupported()) {
        LOG_WARNING(Render_Vulkan, "Depth bounds is enabled but not supported");
    }
    static_vector<VkPipelineColorBlendAttachmentState, Maxwell::NumRenderTargets> cb_attachments;
    const size_t num_attachments{NumAttachments(key.state)};
    for (size_t index = 0; index < num_attachments; ++index) {
        static constexpr std::array mask_table{
            VK_COLOR_COMPONENT_R_BIT,
            VK_COLOR_COMPONENT_G_BIT,
            VK_COLOR_COMPONENT_B_BIT,
            VK_COLOR_COMPONENT_A_BIT,
        };
        const auto& blend{key.state.attachments[index]};
        const std::array mask{blend.Mask()};
        VkColorComponentFlags write_mask{};
        for (size_t i = 0; i < mask_table.size(); ++i) {
            write_mask |= mask[i] ? mask_table[i] : 0;
        }
        cb_attachments.push_back({
            .blendEnable = blend.enable != 0,
            .srcColorBlendFactor = MaxwellToVK::BlendFactor(blend.SourceRGBFactor()),
            .dstColorBlendFactor = MaxwellToVK::BlendFactor(blend.DestRGBFactor()),
            .colorBlendOp = MaxwellToVK::BlendEquation(blend.EquationRGB()),
            .srcAlphaBlendFactor = MaxwellToVK::BlendFactor(blend.SourceAlphaFactor()),
            .dstAlphaBlendFactor = MaxwellToVK::BlendFactor(blend.DestAlphaFactor()),
            .alphaBlendOp = MaxwellToVK::BlendEquation(blend.EquationAlpha()),
            .colorWriteMask = write_mask,
        });
    }
    const VkPipelineColorBlendStateCreateInfo color_blend_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = dynamic.logic_op_enable != 0,
        .logicOp = static_cast<VkLogicOp>(dynamic.logic_op.Value()),
        .attachmentCount = static_cast<u32>(cb_attachments.size()),
        .pAttachments = cb_attachments.data(),
        .blendConstants = {}
    };
    static_vector<VkDynamicState, 34> dynamic_states{
        VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS,         VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_DEPTH_BOUNDS,       VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_LINE_WIDTH,
    };
    if (key.state.extended_dynamic_state) {
        static constexpr std::array extended{
            VK_DYNAMIC_STATE_CULL_MODE_EXT,
            VK_DYNAMIC_STATE_FRONT_FACE_EXT,
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_STENCIL_OP_EXT,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT,
        };
        dynamic_states.insert(dynamic_states.end(), extended.begin(), extended.end());

        // VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT
        if (!key.state.dynamic_vertex_input) {
            dynamic_states.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT);
        }
    }

    // VK_DYNAMIC_STATE_VERTEX_INPUT_EXT
    if (key.state.dynamic_vertex_input) {
        dynamic_states.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_EXT);
    }

    // EDS2 - Core (3 states)
    if (key.state.extended_dynamic_state_2) {
        static constexpr std::array extended2{
            VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE_EXT,
            VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE_EXT,
            VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE_EXT,
        };
        dynamic_states.insert(dynamic_states.end(), extended2.begin(), extended2.end());
    }

    // EDS2 - LogicOp (granular)
    if (key.state.extended_dynamic_state_2_logic_op) {
        dynamic_states.push_back(VK_DYNAMIC_STATE_LOGIC_OP_EXT);
    }

    // EDS3 - Blending (composite: 3 states)
    if (key.state.extended_dynamic_state_3_blend) {
        static constexpr std::array extended3{
            VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
            VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
            VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,
        };
        dynamic_states.insert(dynamic_states.end(), extended3.begin(), extended3.end());
    }

    // VK_EXT_color_write_enable fallback for fully on/off render targets when EDS3 blending is not available.
    if (!key.state.extended_dynamic_state_3_blend && key.state.color_write_enable_dynamic) {
        dynamic_states.push_back(VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT);
    }

    // EDS3 - Enables (composite: per-feature)
    if (key.state.extended_dynamic_state_3_enables) {
        if (device.SupportsDynamicState3DepthClampEnable()) {
            dynamic_states.push_back(VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT);
        }
        if (device.SupportsDynamicState3LogicOpEnable()) {
            dynamic_states.push_back(VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT);
        }
        if (device.SupportsDynamicState3LineRasterizationMode()) {
            dynamic_states.push_back(VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT);
        }
        if (device.SupportsDynamicState3ConservativeRasterizationMode()) {
            dynamic_states.push_back(VK_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT);
        }
        if (device.SupportsDynamicState3LineStippleEnable()) {
            dynamic_states.push_back(VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT);
        }
        if (device.SupportsDynamicState3AlphaToCoverageEnable()) {
            dynamic_states.push_back(VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT);
        }
        if (device.SupportsDynamicState3AlphaToOneEnable()) {
            dynamic_states.push_back(VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT);
        }
    }

    const VkPipelineDynamicStateCreateInfo dynamic_state_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };
    [[maybe_unused]] const VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup_size_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
        .pNext = nullptr,
        .requiredSubgroupSize = GuestWarpSize,
    };
    static_vector<VkPipelineShaderStageCreateInfo, 5> shader_stages;
    for (size_t stage = 0; stage < Maxwell::MaxShaderStage; ++stage) {
        if (!spv_modules[stage]) {
            continue;
        }
        [[maybe_unused]] auto& stage_ci =
            shader_stages.emplace_back(VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = MaxwellToVK::ShaderStage(Shader::StageFromIndex(stage)),
                .module = *spv_modules[stage],
                .pName = "main",
                .pSpecializationInfo = nullptr,
            });
    }
    VkPipelineCreateFlags flags{};
    if (device.IsKhrPipelineExecutablePropertiesEnabled() && Settings::values.renderer_debug.GetValue()) {
        flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    }
    if (uses_descriptor_buffer) {
        flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    (target ? *target : pipeline) = device.GetLogical().CreateGraphicsPipeline({
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags | extra_flags,
        .stageCount = static_cast<u32>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_input_ci,
        .pInputAssemblyState = &input_assembly_ci,
        .pTessellationState = &tessellation_ci,
        .pViewportState = &viewport_ci,
        .pRasterizationState = &rasterization_ci,
        .pMultisampleState = &multisample_ci,
        .pDepthStencilState = &depth_stencil_ci,
        .pColorBlendState = &color_blend_ci,
        .pDynamicState = &dynamic_state_ci,
        .layout = *pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
        .basePipelineHandle = nullptr,
        .basePipelineIndex = 0,
    }, *pipeline_cache);

    // Log graphics pipeline creation
    if (GPU::Logging::IsActive()) {
        const std::string pipeline_info = fmt::format(
            "GraphicsPipeline created: stages={}, attachments={}",
            shader_stages.size(),
            color_blend_ci.attachmentCount
        );
        GPU::Logging::GPULogger::GetInstance().LogPipelineStateChange(pipeline_info);
    }
}

void GraphicsPipeline::Validate() {
    size_t num_images{};
    for (const auto& info : stage_infos) {
        num_images += Shader::NumDescriptors(info.texture_buffer_descriptors);
        num_images += Shader::NumDescriptors(info.image_buffer_descriptors);
        num_images += Shader::NumDescriptors(info.texture_descriptors);
        num_images += Shader::NumDescriptors(info.image_descriptors);
    }
    ASSERT(num_images == num_image_elements);
}

} // namespace Vulkan
