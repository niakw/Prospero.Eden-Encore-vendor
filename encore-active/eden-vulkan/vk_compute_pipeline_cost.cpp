// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <array>
#include "common/scope_exit.h"
#include "performance.h"
#include <cstring>
#include <memory>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <fmt/format.h>

#include "video_core/renderer_vulkan/pipeline_helper.h"
#include "video_core/renderer_vulkan/pipeline_statistics.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/shader_notify.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/gpu_logging/gpu_logging.h"
#include "common/logging.h"
#include "common/settings.h"

namespace Vulkan {

#ifdef PS5_NATIVE
// Tracks queued and active first-use compute builds, not guest dispatches.
std::atomic<unsigned> pending_first_use_compute_builds{0};
#endif

using Shader::ImageBufferDescriptor;
using Shader::Backend::SPIRV::RESCALING_LAYOUT_WORDS_OFFSET;
using Tegra::Texture::TexturePair;

#ifdef PS5_NATIVE
namespace {
// Per-GPU-thread compute descriptor storage. The lease keeps a nested dispatch
// from corrupting an outer dispatch's views or sampler cursors.
struct ComputeDescriptorScratch {
    boost::container::small_vector<VideoCommon::ImageViewInOut, 64> views;
    boost::container::small_vector<VideoCommon::SamplerId, 64> samplers;
    bool in_use{};
};
thread_local ComputeDescriptorScratch compute_descriptor_scratch;
thread_local ComputeDescriptorScratch compute_descriptor_nested_scratch;

class ComputeDescriptorScratchLease {
public:
    ComputeDescriptorScratchLease() {
        if (!compute_descriptor_scratch.in_use) {
            current = &compute_descriptor_scratch;
            current->in_use = true;
        } else if (!compute_descriptor_nested_scratch.in_use) {
            current = &compute_descriptor_nested_scratch;
            current->in_use = true;
        } else {
            nested = std::make_unique<ComputeDescriptorScratch>();
            current = nested.get();
        }
        current->views.clear();
        current->samplers.clear();
    }
    ~ComputeDescriptorScratchLease() {
        if (!nested) current->in_use = false;
    }
    ComputeDescriptorScratchLease(const ComputeDescriptorScratchLease&) = delete;
    ComputeDescriptorScratchLease& operator=(const ComputeDescriptorScratchLease&) = delete;
    auto& Views() noexcept { return current->views; }
    auto& Samplers() noexcept { return current->samplers; }

private:
    std::unique_ptr<ComputeDescriptorScratch> nested;
    ComputeDescriptorScratch* current{};
};
} // namespace
#endif

ComputePipeline::ComputePipeline(const Device& device_, Scheduler& scheduler, vk::PipelineCache& pipeline_cache_,
                                 DescriptorPool& descriptor_pool,
                                 GuestDescriptorQueue& guest_descriptor_queue_,
                                 DescriptorBufferRing& descriptor_buffer_ring_,
                                 Common::ThreadWorker* thread_worker,
                                 PipelineStatistics* pipeline_statistics,
                                 VideoCore::ShaderNotify* shader_notify, const Shader::Info& info_,
                                 vk::ShaderModule spv_module_, u64 shader_hash_)
    : device{device_},
      pipeline_cache(pipeline_cache_), guest_descriptor_queue{guest_descriptor_queue_},
      descriptor_buffer_ring{descriptor_buffer_ring_}, info{info_},
      shader_hash{shader_hash_}, spv_module(std::move(spv_module_)) {
    if (shader_notify) {
        shader_notify->MarkShaderBuilding();
    }
#ifdef PS5_NATIVE
    // Layout setup and worker queue admission may throw. Return the shader
    // notification unless ownership has passed to the worker or inline build.
    bool build_handed_off = false;
    SCOPE_EXIT {
        if (!build_handed_off && shader_notify) {
            shader_notify->MarkShaderComplete();
        }
    };
#endif
    std::copy_n(info.constant_buffer_used_sizes.begin(), uniform_buffer_sizes.size(),
                uniform_buffer_sizes.begin());
    num_descriptor_entries = NumDescriptorEntries(info);
#ifdef PS5_NATIVE
    // Fixed by the compiled compute shader; safe to read before cache locks.
    num_image_elements = Shader::NumDescriptors(info.texture_buffer_descriptors) +
                         Shader::NumDescriptors(info.image_buffer_descriptors) +
                         Shader::NumDescriptors(info.texture_descriptors) +
                         Shader::NumDescriptors(info.image_descriptors);
    num_textures = Shader::NumDescriptors(info.texture_descriptors);
#endif

    DescriptorLayoutBuilder builder{device};
    builder.Add(info, VK_SHADER_STAGE_COMPUTE_BIT);

    uses_push_descriptor = builder.CanUsePushDescriptor();
    uses_descriptor_buffer = builder.CanUseDescriptorBuffer() && descriptor_buffer_ring.IsValid();
    descriptor_set_layout =
        builder.CreateDescriptorSetLayout(uses_push_descriptor, uses_descriptor_buffer);
    if (uses_descriptor_buffer) {
        descriptor_buffer_layout = builder.MakeDescriptorBufferLayout(*descriptor_set_layout);
        if (!descriptor_buffer_ring.CanAllocate(descriptor_buffer_layout.size)) {
            LOG_DEBUG(Render_Vulkan,
                        "Compute shader {:016X} needs {} descriptor bytes per dispatch, falling "
                        "back to sets",
                        shader_hash, descriptor_buffer_layout.size);
            uses_descriptor_buffer = false;
            descriptor_buffer_layout = {};
            descriptor_set_layout = builder.CreateDescriptorSetLayout(false);
        }
    }
    pipeline_layout = builder.CreatePipelineLayout(*descriptor_set_layout);
    if (!uses_descriptor_buffer) {
        descriptor_update_template =
            builder.CreateTemplate(*descriptor_set_layout, *pipeline_layout, uses_push_descriptor);
        if (!uses_push_descriptor) {
            descriptor_allocator =
                descriptor_pool.Allocator(device, scheduler, *descriptor_set_layout, info);
        }
    }

    auto func{[this, shader_notify, pipeline_statistics] {
#ifdef PS5_NATIVE
        ::Eden::Performance::ProbeSchedulerOnce("VkPipelineBuilder");
#endif
        // The Vulkan worker may throw before/after vkCreateComputePipelines;
        // never leave scheduler GPU build_condvar waiters blocked forever.
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
        const VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup_size_ci{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
            .pNext = nullptr,
            .requiredSubgroupSize = GuestWarpSize,
        };
        VkPipelineCreateFlags flags{};
        if (device.IsKhrPipelineExecutablePropertiesEnabled() && Settings::values.renderer_debug.GetValue()) {
            flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
        }
        if (uses_descriptor_buffer) {
            flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        }
        const VkComputePipelineCreateInfo compute_ci{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = flags,
            .stage{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext =
                    device.IsExtSubgroupSizeControlSupported() ? &subgroup_size_ci : nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = *spv_module,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            .layout = *pipeline_layout,
            .basePipelineHandle = 0,
            .basePipelineIndex = 0,
        };
            pipeline = device.GetLogical().CreateComputePipeline(compute_ci, *pipeline_cache);
        // Log compute pipeline creation
        if (GPU::Logging::IsActive()) {
            GPU::Logging::GPULogger::GetInstance().LogPipelineStateChange(
                "ComputePipeline created"
            );
        }

        if (pipeline_statistics && pipeline) {
            pipeline_statistics->Collect(device, *pipeline);
        }
        } catch (const std::exception& exception) {
            LOG_CRITICAL(Render_Vulkan, "Compute pipeline build failed {:016X}: {}",
                         shader_hash, exception.what());
        } catch (...) {
            LOG_CRITICAL(Render_Vulkan, "Compute pipeline build failed {:016X}: unknown exception",
                         shader_hash);
        }
    }};
    if (thread_worker) {
#ifdef PS5_NATIVE
        // Reserve a slot BEFORE QueueWork: a pending compute shader has
        // priority over a speculative optimized graphics rebuild.
        pending_first_use_compute_builds.fetch_add(1, std::memory_order_release);
        try {
            thread_worker->QueueWork([build = std::move(func)]() mutable {
                SCOPE_EXIT {
                    pending_first_use_compute_builds.fetch_sub(1, std::memory_order_release);
                };
                build();
            });
        } catch (...) {
            pending_first_use_compute_builds.fetch_sub(1, std::memory_order_release);
            throw;
        }
        build_handed_off = true;
#else
        thread_worker->QueueWork(std::move(func));
#endif
    } else {
#ifdef PS5_NATIVE
        // Inline func() has its own completion guard.
        build_handed_off = true;
#endif
        func();
    }
}

#ifdef PS5_NATIVE
void ComputePipeline::PreflightDescriptorScratch() {
    // Reserve on the GPU owner, before the paired guest cache mutexes.
    // Compare guest payload bytes only after their normal synchronization.
    if (uses_descriptor_buffer &&
        last_descriptor_payload.capacity() < num_descriptor_entries) {
        last_descriptor_payload.reserve(num_descriptor_entries);
    }
    // No guest texture/cache data is consulted here: counts are immutable.
    // Skip the slot currently owned by a nested Configure invocation.
    auto& scratch = compute_descriptor_scratch.in_use
                        ? compute_descriptor_nested_scratch
                        : compute_descriptor_scratch;
    if (scratch.in_use) return;
    if (scratch.views.capacity() < num_image_elements)
        scratch.views.reserve(num_image_elements);
    if (scratch.samplers.capacity() < num_textures)
        scratch.samplers.reserve(num_textures);
}
#endif

bool ComputePipeline::Configure(Tegra::Engines::KeplerCompute& kepler_compute,
                                Tegra::MemoryManager& gpu_memory, Scheduler& scheduler,
                                BufferCache& buffer_cache, TextureCache& texture_cache) {
    // A descriptor-free compute shader must not reserve the legacy 1024
    // guest entries or wait for a GPU worker while holding both cache locks.
    if (num_descriptor_entries == 0) {
        guest_descriptor_queue.AcquireKnownEmpty(uses_descriptor_buffer);
    } else {
        guest_descriptor_queue.Acquire(scheduler, num_descriptor_entries, uses_descriptor_buffer);
    }

    buffer_cache.SetComputeUniformBufferState(info.constant_buffer_mask, &uniform_buffer_sizes);
    buffer_cache.UnbindComputeStorageBuffers();
    size_t ssbo_index{};
    for (const auto& desc : info.storage_buffers_descriptors) {
        ASSERT(desc.count == 1);
        buffer_cache.BindComputeStorageBuffer(ssbo_index, desc.cbuf_index, desc.cbuf_offset,
                                              desc.is_written);
        ++ssbo_index;
    }

    // No TIC/TSC state is consumed by a shader that has no sampled,
    // storage or texel-buffer images. Defer table synchronization until a
    // later compute pipeline actually uses descriptor tables.
    if (!info.texture_buffer_descriptors.empty() ||
        !info.image_buffer_descriptors.empty() ||
        !info.texture_descriptors.empty() ||
        !info.image_descriptors.empty()) {
        texture_cache.SynchronizeDescriptors(true);
    }

#ifdef PS5_NATIVE
    // Preallocated by DispatchCompute before its paired cache mutexes.
    // A nested call leases another slot rather than overwriting these IDs.
    ComputeDescriptorScratchLease scratch;
    auto& views = scratch.Views();
    auto& samplers = scratch.Samplers();
    views.reserve(num_image_elements);
    samplers.reserve(num_textures);
#else
    boost::container::small_vector<VideoCommon::ImageViewInOut, 64> views;
    boost::container::small_vector<VideoCommon::SamplerId, 64> samplers;
#endif

    const auto& qmd{kepler_compute.launch_description};
    const auto& cbufs{qmd.const_buffer_config};
    const bool via_header_index{qmd.linked_tsc != 0};
#ifdef PS5_NATIVE
    // Descriptor synchronization above fixes the TSC table for this dispatch.
    // This bounded draw-local memo avoids duplicate sampler-table probes but
    // NEVER carries sampler IDs across dispatches, remaps or channel switches.
    struct ComputeSamplerMemo {
        u32 handle{};
        VideoCommon::SamplerId id{};
        bool valid{};
    };
    std::array<ComputeSamplerMemo, 16> sampler_memo{};
    const auto resolve_sampler{[&](u32 handle) -> VideoCommon::SamplerId {
        auto& entry = sampler_memo[handle & 15U];
        if (!entry.valid || entry.handle != handle) {
            entry.id = texture_cache.GetSamplerId(handle, true);
            entry.handle = handle;
            entry.valid = true;
        }
        return entry.id;
    }};
#endif
    const auto read_handle{[&](const auto& desc, u32 index) {
        ASSERT(((qmd.const_buffer_enable_mask >> desc.cbuf_index) & 1) != 0);
        const u32 index_offset{index << desc.size_shift};
        const u32 offset{desc.cbuf_offset + index_offset};
        const GPUVAddr addr{cbufs[desc.cbuf_index].Address() + offset};
        if constexpr (std::is_same_v<decltype(desc), const Shader::TextureDescriptor&> ||
                      std::is_same_v<decltype(desc), const Shader::TextureBufferDescriptor&>) {
            if (desc.has_secondary) {
                ASSERT(((qmd.const_buffer_enable_mask >> desc.secondary_cbuf_index) & 1) != 0);
                const u32 secondary_offset{desc.secondary_cbuf_offset + index_offset};
                const GPUVAddr separate_addr{cbufs[desc.secondary_cbuf_index].Address() +
                                             secondary_offset};
                const u32 lhs_raw{gpu_memory.Read<u32>(addr) << desc.shift_left};
                const u32 rhs_raw{gpu_memory.Read<u32>(separate_addr) << desc.secondary_shift_left};
                return TexturePair(lhs_raw | rhs_raw, via_header_index);
            }
        }
        return TexturePair(gpu_memory.Read<u32>(addr), via_header_index);
    }};
    const auto add_image{[&](const auto& desc, bool blacklist) {
        for (u32 index = 0; index < desc.count; ++index) {
            const auto handle{read_handle(desc, index)};
            views.push_back({
                .index = handle.first,
                .blacklist = blacklist,
                .id = {},
            });
        }
    }};
    for (const auto& desc : info.texture_buffer_descriptors) {
        add_image(desc, false);
    }
    for (const auto& desc : info.image_buffer_descriptors) {
        add_image(desc, false);
    }
    for (const auto& desc : info.texture_descriptors) {
        for (u32 index = 0; index < desc.count; ++index) {
            const auto handle{read_handle(desc, index)};
            views.push_back({handle.first});

#ifdef PS5_NATIVE
            samplers.push_back(resolve_sampler(handle.second));
#else
            VideoCommon::SamplerId sampler = texture_cache.GetSamplerId(handle.second, true);
            samplers.push_back(sampler);
#endif
        }
    }
    for (const auto& desc : info.image_descriptors) {
        add_image(desc, desc.is_written);
    }
    texture_cache.FillImageViews(std::span(views.data(), views.size()), true);

    buffer_cache.UnbindComputeTextureBuffers();
    size_t index{};
    const auto add_buffer{[&](const auto& desc) {
        constexpr bool is_image = std::is_same_v<decltype(desc), const ImageBufferDescriptor&>;
        for (u32 i = 0; i < desc.count; ++i) {
            bool is_written{false};
            if constexpr (is_image) {
                is_written = desc.is_written;
            }
            ImageView& image_view = texture_cache.GetImageView(views[index].id);
            PixelFormat format{image_view.format};
            if constexpr (is_image) {
                if (const auto explicit_format{PixelFormatFromImageFormat(desc.format)}) {
                    format = *explicit_format;
                }
            }
            buffer_cache.BindComputeTextureBuffer(index, image_view.GpuAddr(),
                                                  image_view.BufferSize(), format,
                                                  is_written, is_image);
            ++index;
        }
    }};
    std::ranges::for_each(info.texture_buffer_descriptors, add_buffer);
    std::ranges::for_each(info.image_buffer_descriptors, add_buffer);

    buffer_cache.UpdateComputeBuffers();
    buffer_cache.BindHostComputeBuffers();
    if (buffer_cache.any_buffer_uploaded) {
        buffer_cache.runtime.PostCopyBarrier();
        buffer_cache.any_buffer_uploaded = false;
    }

    RescalingPushConstant rescaling;
    const VideoCommon::SamplerId* samplers_it{samplers.data()};
    const VideoCommon::ImageViewInOut* views_it{views.data()};
    PushImageDescriptors(texture_cache, guest_descriptor_queue, info, rescaling, samplers_it,
                         views_it);

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
                ::Eden::Performance::CachePhase::ComputePipelineWait,
                ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
#ifndef PS5_NATIVE
            std::unique_lock lock{build_mutex};
#endif
            build_condvar.wait(lock, [this] { return is_built.load(std::memory_order::relaxed); });
#ifdef PS5_NATIVE
            lock.unlock();
            pipeline_ready_wait.Stop();
            timer.reset(); // Exclude diagnostic print I/O from VulkanTimer(16).
            keyed_wait.Complete("compute", diagnostic_cache_key_hash, IsBound());
#endif
        });
#ifdef PS5_NATIVE
        first_use_wait_recorded = true;
        }
#endif
    }

    // Log compute pipeline binding
    if (GPU::Logging::IsActive() &&
        Settings::values.gpu_log_vulkan_calls.GetValue()) {
        GPU::Logging::GPULogger::GetInstance().LogPipelineBind(true, "compute pipeline");
    }

    const DescriptorUpdateEntry* const descriptor_data{guest_descriptor_queue.UpdateData()};
    VkDeviceSize descriptor_buffer_offset{};
    u32 descriptor_buffer_chunk{};
    if (uses_descriptor_buffer) {
#ifdef PS5_NATIVE
        // Same payload and generation: keep the previously uploaded bytes,
        // extending the GPU ring tick without another allocation or upload.
        const bool reuse_allocation =
            last_descriptor_buffer_valid &&
            last_descriptor_buffer_generation == descriptor_buffer_ring.CurrentGeneration() &&
            last_descriptor_payload.size() == num_descriptor_entries &&
            (num_descriptor_entries == 0 ||
             std::memcmp(last_descriptor_payload.data(), descriptor_data,
                         num_descriptor_entries * sizeof(DescriptorUpdateEntry)) == 0);
        if (reuse_allocation) {
            descriptor_buffer_offset = last_descriptor_buffer_offset;
            descriptor_buffer_chunk = last_descriptor_buffer_chunk;
            descriptor_buffer_ring.TouchFrame(scheduler);
        } else
#endif
        {
            const DescriptorBufferRing::Allocation alloc{
                descriptor_buffer_ring.Allocate(scheduler, descriptor_buffer_layout.size)};
            if (!alloc.host) {
                LOG_DEBUG(Render_Vulkan, "Failed to reserve descriptor memory, skipping dispatch");
                return false;
            }
            WriteDescriptorBuffer(device, descriptor_buffer_layout, descriptor_data, alloc.host);
            descriptor_buffer_offset = alloc.offset;
            descriptor_buffer_chunk = alloc.chunk;
#ifdef PS5_NATIVE
            last_descriptor_buffer_offset = alloc.offset;
            last_descriptor_buffer_chunk = alloc.chunk;
            last_descriptor_buffer_generation = alloc.generation;
            if (num_descriptor_entries != 0) {
                last_descriptor_payload.assign(descriptor_data,
                                               descriptor_data + num_descriptor_entries);
            } else {
                last_descriptor_payload.clear();
            }
            last_descriptor_buffer_valid = true;
#endif
        }
    }

    const bool bind_descriptor_buffer{
        uses_descriptor_buffer && scheduler.UpdateDescriptorBufferChunk(descriptor_buffer_chunk)};

    const bool is_rescaling = !info.texture_descriptors.empty() || !info.image_descriptors.empty();
    scheduler.Record([this, descriptor_data, is_rescaling, descriptor_buffer_offset,
                      descriptor_buffer_chunk, bind_descriptor_buffer,
                      rescaling_data = rescaling.Data()](vk::CommandBuffer cmdbuf) {
        if (bind_descriptor_buffer) {
            const VkDescriptorBufferBindingInfoEXT binding_info{
                descriptor_buffer_ring.BindingInfo(descriptor_buffer_chunk)};
            cmdbuf.BindDescriptorBuffersEXT(binding_info);
        }
        if (!pipeline) {
            return;
        }
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);
        if (!descriptor_set_layout) {
            return;
        }
        if (is_rescaling) {
            cmdbuf.PushConstants(*pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                 RESCALING_LAYOUT_WORDS_OFFSET, sizeof(rescaling_data),
                                 rescaling_data.data());
        }
        if (uses_descriptor_buffer) {
            const u32 buffer_index{};
            cmdbuf.SetDescriptorBufferOffsetsEXT(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_layout,
                                                 0, buffer_index, descriptor_buffer_offset);
        } else if (uses_push_descriptor) {
            cmdbuf.PushDescriptorSetWithTemplateKHR(*descriptor_update_template, *pipeline_layout,
                                                    0, descriptor_data);
        } else {
            const VkDescriptorSet descriptor_set{descriptor_allocator.Commit()};
            const vk::Device& dev{device.GetLogical()};
            dev.UpdateDescriptorSet(descriptor_set, *descriptor_update_template, descriptor_data);
            cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_layout, 0,
                                      descriptor_set, nullptr);
        }
    });
    return true;
}

} // namespace Vulkan
