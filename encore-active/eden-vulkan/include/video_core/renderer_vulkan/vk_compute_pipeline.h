// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "common/common_types.h"
#include "common/thread_worker.h"
#include "video_core/renderer_vulkan/striped_wait.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/renderer_vulkan/pipeline_helper.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_descriptor_buffer.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace VideoCore {
class ShaderNotify;
}

namespace Vulkan {

class Device;
class PipelineStatistics;
class Scheduler;

class ComputePipeline {
public:
    explicit ComputePipeline(const Device& device, Scheduler& scheduler, vk::PipelineCache& pipeline_cache,
                             DescriptorPool& descriptor_pool,
                             GuestDescriptorQueue& guest_descriptor_queue,
                             DescriptorBufferRing& descriptor_buffer_ring,
                             Common::ThreadWorker* thread_worker,
                             PipelineStatistics* pipeline_statistics,
                             VideoCore::ShaderNotify* shader_notify, const Shader::Info& info,
                             vk::ShaderModule spv_module, u64 shader_hash);

    ComputePipeline& operator=(ComputePipeline&&) noexcept = delete;
    ComputePipeline(ComputePipeline&&) noexcept = delete;

    ComputePipeline& operator=(const ComputePipeline&) = delete;
    ComputePipeline(const ComputePipeline&) = delete;

    [[nodiscard]] bool Configure(Tegra::Engines::KeplerCompute& kepler_compute,
                                 Tegra::MemoryManager& gpu_memory, Scheduler& scheduler,
                                 BufferCache& buffer_cache, TextureCache& texture_cache);

#ifdef PS5_NATIVE
    // Called on the GPU owner before the paired buffer/texture cache locks.
    void PreflightDescriptorScratch();
    [[nodiscard]] bool HasDescriptorEntries() const noexcept {
        return num_descriptor_entries != 0;
    }
    [[nodiscard]] bool CanUseDescriptorBuffer() const noexcept {
        return uses_descriptor_buffer;
    }
    [[nodiscard]] size_t DescriptorEntryReservation() const noexcept {
        return num_descriptor_entries != 0 ? num_descriptor_entries : 0x400;
    }
    [[nodiscard]] VkDeviceSize DescriptorRingPreflightSize() const noexcept {
        // Unlike graphics, every descriptor-buffer compute dispatch allocates.
        return uses_descriptor_buffer ? descriptor_buffer_layout.size : 0;
    }
#endif
    bool IsBound() const noexcept {
        return static_cast<bool>(pipeline);
    }

private:
    const Device& device;
    vk::PipelineCache& pipeline_cache;
    GuestDescriptorQueue& guest_descriptor_queue;
    DescriptorBufferRing& descriptor_buffer_ring;
    Shader::Info info;
    u64 shader_hash{};
    u32 num_descriptor_entries{};
#ifdef PS5_NATIVE
    size_t num_image_elements{};
    size_t num_textures{};
#endif

    VideoCommon::ComputeUniformBufferSizes uniform_buffer_sizes{};

    vk::ShaderModule spv_module;
    vk::DescriptorSetLayout descriptor_set_layout;
    bool uses_push_descriptor{false};
    bool uses_descriptor_buffer{false};
    DescriptorBufferLayout descriptor_buffer_layout;
    DescriptorAllocator descriptor_allocator;
    vk::PipelineLayout pipeline_layout;
    vk::DescriptorUpdateTemplate descriptor_update_template;
    vk::Pipeline pipeline;

    ::Eden::StripedCondition build_condvar;
    ::Eden::StripedMutex build_mutex;
    std::atomic_bool is_built{false};
};

} // namespace Vulkan
