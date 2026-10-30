// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <type_traits>
#include <vector>

#include "common/thread_worker.h"
#include "video_core/renderer_vulkan/striped_wait.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
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

struct GraphicsPipelineCacheKey {
    std::array<u64, 6> unique_hashes;
    FixedPipelineState state;

    size_t Hash() const noexcept;

    bool operator==(const GraphicsPipelineCacheKey& rhs) const noexcept;

    bool operator!=(const GraphicsPipelineCacheKey& rhs) const noexcept {
        return !operator==(rhs);
    }

    size_t Size() const noexcept {
        return sizeof(unique_hashes) + state.Size();
    }
};
static_assert(std::has_unique_object_representations_v<GraphicsPipelineCacheKey>);
static_assert(std::is_trivially_copyable_v<GraphicsPipelineCacheKey>);
static_assert(std::is_trivially_constructible_v<GraphicsPipelineCacheKey>);

} // namespace Vulkan

namespace std {
template <>
struct hash<Vulkan::GraphicsPipelineCacheKey> {
    size_t operator()(const Vulkan::GraphicsPipelineCacheKey& k) const noexcept {
        return k.Hash();
    }
};
} // namespace std

namespace Vulkan {

class Device;
class PipelineStatistics;
class RenderPassCache;
class RescalingPushConstant;
class RenderAreaPushConstant;
class Scheduler;

class GraphicsPipeline {
    static constexpr size_t NUM_STAGES = Tegra::Engines::Maxwell3D::Regs::MaxShaderStage;

public:
    explicit GraphicsPipeline(
        Scheduler& scheduler, BufferCache& buffer_cache, TextureCache& texture_cache,
        vk::PipelineCache& pipeline_cache, VideoCore::ShaderNotify* shader_notify,
        const Device& device, DescriptorPool& descriptor_pool,
        GuestDescriptorQueue& guest_descriptor_queue,
        DescriptorBufferRing& descriptor_buffer_ring, Common::ThreadWorker* worker_thread,
        PipelineStatistics* pipeline_statistics, RenderPassCache& render_pass_cache,
        const GraphicsPipelineCacheKey& key, std::array<vk::ShaderModule, NUM_STAGES> stages,
        const std::array<const Shader::Info*, NUM_STAGES>& infos);

    bool HasDynamicVertexInput() const noexcept { return key.state.dynamic_vertex_input; }
#ifdef PS5_NATIVE
    [[nodiscard]] bool CanUseDescriptorBuffer() const noexcept {
        return uses_descriptor_buffer && descriptor_set_layout;
    }
    [[nodiscard]] bool HasDescriptorEntries() const noexcept {
        return num_descriptor_entries != 0;
    }
    [[nodiscard]] size_t DescriptorEntryReservation() const noexcept {
        return num_descriptor_entries != 0 ? num_descriptor_entries : 0x400;
    }
    [[nodiscard]] VkDeviceSize DescriptorRingPreflightSize() const noexcept {
        // Before reading guest descriptors, these are the only two conditions
        // sufficient to PROVE the old descriptor allocation cannot be reused.
        return CanUseDescriptorBuffer() &&
               (last_descriptor_buffer_generation != descriptor_buffer_ring.CurrentGeneration() ||
                last_descriptor_payload.size() != num_descriptor_entries)
                   ? descriptor_buffer_layout.size : 0;
    }
#endif
    bool SupportsAlphaToCoverage() const noexcept {
        return fragment_has_color0_output;
    }

    bool SupportsAlphaToOne() const noexcept {
        return fragment_has_color0_output;
    }

    bool UsesExtendedDynamicState() const noexcept {
        return key.state.extended_dynamic_state != 0;
    }
    GraphicsPipeline& operator=(GraphicsPipeline&&) noexcept = delete;
    GraphicsPipeline(GraphicsPipeline&&) noexcept = delete;

    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    ~GraphicsPipeline();

    void AddTransition(GraphicsPipeline* transition);
#ifdef PS5_NATIVE
    // Reserve graphics descriptor scratch before acquiring both guest caches.
    // No descriptor content is read outside the locks.
    void PreflightDescriptorScratch();
#endif

    bool Configure(bool is_indexed) {
        return configure_func(this, is_indexed);
    }

    [[nodiscard]] GraphicsPipeline* Next(const GraphicsPipelineCacheKey& current_key) noexcept {
        if (key == current_key) {
            return this;
        }
#ifdef PS5_NATIVE
        // Two frequent overlay/HUD edges avoid rescanning a large transition
        // graph on each alternating draw. Store indices, never vector pointers.
        if (transition_keys.size() >= 8) {
            const size_t hot = hot_transition_indices[0];
            if (hot < transition_keys.size() && transition_keys[hot] == current_key) {
                return transitions[hot];
            }
            const size_t other = hot_transition_indices[1];
            if (other != hot && other < transition_keys.size() &&
                transition_keys[other] == current_key) {
                std::swap(hot_transition_indices[0], hot_transition_indices[1]);
                return transitions[other];
            }
        }
#endif
        const auto it{std::find(transition_keys.begin(), transition_keys.end(), current_key)};
        if (it == transition_keys.end()) {
            return nullptr;
        }
        const size_t index{static_cast<size_t>(std::distance(transition_keys.begin(), it))};
#ifdef PS5_NATIVE
        hot_transition_indices[1] = hot_transition_indices[0];
        hot_transition_indices[0] = index;
#endif
        return transitions[index];
    }

    [[nodiscard]] bool IsBuilt() const noexcept {
        // Matches the release store in the builder's SCOPE_EXIT. Consumers
        // can only inspect the constructed VkPipeline after acquiring it.
        return is_built.load(std::memory_order::acquire);
    }

    // Queued worker checks pipeline readiness AFTER async build wait.
    [[nodiscard]] bool CanExecuteDraw() const noexcept {
        if (!is_built.load(std::memory_order::acquire)) return false;
#ifdef PS5_NATIVE
        // A fast-first driver build can fail without throwing while its
        // independently queued optimized variant succeeds. ConfigureDraw
        // already binds that published variant; do not discard the following
        // Draw/DrawIndirect simply because the initial handle is null.
        // The release/acquire flag publishes the optimized VkPipeline.
        return static_cast<bool>(pipeline) ||
               optimized_ready.load(std::memory_order_acquire);
#else
        return static_cast<bool>(pipeline);
#endif
    }
    // Native Vulkan worker must use the pipeline actually selected by its
    // preceding ConfigureDraw callback, not a variant published afterward.
    // Desktop keeps the original readiness check.
    [[nodiscard]] bool CanExecuteRecordedDraw() const noexcept {
#ifdef PS5_NATIVE
        return worker_draw_bind_valid;
#else
        return CanExecuteDraw();
#endif
    }

    template <typename Spec>
    static auto MakeConfigureSpecFunc() {
        return [](GraphicsPipeline* pl, bool is_indexed) { return pl->ConfigureImpl<Spec>(is_indexed); };
    }

    void SetEngine(Tegra::Engines::Maxwell3D* maxwell3d_, Tegra::MemoryManager* gpu_memory_) {
        maxwell3d = maxwell3d_;
        gpu_memory = gpu_memory_;
    }

private:
    template <typename Spec>
    bool ConfigureImpl(bool is_indexed);

    bool ConfigureDraw(const RescalingPushConstant& rescaling,
                       const RenderAreaPushConstant& render_are);

    void MakePipeline(VkRenderPass render_pass, VkPipelineCreateFlags extra_flags = 0,
                      vk::Pipeline* target = nullptr);
    void QueueOptimizedPipeline(VkRenderPass render_pass);

    void Validate();

    const GraphicsPipelineCacheKey key;
    Tegra::Engines::Maxwell3D* maxwell3d;
    Tegra::MemoryManager* gpu_memory;
    const Device& device;
    TextureCache& texture_cache;
    BufferCache& buffer_cache;
    vk::PipelineCache& pipeline_cache;
    Scheduler& scheduler;
    GuestDescriptorQueue& guest_descriptor_queue;
    DescriptorBufferRing& descriptor_buffer_ring;

    bool (*configure_func)(GraphicsPipeline*, bool){};

    std::vector<GraphicsPipelineCacheKey> transition_keys;
    std::vector<GraphicsPipeline*> transitions;
#ifdef PS5_NATIVE
    // GPU owner only. Two valid indices remain stable across vector growth.
    std::array<size_t, 2> hot_transition_indices{};
#endif

    std::array<vk::ShaderModule, NUM_STAGES> spv_modules;

    std::array<Shader::Info, NUM_STAGES> stage_infos;
    std::array<u32, 5> enabled_uniform_buffer_masks{};
    VideoCommon::UniformBufferSizes uniform_buffer_sizes{};
    u32 num_descriptor_entries{};
    size_t num_image_elements{};
    u32 num_textures{};
    // Immutable shader layout: no UBO/SSBO/TBO means no host buffer work.
    std::array<bool, NUM_STAGES> stage_has_host_buffer_bindings{};
    std::array<bool, NUM_STAGES> stage_has_image_bindings{};
    std::array<size_t, NUM_STAGES> stage_sampled_view_counts{};
    std::array<size_t, NUM_STAGES> stage_storage_view_counts{};
    bool fragment_has_color0_output{};

    vk::DescriptorSetLayout descriptor_set_layout;
    DescriptorAllocator descriptor_allocator;
    vk::PipelineLayout pipeline_layout;
    vk::DescriptorUpdateTemplate descriptor_update_template;
    vk::Pipeline pipeline;
    struct OptimizeState {
        std::mutex mutex;
        std::condition_variable done;
        bool cancelled{false};
        bool running{false};
    };
    std::shared_ptr<OptimizeState> optimize_state; // allocated ONLY for active R313 builds
    vk::Pipeline optimized_pipeline;
    std::atomic_bool optimized_ready{false};
#ifdef PS5_NATIVE
    // Set/read only on the ordered Vulkan command worker, immediately before
    // each draw callback. Never use the later optimized_ready publication as
    // proof that an earlier ConfigureDraw actually bound the recovered handle.
    bool worker_draw_bind_valid{};
#endif

    DescriptorBufferLayout descriptor_buffer_layout;
    std::vector<DescriptorUpdateEntry> last_descriptor_payload;
    VkDeviceSize last_descriptor_buffer_offset{};
    u32 last_descriptor_buffer_chunk{};
    u64 last_descriptor_buffer_generation{};

    ::Eden::StripedCondition build_condvar;
    ::Eden::StripedMutex build_mutex;
    std::atomic_bool is_built{false};
#ifdef PS5_NATIVE
    // Only the GPU owner writes this after enqueueing the ordered worker wait.
    bool first_use_wait_recorded{};
#endif
    bool uses_push_descriptor{false};
    bool uses_descriptor_buffer{false};
};

} // namespace Vulkan
