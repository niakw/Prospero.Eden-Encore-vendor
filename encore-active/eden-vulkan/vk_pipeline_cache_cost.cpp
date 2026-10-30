// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdio>
#include "pipeline_trace_registry.h"
#include "performance.h"
#include "dev_vulkan.h"
#include "diagnostics.h"
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stop_token>
#include <thread>
#ifdef PS5_NATIVE
#include <pthread.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <cstdio>
#include "performance.h"
#endif

#include <vector>
#include <bit>
#include <numeric>
#include "common/cityhash.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/thread_worker.h"
#include "core/core.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/program_header.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/dirty_flags.h"
#include <atomic>
#include <mutex>
#include <set>
#include <unordered_set>
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/pipeline_helper.h"
#include "video_core/renderer_vulkan/pipeline_statistics.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/shader_cache.h"
#include "video_core/shader_environment.h"
#include "video_core/shader_notify.h"
#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/gpu_logging/gpu_logging.h"

#ifdef __ANDROID__
#include "../../android/app/src/main/jni/android_settings.h"
#endif

namespace Vulkan {
#ifdef PS5_NATIVE
// The compute pipeline builder exposes this Vulkan-namespace atomic counter.
extern std::atomic<unsigned> pending_first_use_compute_builds;
extern std::atomic<unsigned> pending_first_use_graphics_builds;
#endif

namespace {
#ifdef PS5_NATIVE
// Detailed-only, per-cache-key CPU translation/admission cost. This NEVER
// waits for the Vulkan driver worker and does not run on cache-hit draws.
// The timer ends before emitting its diagnostic, avoiding log-I/O inflation.
struct ColdShaderMissTrace {
    const char* kind;
    u64 key_hash;
    bool retry;
    bool enabled;
    ::Eden::Performance::ShaderMissStageCosts stages{};
    ::Eden::Performance::ShaderMissStageCosts* previous{};
    std::chrono::steady_clock::time_point started{};

    ColdShaderMissTrace(const char* kind_, u64 key_hash_, bool retry_)
        : kind(kind_), key_hash(key_hash_), retry(retry_),
          enabled(::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)),
          previous(enabled ? ::Eden::Performance::shader_miss_stage_active : nullptr),
          started(enabled ? std::chrono::steady_clock::now()
                          : std::chrono::steady_clock::time_point{}) {
        if (enabled) ::Eden::Performance::shader_miss_stage_active = &stages;
    }
    ColdShaderMissTrace(const ColdShaderMissTrace&) = delete;
    ColdShaderMissTrace& operator=(const ColdShaderMissTrace&) = delete;
    ~ColdShaderMissTrace() {
        if (enabled && ::Eden::Performance::shader_miss_stage_active == &stages)
            ::Eden::Performance::shader_miss_stage_active = previous;
    }
    void Complete(bool success) {
        if (!enabled) return;
        // Stop attributing before print I/O or subsequent cache work.
        if (::Eden::Performance::shader_miss_stage_active == &stages)
            ::Eden::Performance::shader_miss_stage_active = previous;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        std::printf("EDEN_VULKAN_SHADER_MISS kind=%s key=%016llx retry=%u "
                    "success=%u cpu_ns=%llu env_ns=%llu pool_ns=%llu "
                    "cfg_ns=%llu ir_ns=%llu spirv_ns=%llu module_ns=%llu\n", kind,
                    static_cast<unsigned long long>(key_hash), unsigned(retry),
                    unsigned(success), static_cast<unsigned long long>(ns),
                    stages.env_ns, stages.pool_ns, stages.cfg_ns, stages.ir_ns,
                    stages.spirv_ns, stages.module_ns);
    }
};
#endif
#ifdef PS5_NATIVE
// Keep the OPTIONAL graphics optimizer away from foreground compute
// CFG/IR/SPIR-V translation, before the Vulkan driver worker is enqueued.
// Only runtime first-use translation participates; preloaded cache jobs do not.
struct ComputeTranslationPriorityScope {
    explicit ComputeTranslationPriorityScope(bool foreground_) : foreground{foreground_} {
        if (foreground) {
            pending_first_use_compute_builds.fetch_add(1, std::memory_order_release);
        }
    }
    ~ComputeTranslationPriorityScope() {
        if (foreground) {
            pending_first_use_compute_builds.fetch_sub(1, std::memory_order_release);
        }
    }
    bool foreground;
};
// This foreground reservation is published BEFORE the renderer translates
// cold graphics CFG/IR/SPIR-V and constructs shader modules. The normal
// graphics builder owns a second reservation from QueueWork to completion.
struct GraphicsTranslationPriorityScope {
    explicit GraphicsTranslationPriorityScope(bool foreground_) : foreground{foreground_} {
        if (foreground) {
            pending_first_use_graphics_builds.fetch_add(1, std::memory_order_release);
        }
    }
    ~GraphicsTranslationPriorityScope() {
        if (foreground) {
            pending_first_use_graphics_builds.fetch_sub(1, std::memory_order_release);
        }
    }
    GraphicsTranslationPriorityScope(const GraphicsTranslationPriorityScope&) = delete;
    GraphicsTranslationPriorityScope& operator=(const GraphicsTranslationPriorityScope&) = delete;
    bool foreground;
};
#endif
using Shader::Backend::SPIRV::EmitSPIRV;
using Shader::Maxwell::ConvertLegacyToGeneric;
using Shader::Maxwell::GenerateGeometryPassthrough;
using Shader::Maxwell::MergeDualVertexPrograms;
using Shader::Maxwell::TranslateProgram;
using VideoCommon::ComputeEnvironment;
using VideoCommon::FileEnvironment;
using VideoCommon::GenericEnvironment;
using VideoCommon::GraphicsEnvironment;

constexpr u32 CACHE_VERSION = 18;
constexpr size_t VULKAN_CACHE_FLUSH_PIPELINES = 128;
constexpr size_t VULKAN_CACHE_FLUSH_MIN_SECONDS = 30;
constexpr std::array<char, 8> VULKAN_CACHE_MAGIC_NUMBER{'y', 'u', 'z', 'u', 'v', 'k', 'c', 'h'};

template <typename Container>
auto MakeSpan(Container& container) {
    return std::span(container.data(), container.size());
}

Shader::OutputTopology MaxwellToOutputTopology(Maxwell::PrimitiveTopology topology) {
    switch (topology) {
    case Maxwell::PrimitiveTopology::Points:
        return Shader::OutputTopology::PointList;
    case Maxwell::PrimitiveTopology::LineStrip:
        return Shader::OutputTopology::LineStrip;
    default:
        return Shader::OutputTopology::TriangleStrip;
    }
}

Shader::CompareFunction MaxwellToCompareFunction(Maxwell::ComparisonOp comparison) {
    switch (comparison) {
    case Maxwell::ComparisonOp::Never_D3D:
    case Maxwell::ComparisonOp::Never_GL:
        return Shader::CompareFunction::Never;
    case Maxwell::ComparisonOp::Less_D3D:
    case Maxwell::ComparisonOp::Less_GL:
        return Shader::CompareFunction::Less;
    case Maxwell::ComparisonOp::Equal_D3D:
    case Maxwell::ComparisonOp::Equal_GL:
        return Shader::CompareFunction::Equal;
    case Maxwell::ComparisonOp::LessEqual_D3D:
    case Maxwell::ComparisonOp::LessEqual_GL:
        return Shader::CompareFunction::LessThanEqual;
    case Maxwell::ComparisonOp::Greater_D3D:
    case Maxwell::ComparisonOp::Greater_GL:
        return Shader::CompareFunction::Greater;
    case Maxwell::ComparisonOp::NotEqual_D3D:
    case Maxwell::ComparisonOp::NotEqual_GL:
        return Shader::CompareFunction::NotEqual;
    case Maxwell::ComparisonOp::GreaterEqual_D3D:
    case Maxwell::ComparisonOp::GreaterEqual_GL:
        return Shader::CompareFunction::GreaterThanEqual;
    case Maxwell::ComparisonOp::Always_D3D:
    case Maxwell::ComparisonOp::Always_GL:
        return Shader::CompareFunction::Always;
    }
    UNIMPLEMENTED_MSG("Unimplemented comparison op={}", comparison);
    return {};
}

Shader::AttributeType CastAttributeType(const FixedPipelineState::VertexAttribute& attr) {
    if (attr.enabled == 0) {
        return Shader::AttributeType::Disabled;
    }
    switch (attr.Type()) {
    case Maxwell::VertexAttribute::Type::UnusedEnumDoNotUseBecauseItWillGoAway:
        ASSERT_MSG(false, "Invalid vertex attribute type!");
        return Shader::AttributeType::Disabled;
    case Maxwell::VertexAttribute::Type::SNorm:
    case Maxwell::VertexAttribute::Type::UNorm:
    case Maxwell::VertexAttribute::Type::Float:
        return Shader::AttributeType::Float;
    case Maxwell::VertexAttribute::Type::SInt:
        return Shader::AttributeType::SignedInt;
    case Maxwell::VertexAttribute::Type::UInt:
        return Shader::AttributeType::UnsignedInt;
    case Maxwell::VertexAttribute::Type::UScaled:
        return Shader::AttributeType::UnsignedScaled;
    case Maxwell::VertexAttribute::Type::SScaled:
        return Shader::AttributeType::SignedScaled;
    }
    return Shader::AttributeType::Float;
}

Shader::AttributeType AttributeType(const FixedPipelineState& state, size_t index) {
    switch (state.DynamicAttributeType(index)) {
    case 0:
        return Shader::AttributeType::Disabled;
    case 1:
        return Shader::AttributeType::Float;
    case 2:
        return Shader::AttributeType::SignedInt;
    case 3:
        return Shader::AttributeType::UnsignedInt;
    }
    return Shader::AttributeType::Disabled;
}

Shader::RuntimeInfo MakeRuntimeInfo(std::span<const Shader::IR::Program> programs,
                                    const GraphicsPipelineCacheKey& key,
                                    const Shader::IR::Program& program,
                                    const Shader::IR::Program* previous_program,
                                    const Vulkan::Device& device) {
    Shader::RuntimeInfo info;
    if (previous_program) {
        info.previous_stage_stores = previous_program->info.stores;
        info.previous_stage_legacy_stores_mapping = previous_program->info.legacy_stores_mapping;
        if (previous_program->is_geometry_passthrough) {
            info.previous_stage_stores.mask |= previous_program->info.passthrough.mask;
        }
    } else {
        info.previous_stage_stores.mask.set();
    }
    const Shader::Stage stage{program.stage};
    const bool has_geometry{key.unique_hashes[4] != 0 && !programs[4].is_geometry_passthrough};
    const bool gl_ndc{key.state.ndc_minus_one_to_one != 0};
    const float point_size{std::bit_cast<float>(key.state.point_size)};
    switch (stage) {
    case Shader::Stage::VertexB:
        if (!has_geometry) {
            if (key.state.topology == Maxwell::PrimitiveTopology::Points) {
                info.fixed_state_point_size = point_size;
            }
            if (key.state.xfb_enabled) {
                if (device.IsExtTransformFeedbackSupported()) {
                    auto [varyings, count] =
                        VideoCommon::MakeTransformFeedbackVaryings(key.state.xfb_state);
                    info.xfb_varyings = varyings;
                    info.xfb_count = count;
                } else {
                    LOG_WARNING(Render_Vulkan, "XFB requested in pipeline key but device lacks VK_EXT_transform_feedback; ignoring XFB decorations");
                }
            }
            info.convert_depth_mode = gl_ndc;
        }
        if (key.state.dynamic_vertex_input) {
            for (size_t index = 0; index < Maxwell::NumVertexAttributes; ++index) {
                info.generic_input_types[index] = AttributeType(key.state, index);
            }
        } else {
            std::ranges::transform(key.state.attributes, info.generic_input_types.begin(),
                                   &CastAttributeType);
        }
        break;
    case Shader::Stage::TessellationEval:
        info.tess_clockwise = key.state.tessellation_clockwise != 0;
        info.tess_primitive = [&key] {
            const u32 raw{key.state.tessellation_primitive.Value()};
            switch (static_cast<Maxwell::Tessellation::DomainType>(raw)) {
            case Maxwell::Tessellation::DomainType::Isolines:
                return Shader::TessPrimitive::Isolines;
            case Maxwell::Tessellation::DomainType::Triangles:
                return Shader::TessPrimitive::Triangles;
            case Maxwell::Tessellation::DomainType::Quads:
                return Shader::TessPrimitive::Quads;
            }
            ASSERT(false);
            return Shader::TessPrimitive::Triangles;
        }();
        info.tess_spacing = [&] {
            const u32 raw{key.state.tessellation_spacing};
            switch (static_cast<Maxwell::Tessellation::Spacing>(raw)) {
            case Maxwell::Tessellation::Spacing::Integer:
                return Shader::TessSpacing::Equal;
            case Maxwell::Tessellation::Spacing::FractionalOdd:
                return Shader::TessSpacing::FractionalOdd;
            case Maxwell::Tessellation::Spacing::FractionalEven:
                return Shader::TessSpacing::FractionalEven;
            }
            ASSERT(false);
            return Shader::TessSpacing::Equal;
        }();
        break;
    case Shader::Stage::Geometry:
        if (program.output_topology == Shader::OutputTopology::PointList) {
            info.fixed_state_point_size = point_size;
        }
        if (key.state.xfb_enabled != 0) {
            if (device.IsExtTransformFeedbackSupported()) {
                auto [varyings, count] =
                    VideoCommon::MakeTransformFeedbackVaryings(key.state.xfb_state);
                info.xfb_varyings = varyings;
                info.xfb_count = count;
            } else {
                LOG_WARNING(Render_Vulkan, "XFB requested in pipeline key but device lacks VK_EXT_transform_feedback; ignoring XFB decorations");
            }
        }
        info.convert_depth_mode = gl_ndc;
        break;
    case Shader::Stage::Fragment: {
        info.alpha_test_func = MaxwellToCompareFunction(
            key.state.UnpackComparisonOp(key.state.alpha_test_func.Value()));
        info.alpha_test_reference = std::bit_cast<float>(key.state.alpha_test_ref);

        info.dual_source_blend = key.state.attachment0_dual_source_blend != 0;

        if (device.IsMoltenVK()) {
            for (size_t i = 0; i < 8; ++i) {
                const auto format = static_cast<Tegra::RenderTargetFormat>(key.state.color_formats[i]);
                const auto pixel_format = VideoCore::Surface::PixelFormatFromRenderTargetFormat(format);
                if (VideoCore::Surface::IsPixelFormatInteger(pixel_format)) {
                    if (VideoCore::Surface::IsPixelFormatSignedInteger(pixel_format)) {
                        info.color_output_types[i] = Shader::AttributeType::SignedInt;
                    } else {
                        info.color_output_types[i] = Shader::AttributeType::UnsignedInt;
                    }
                } else {
                    info.color_output_types[i] = Shader::AttributeType::Float;
                }
            }
        }
        break;
    }
    default:
        break;
    }
    switch (key.state.topology) {
    case Maxwell::PrimitiveTopology::Points:
        info.input_topology = Shader::InputTopology::Points;
        break;
    case Maxwell::PrimitiveTopology::Lines:
    case Maxwell::PrimitiveTopology::LineLoop:
    case Maxwell::PrimitiveTopology::LineStrip:
        info.input_topology = Shader::InputTopology::Lines;
        break;
    case Maxwell::PrimitiveTopology::Triangles:
    case Maxwell::PrimitiveTopology::TriangleStrip:
    case Maxwell::PrimitiveTopology::TriangleFan:
    case Maxwell::PrimitiveTopology::Quads:
    case Maxwell::PrimitiveTopology::QuadStrip:
    case Maxwell::PrimitiveTopology::Polygon:
    case Maxwell::PrimitiveTopology::Patches:
        info.input_topology = Shader::InputTopology::Triangles;
        break;
    case Maxwell::PrimitiveTopology::LinesAdjacency:
    case Maxwell::PrimitiveTopology::LineStripAdjacency:
        info.input_topology = Shader::InputTopology::LinesAdjacency;
        break;
    case Maxwell::PrimitiveTopology::TrianglesAdjacency:
    case Maxwell::PrimitiveTopology::TriangleStripAdjacency:
        info.input_topology = Shader::InputTopology::TrianglesAdjacency;
        break;
    }
    info.force_early_z = key.state.early_z != 0;
    info.y_negate = key.state.y_negate != 0;
    return info;
}

size_t GetTotalPipelineWorkers() {
    const size_t max_core_threads =
        std::max<size_t>(static_cast<size_t>(std::thread::hardware_concurrency()), 2ULL) - 1ULL;
#ifdef __ANDROID__
    const int configured = AndroidSettings::values.pipeline_worker_count.GetValue();
    const int clamped = std::clamp(configured, 2, 8);
    const size_t desired = static_cast<size_t>(clamped);
    if (desired == 0) {
        return 1ULL;
    }
    return std::min(max_core_threads, desired);
#else
#ifdef PS5_NATIVE
    const size_t reported = std::max<size_t>(static_cast<size_t>(std::thread::hardware_concurrency()), 2ULL);
    cpuset_t allowed{};
    const int affinity_rc = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, 8, &allowed);
    size_t available = 0;
    std::uint64_t allowed_mask = 0;
    if (affinity_rc == 0) {
        for (unsigned cpu = 0; cpu < 64; ++cpu) {
            if (CPU_ISSET(cpu, &allowed)) {
                ++available;
                allowed_mask |= std::uint64_t{1} << cpu;
            }
        }
    }
    // Reserve only what is REALLY shared with guest/GPU workers in the
    // CURRENT thread's allowed mask. The masked-off primaries have already
    // been excluded by SetSecondaryPlacement: do NOT subtract them again.
    // The kernel's confirmed affinity is authoritative. C++ hardware_concurrency()
    // is only a hint: on constrained firmware it can under-report the CPUs
    // actually granted to this thread and silently starve shader compilers.
    // Never borrow CPUs outside the read-back allowed mask.
    const size_t schedulable = available ? available : std::min<size_t>(reported, 4);
    const std::uint64_t primary_mask = ::Eden::Performance::PinnedWorkerMask();
    size_t primary_in_mask = 0;
    if (affinity_rc == 0 && primary_mask) {
        for (unsigned cpu = 0; cpu < 64; ++cpu)
            if (CPU_ISSET(cpu, &allowed) && (primary_mask & (std::uint64_t{1} << cpu)))
                ++primary_in_mask;
    }
    // A logical-only dev A/B trial may have already pinned the SHADER
    // parent to a kernel-verified secondary mask. It is then wrong to
    // subtract the seven primary CPU/GPU services AGAIN. Do not equate
    // this proof of LOGICAL nonoverlap with physical/SMT core identity.
    const std::uint64_t secondary_mask = ::Eden::Performance::VerifiedSecondaryPlacementMask();
    const bool secondary_isolated = affinity_rc == 0 && allowed_mask != 0 &&
        secondary_mask != 0 && (allowed_mask & ~secondary_mask) == 0;
    constexpr size_t secondary_headroom = 2;
    constexpr size_t unverified_reserved = 7;
    constexpr size_t max_pipeline_workers = 6;
    const size_t reserved = affinity_rc == 0 && primary_mask ?
        primary_in_mask + secondary_headroom :
        secondary_isolated ? secondary_headroom : unverified_reserved;
    const size_t spare = schedulable > reserved ? schedulable - reserved : 0ULL;
    const size_t selected = std::max<size_t>(1ULL, std::min(spare, max_pipeline_workers));
    std::printf("EDEN_PS5_SHADER_WORKERS reported=%zu available=%zu workers=%zu "
                "reserved=%zu primary_in_mask=%zu physical_verified=%u logical_isolated=%u affinity_rc=%d "
                "allowed_mask=0x%llx primary_mask=0x%llx secondary_mask=0x%llx\n",
                reported, available, selected, reserved, primary_in_mask,
                unsigned(primary_mask != 0), unsigned(secondary_isolated), affinity_rc,
                static_cast<unsigned long long>(allowed_mask),
                static_cast<unsigned long long>(primary_mask),
                static_cast<unsigned long long>(secondary_mask));
    return selected;
#else
    return max_core_threads;
#endif
#endif
}

} // Anonymous namespace

size_t ComputePipelineCacheKey::Hash() const noexcept {
    const u64 hash = Common::CityHash64(reinterpret_cast<const char*>(this), sizeof *this);
    return static_cast<size_t>(hash);
}

bool ComputePipelineCacheKey::operator==(const ComputePipelineCacheKey& rhs) const noexcept {
    return std::memcmp(&rhs, this, sizeof *this) == 0;
}

size_t GraphicsPipelineCacheKey::Hash() const noexcept {
    const u64 hash = Common::CityHash64(reinterpret_cast<const char*>(this), Size());
    return static_cast<size_t>(hash);
}

bool GraphicsPipelineCacheKey::operator==(const GraphicsPipelineCacheKey& rhs) const noexcept {
    return std::memcmp(&rhs, this, Size()) == 0;
}

PipelineCache::PipelineCache(Tegra::MaxwellDeviceMemoryManager& device_memory_,
                             const Device& device_, Scheduler& scheduler_,
                             DescriptorPool& descriptor_pool_,
                             GuestDescriptorQueue& guest_descriptor_queue_,
                             DescriptorBufferRing& descriptor_buffer_ring_,
                             RenderPassCache& render_pass_cache_, BufferCache& buffer_cache_,
                             TextureCache& texture_cache_, VideoCore::ShaderNotify& shader_notify_)
    : VideoCommon::ShaderCache{device_memory_}, device{device_}, scheduler{scheduler_},
      descriptor_pool{descriptor_pool_}, guest_descriptor_queue{guest_descriptor_queue_},
      descriptor_buffer_ring{descriptor_buffer_ring_},
      render_pass_cache{render_pass_cache_}, buffer_cache{buffer_cache_},
      texture_cache{texture_cache_}, shader_notify{shader_notify_},
      use_asynchronous_shaders{Settings::values.use_asynchronous_shaders.GetValue()},
      use_vulkan_pipeline_cache{Settings::values.use_vulkan_driver_pipeline_cache.GetValue()},
      workers(device.HasBrokenParallelShaderCompiling() ? 1ULL : GetTotalPipelineWorkers(),
              "VkPipelineBuilder", {}, Common::ThreadPlacement::Background),
      serialization_thread(1, "VkPipelineSerialization", {},
                           Common::ThreadPlacement::Background) {
    const auto& float_control{device.FloatControlProperties()};
    const VkDriverId driver_id{device.GetDriverID()};
    const VkShaderStageFlags subgroup_stages{device.GetSubgroupSupportedStages()};
    const auto subgroup_stage_bit{[subgroup_stages](VkShaderStageFlags flag, Shader::Stage stage) {
        return (subgroup_stages & flag) != 0 ? (1u << static_cast<u32>(stage)) : 0u;
    }};
    const u32 supported_subgroup_stages{
        subgroup_stage_bit(VK_SHADER_STAGE_VERTEX_BIT, Shader::Stage::VertexA) |
        subgroup_stage_bit(VK_SHADER_STAGE_VERTEX_BIT, Shader::Stage::VertexB) |
        subgroup_stage_bit(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                           Shader::Stage::TessellationControl) |
        subgroup_stage_bit(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                           Shader::Stage::TessellationEval) |
        subgroup_stage_bit(VK_SHADER_STAGE_GEOMETRY_BIT, Shader::Stage::Geometry) |
        subgroup_stage_bit(VK_SHADER_STAGE_FRAGMENT_BIT, Shader::Stage::Fragment) |
        subgroup_stage_bit(VK_SHADER_STAGE_COMPUTE_BIT, Shader::Stage::Compute)};
    profile = Shader::Profile{
        .supported_spirv = device.SupportedSpirvVersion(),
        .unified_descriptor_binding = true,
        .support_descriptor_aliasing = device.IsDescriptorAliasingSupported(),
        .support_int8 = device.IsInt8Supported(),
        .support_uniform_and_storage_buffer_8bit =
            device.IsUniformAndStorageBuffer8BitAccessSupported(),
        .support_storage_buffer_8bit = device.IsStorageBuffer8BitAccessSupported(),
        .support_int16 = device.IsShaderInt16Supported(),
        .support_uniform_and_storage_buffer_16bit =
            device.IsUniformAndStorageBuffer16BitAccessSupported(),
        .support_storage_buffer_16bit = device.IsStorageBuffer16BitAccessSupported(),
        .support_int64 = device.IsShaderInt64Supported(),
        .support_vertex_instance_id = false,
        .support_float_controls = device.IsKhrShaderFloatControlsSupported(),
        .support_separate_denorm_behavior =
            float_control.denormBehaviorIndependence == VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
        .support_separate_rounding_mode =
            float_control.roundingModeIndependence == VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
        .support_fp16_denorm_preserve = float_control.shaderDenormPreserveFloat16 != VK_FALSE,
        .support_fp32_denorm_preserve = float_control.shaderDenormPreserveFloat32 != VK_FALSE,
        .support_fp16_denorm_flush = float_control.shaderDenormFlushToZeroFloat16 != VK_FALSE,
        .support_fp32_denorm_flush = float_control.shaderDenormFlushToZeroFloat32 != VK_FALSE,
        .support_fp16_signed_zero_nan_preserve =
            float_control.shaderSignedZeroInfNanPreserveFloat16 != VK_FALSE,
        .support_fp32_signed_zero_nan_preserve =
            float_control.shaderSignedZeroInfNanPreserveFloat32 != VK_FALSE,
        .support_fp64_signed_zero_nan_preserve =
            float_control.shaderSignedZeroInfNanPreserveFloat64 != VK_FALSE,
        .support_explicit_workgroup_layout = device.IsKhrWorkgroupMemoryExplicitLayoutSupported(),
        .support_workgroup_layout_8bit_access =
            device.IsWorkgroupMemoryExplicitLayout8BitAccessSupported(),
        .support_workgroup_layout_16bit_access =
            device.IsWorkgroupMemoryExplicitLayout16BitAccessSupported(),
        .support_shader_quad_control = device.IsKhrShaderQuadControlSupported(),
        .support_quad_shuffles = device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_QUAD_BIT),
        .support_vote = device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_VOTE_BIT),
        .supported_subgroup_stages = supported_subgroup_stages,
        .support_viewport_index_layer_non_geometry =
            device.IsExtShaderViewportIndexLayerSupported(),
        .support_viewport_mask = device.IsNvViewportArray2Supported(),
        .support_typeless_image_loads = device.IsFormatlessImageLoadSupported(),
        .support_demote_to_helper_invocation =
            device.IsExtShaderDemoteToHelperInvocationSupported(),
        .support_int64_atomics = device.IsExtShaderAtomicInt64Supported(),
        .support_shared_int64_atomics = device.IsSharedInt64AtomicsSupported(),
        .support_derivative_control = true,
        .support_geometry_shader_passthrough = device.IsNvGeometryShaderPassthroughSupported(),
        .support_native_ndc = device.IsExtDepthClipControlSupported(),
        .support_scaled_attributes = !device.MustEmulateScaledFormats(),
        .support_multi_viewport = device.SupportsMultiViewport(),
        .support_geometry_streams = device.AreTransformFeedbackGeometryStreamsSupported(),
        .support_sampled_image_array_nonuniform_indexing =
            device.IsSampledImageArrayNonUniformIndexingSupported(),
        .support_storage_image_array_nonuniform_indexing =
            device.IsStorageImageArrayNonUniformIndexingSupported(),
        .support_uniform_texel_buffer_array_nonuniform_indexing =
            device.IsUniformTexelBufferArrayNonUniformIndexingSupported(),
        .support_storage_texel_buffer_array_nonuniform_indexing =
            device.IsStorageTexelBufferArrayNonUniformIndexingSupported(),

        .warp_size_potentially_larger_than_guest = device.IsWarpSizePotentiallyBiggerThanGuest(),

        .lower_left_origin_mode = false,
        .need_declared_frag_colors = false,
        .need_gather_subpixel_offset = driver_id == VK_DRIVER_ID_AMD_PROPRIETARY ||
                                       driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
                                       driver_id == VK_DRIVER_ID_MESA_RADV ||
                                       driver_id == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS ||
                                       driver_id == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA,

        .has_broken_spirv_clamp = driver_id == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS,
        .has_broken_spirv_position_input = driver_id == false,
        .has_broken_unsigned_image_offsets = false,
        .has_broken_signed_operations = false,
        .has_broken_fp16_float_controls = driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY,
        .has_broken_fp32_denorm_flush = driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY,
        .ignore_nan_fp_comparisons = false,
        .has_broken_spirv_subgroup_mask_vector_extract_dynamic = false,
        .has_broken_robust =
            device.IsNvidia() && device.GetNvidiaArch() <= NvidiaArchitecture::Arch_Pascal,
        .min_ssbo_alignment = device.GetStorageBufferAlignment(),
        .max_user_clip_distances = device.GetMaxUserClipDistances()
    };

    host_info = Shader::HostTranslateInfo{
        .min_ssbo_alignment = device.GetStorageBufferAlignment(),
        .max_per_stage_descriptor_sampled_images = device.GetMaxPerStageDescriptorSampledImages(),
        .max_per_stage_resources = device.GetMaxPerStageResources(),
        .max_descriptor_set_samplers = device.GetMaxDescriptorSetSamplers(),
        .max_descriptor_set_uniform_buffers = device.GetMaxDescriptorSetUniformBuffers(),
        .max_descriptor_set_uniform_buffers_dynamic = device.GetMaxDescriptorSetUniformBuffersDynamic(),
        .max_descriptor_set_storage_buffers = device.GetMaxDescriptorSetStorageBuffers(),
        .max_descriptor_set_storage_buffers_dynamic = device.GetMaxDescriptorSetStorageBuffersDynamic(),
        .max_descriptor_set_sampled_images = device.GetMaxDescriptorSetSampledImages(),
        .max_descriptor_set_storage_images = device.GetMaxDescriptorSetStorageImages(),
        .max_descriptor_set_input_attachements = device.GetMaxDescriptorSetInputAttachments(),
        .support_float64 = device.IsFloat64Supported(),
        .support_float16 = device.IsFloat16Supported(),
        .support_int64 = device.IsShaderInt64Supported(),
        .needs_demote_reorder = driver_id == VK_DRIVER_ID_AMD_PROPRIETARY ||
                                driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
                                driver_id == VK_DRIVER_ID_SAMSUNG_PROPRIETARY,
        .support_snorm_render_buffer = true,
        .support_viewport_index_layer = device.IsExtShaderViewportIndexLayerSupported(),
        .support_geometry_shader_passthrough = device.IsNvGeometryShaderPassthroughSupported(),
        .support_conditional_barrier = device.SupportsConditionalBarriers(),
    };
    host_info.ApplyDescriptorLimitPolicy();

    if (device.GetMaxVertexInputAttributes() < Maxwell::NumVertexAttributes) {
        LOG_WARNING(Render_Vulkan, "maxVertexInputAttributes is too low: {} < {}",
                    device.GetMaxVertexInputAttributes(), Maxwell::NumVertexAttributes);
    }
    if (device.GetMaxVertexInputBindings() < Maxwell::NumVertexArrays) {
        LOG_WARNING(Render_Vulkan, "maxVertexInputBindings is too low: {} < {}",
                    device.GetMaxVertexInputBindings(), Maxwell::NumVertexArrays);
    }

    LOG_INFO(Render_Vulkan, "DynamicState setting value: {}", u32(Settings::values.dyna_state.GetValue()));

    dynamic_features = {};
    dynamic_features.driver_id = device.GetDriverID();
    dynamic_features.driver_version = device.GetDriverVersion();

    // User granularity enforced in vulkan_device.cpp switch statement:
    //   Level 0: Core Dynamic States only
    //   Level 1: Core + EDS1
    //   Level 2: Core + EDS1 + EDS2 (accumulative)
    //   Level 3: Core + EDS1 + EDS2 + EDS3 (accumulative)
    // Here we only verify if extensions were successfully loaded by the device

    dynamic_features.has_extended_dynamic_state =
        device.IsExtExtendedDynamicStateSupported();

    dynamic_features.has_extended_dynamic_state_2 =
        device.IsExtExtendedDynamicState2Supported();
    dynamic_features.has_extended_dynamic_state_2_logic_op =
        device.IsExtExtendedDynamicState2ExtrasSupported();
    dynamic_features.has_extended_dynamic_state_2_patch_control_points = false;

    dynamic_features.has_extended_dynamic_state_3_blend =
        device.IsExtExtendedDynamicState3BlendingSupported();
    dynamic_features.has_extended_dynamic_state_3_enables =
        device.IsExtExtendedDynamicState3EnablesSupported();
    dynamic_features.has_color_write_enable =
        device.IsExtColorWriteEnableSupported();
    dynamic_features.has_dynamic_state3_depth_clamp_enable =
        dynamic_features.has_extended_dynamic_state_3_enables &&
        device.SupportsDynamicState3DepthClampEnable();
    dynamic_features.has_dynamic_state3_logic_op_enable =
        dynamic_features.has_extended_dynamic_state_3_enables &&
        device.SupportsDynamicState3LogicOpEnable();
    dynamic_features.has_dynamic_state3_line_stipple_enable =
        dynamic_features.has_extended_dynamic_state_3_enables &&
        device.SupportsDynamicState3LineStippleEnable();

    // VIDS: Independent toggle (not affected by dyna_state levels)
    dynamic_features.has_dynamic_vertex_input =
        device.IsExtVertexInputDynamicStateSupported() &&
        Settings::values.vertex_input_dynamic_state.GetValue();

    dynamic_features.has_provoking_vertex = device.IsExtProvokingVertexSupported();
    dynamic_features.has_provoking_vertex_first_mode =
        device.SupportsProvokingVertexFirstMode();
    dynamic_features.has_provoking_vertex_last_mode =
        device.SupportsProvokingVertexLastMode();
    dynamic_features.has_provoking_vertex_tf_preserve =
        device.SupportsTransformFeedbackProvokingVertexPreservation();
    if (Eden::Performance::vulkan_cost_enabled.load(std::memory_order_relaxed))
        std::printf("EDEN_VULKAN_DYNAMIC blend=%u enables=%u vertex=%u\n",
                    unsigned(dynamic_features.has_extended_dynamic_state_3_blend),
                    unsigned(dynamic_features.has_extended_dynamic_state_3_enables),
                    unsigned(dynamic_features.has_dynamic_vertex_input));
}

namespace {
void EdenResetStaticVertexInputBinding() noexcept;
} // namespace

PipelineCache::~PipelineCache() {
    EdenResetStaticVertexInputBinding();
#ifdef PS5_NATIVE
    // First-use shader environments are serialized on this worker. Let all
    // accepted writes finish before destroying the worker and VkPipelineCache,
    // or a later launch re-translates shaders that were never persisted.
    // This is a shutdown-only drain, never a wait in the draw hot path.
    serialization_thread.WaitForRequests();
#endif
    if (use_vulkan_pipeline_cache && !vulkan_pipeline_cache_filename.empty()) {
        SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                     CACHE_VERSION);
    }
}

namespace {
std::mutex eden_attributeless_mutex;
using EdenVertexIdentity = std::array<u64, 2>;
std::set<EdenVertexIdentity> eden_attributeless_vertex;
std::atomic<u32> eden_attributeless_generation{0};
bool eden_static_vertex_input_bound = false;

void EdenResetStaticVertexInputBinding() noexcept {
    eden_static_vertex_input_bound = false;
}

void EdenRecordAttributeless(EdenVertexIdentity vertex_hash, const Shader::Info& info) {
    for (size_t index = 0; index < Shader::IR::NUM_GENERICS; ++index) {
        if (info.loads.Generic(index)) {
            return;
        }
    }
    std::scoped_lock lock{eden_attributeless_mutex};
    if (eden_attributeless_vertex.insert(vertex_hash).second) {
        eden_attributeless_generation.fetch_add(1, std::memory_order_release);
    }
}

// GPU thread: whether the vertex shader reads no attribute, as far as built pipelines tell.
bool EdenAttributeless(EdenVertexIdentity vertex_hash) {
    // The graphics thread owns this bounded memo. Shader builders only update
    // the set under its mutex and publish a new generation on insertion.
    // Alternating shader pairs should not lock or tree-search on every draw.
    struct Lookup {
        EdenVertexIdentity hash{};
        u32 generation = ~0u;
        bool result = false;
    };
    static std::array<Lookup, 64> lookups{};
    const u32 generation = eden_attributeless_generation.load(std::memory_order_acquire);
    const size_t slot = (vertex_hash[0] ^ std::rotl(vertex_hash[1], 11)) & (lookups.size() - 1);
    auto& lookup = lookups[slot];
    if (lookup.generation == generation && lookup.hash == vertex_hash)
        return lookup.result;
    const std::scoped_lock lock{eden_attributeless_mutex};
    const bool result = eden_attributeless_vertex.contains(vertex_hash);
    lookup.hash = vertex_hash;
    lookup.result = result;
    lookup.generation = generation;
    return result;
}

// A static key without attributes (what the loader takes back from the disk cache).
bool EdenAttributelessStaticKey(const FixedPipelineState& state) {
    return state.dynamic_vertex_input == 0 &&
           std::ranges::none_of(state.attributes,
                                [](const auto& attribute) { return attribute.raw != 0; });
}
} // Anonymous namespace

GraphicsPipeline* PipelineCache::CurrentGraphicsPipeline() {

    if (!RefreshStages(graphics_key.unique_hashes)) {
        current_pipeline = nullptr;
        return nullptr;
    }
    graphics_key.state.Refresh(*maxwell3d, dynamic_features);
    const bool static_vertex_input = graphics_key.state.dynamic_vertex_input != 0 &&
                                     ::Eden::DevVulkan::static_vertex_input &&
                                     EdenAttributeless({graphics_key.unique_hashes[0], graphics_key.unique_hashes[1]});
    if (static_vertex_input) {
        // Instance-rate streams as the registers set them (with every stream per vertex the
        // driver drew these wrong too); no attribute, so the key does not depend on what the
        // registers hold for attributes the shader does not read.
        const auto& regs = maxwell3d->regs;
        graphics_key.state.dynamic_vertex_input.Assign(0);
        graphics_key.state.enabled_divisors = 0;
        graphics_key.state.attributes = {};
        graphics_key.state.binding_divisors = {};
        for (size_t index = 0; index < Maxwell::NumVertexArrays; ++index) {
            const bool is_enabled = regs.vertex_stream_instances.IsInstancingEnabled(index);
            graphics_key.state.binding_divisors[index] =
                is_enabled ? regs.vertex_streams[index].frequency : 0;
            graphics_key.state.enabled_divisors |= (is_enabled ? u64{1} : 0) << index;
        }
        // The union's attribute_types now holds divisors: the next dynamic draw recomputes them
        // and sets its vertex input again (a static pipeline replaced it).
        maxwell3d->dirty.flags[Dirty::VertexInput] = true;
    }
    if (static_vertex_input && !eden_static_vertex_input_bound) {
        // Rebind the vertex buffers with their strides, dynamic state of the static pipeline.
        auto& flags = maxwell3d->dirty.flags;
        flags[VideoCommon::Dirty::VertexBuffers] = true;
        for (size_t index = 0; index < Maxwell::NumVertexArrays; ++index) {
            flags[VideoCommon::Dirty::VertexBuffer0 + index] = true;
        }
    }
    eden_static_vertex_input_bound = static_vertex_input;

    if (current_pipeline) {
        GraphicsPipeline* const next{current_pipeline->Next(graphics_key)};
        if (next) {
            current_pipeline = next;
            return BuiltPipeline(current_pipeline);
        }
    }
    return CurrentGraphicsPipelineSlowPath();
}

ComputePipeline* PipelineCache::CurrentComputePipeline() {

    const ShaderInfo* const shader{ComputeShader()};
    if (!shader) {
        return nullptr;
    }
    const auto& qmd{kepler_compute->launch_description};
    const ComputePipelineCacheKey key{
        .unique_hash = shader->unique_hash,
        .shared_memory_size = qmd.shared_alloc,
        .workgroup_size{qmd.block_dim_x, qmd.block_dim_y, qmd.block_dim_z},
    };
    const auto [pair, is_new]{compute_cache.try_emplace(key)};
    auto& pipeline{pair->second};
    if (is_new) {
#ifdef PS5_NATIVE
        // P0.1: time every cold compute CPU translation and driver enqueue.
        // The asynchronous Vulkan pipeline wait is reported separately.
        ::Eden::Performance::CachePhaseTimer first_use_compute_cost{
            ::Eden::Performance::CachePhase::ComputePipelineCreate,
            ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
#endif
#ifdef PS5_NATIVE
        ColdShaderMissTrace cold_miss{"compute", key.Hash(), false};
#endif
        pipeline = CreateComputePipeline(key, shader);
#ifdef PS5_NATIVE
        first_use_compute_cost.Stop();
        cold_miss.Complete(static_cast<bool>(pipeline));
        if (!pipeline) {
            // Bound recovery metadata without starving the latest failed
            // compute shaders after 32 earlier failed keys. This scan runs
            // ONLY on a new null pipeline, not per compute dispatch.
            if (failed_compute_retries.size() >= 32) {
                const auto oldest = std::min_element(
                    failed_compute_retries.begin(), failed_compute_retries.end(),
                    [](const auto& left, const auto& right) {
                        return left.second.eligible_at < right.second.eligible_at;
                    });
                if (oldest != failed_compute_retries.end()) {
                    failed_compute_retries.erase(oldest);
                }
            }
            failed_compute_retries.try_emplace(
                key, ComputeRetryState{
                    std::chrono::steady_clock::now() + std::chrono::seconds(2), false});
        }
#endif
    }
#ifdef PS5_NATIVE
    else if (!pipeline) {
        const auto failed = failed_compute_retries.find(key);
        if (failed != failed_compute_retries.end() && !failed->second.attempted &&
            std::chrono::steady_clock::now() >= failed->second.eligible_at) {
            // Mark before translation: persistent failure must never spin
            // within repeated compute dispatches.
            failed->second.attempted = true;
            ::Eden::Performance::CachePhaseTimer retry_compute_cost{
                ::Eden::Performance::CachePhase::ComputePipelineCreate,
                ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
            ColdShaderMissTrace cold_retry{"compute", key.Hash(), true};
            pipeline = CreateComputePipeline(key, shader);
            retry_compute_cost.Stop();
            cold_retry.Complete(static_cast<bool>(pipeline));
            // A consumed retry cannot repeat: the null compute_cache entry
            // is no longer marked retryable. Recycle its 32-key budget slot.
            failed_compute_retries.erase(failed);
        }
    }
#endif
    if (::Eden::DevVulkan::trace_pipelines && pipeline) {
        const auto first_use = ::Eden::PipelineTrace::compute_first_use.Record(
            ::Eden::DevVulkan::gpu_time_session.load(std::memory_order_acquire), pipeline.get());
        if (first_use != 0) {
            char text[160];
            std::snprintf(text, sizeof(text), "C%zu p=%p new=%d cs=%llx shared=%u wg=%u,%u,%u", first_use,
                          (const void*)pipeline.get(), int(is_new), (unsigned long long)key.unique_hash, unsigned(key.shared_memory_size),
                          unsigned(key.workgroup_size[0]), unsigned(key.workgroup_size[1]),
                          unsigned(key.workgroup_size[2]));
            ::Eden::Report("pipeline", text);
            LOG_INFO(Render_Vulkan, "EDEN_PIPELINE_FIRST_USE {}", text);
        }
    }
    return pipeline.get();
}

void PipelineCache::LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                                      const VideoCore::DiskResourceLoadCallback& callback) {
    if (title_id == 0) {
        return;
    }
    if (!pipeline_cache_filename.empty()) {
        serialization_thread.WaitForRequests();
        if (use_vulkan_pipeline_cache && !vulkan_pipeline_cache_filename.empty()) {
            SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                         CACHE_VERSION);
        }
    }

    const auto shader_dir{Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir)};
    const auto base_dir{shader_dir / fmt::format("{:016x}", title_id)};
    if (!Common::FS::CreateDir(shader_dir) || !Common::FS::CreateDir(base_dir)) {
        LOG_ERROR(Common_Filesystem, "Failed to create pipeline cache directories");
        return;
    }
    pipeline_cache_filename = base_dir / "vulkan.bin";

    if (use_vulkan_pipeline_cache) {
        vulkan_pipeline_cache_filename = base_dir / "vulkan_pipelines.bin";
        vulkan_pipeline_cache =
            LoadVulkanPipelineCache(vulkan_pipeline_cache_filename, CACHE_VERSION);
    }

#ifdef PS5_NATIVE
    // The PS5 is launching one foreground game, so do not delay it by
    // compiling the full backlog of shaders from earlier sessions.
    // An already-stopped token is a special initialization-only request:
    // after naming the per-title cache, leave workers RUNNING for later jobs.
    if (stop_loading.stop_requested()) {
        LOG_INFO(Render_Vulkan, "PS5 lazy shader cache ready for {:016x}", title_id);
        return;
    }
#endif

#ifdef PS5_NATIVE
    // Eight entries at most: restore some persisted pipelines without
    // rebuilding an unbounded title cache at launch. This token controls only
    // the disk READER; the persistent Vulkan compiler workers stay alive.
    constexpr size_t kStartupPrewarmPipelines = 8;
    const auto startup_prewarm_started = std::chrono::steady_clock::now();
    std::stop_source startup_reader;
#endif
    struct {
        std::mutex mutex;
        size_t total{};
        size_t built{};
        size_t valid{};
        bool has_loaded{};
        std::unique_ptr<PipelineStatistics> statistics;
    } state;

    if (device.IsKhrPipelineExecutablePropertiesEnabled()) {
        state.statistics = std::make_unique<PipelineStatistics>(device);
    }
    const auto load_compute{[&](std::ifstream& file, FileEnvironment env) {
        ComputePipelineCacheKey key;
        file.read(reinterpret_cast<char*>(&key), sizeof(key));

        workers.QueueWork([this, key, env_ = std::move(env), &state, &callback]() mutable {
            ShaderPools pools;
            auto pipeline{CreateComputePipeline(pools, key, env_, state.statistics.get(), false)};
            std::scoped_lock lock{state.mutex};
            // Do not seed a failed Vulkan compute pipeline for future dispatches.
            if (pipeline && pipeline->IsBound()) {
                compute_cache.emplace(key, std::move(pipeline));
                ++state.valid;
            }
            ++state.built;
            if (state.has_loaded) {
                callback(VideoCore::LoadCallbackStage::Build, state.built, state.total);
            }
        });
        // Workers read total while holding state.mutex. Publish reader
        // increments under the same lock to avoid a cache-prewarm data race.
        {
            std::scoped_lock count_lock{state.mutex};
            ++state.total;
        }
#ifdef PS5_NATIVE
        if (state.total == kStartupPrewarmPipelines)
            startup_reader.request_stop();
#endif
    }};
    const auto load_graphics{[&](std::ifstream& file, std::vector<FileEnvironment> envs) {
        GraphicsPipelineCacheKey key;
        file.read(reinterpret_cast<char*>(&key), sizeof(key));

        if ((key.state.extended_dynamic_state != 0) !=
                dynamic_features.has_extended_dynamic_state ||
            (key.state.extended_dynamic_state_2 != 0) !=
                dynamic_features.has_extended_dynamic_state_2 ||
            (key.state.extended_dynamic_state_2_logic_op != 0) !=
                dynamic_features.has_extended_dynamic_state_2_logic_op ||
            (key.state.extended_dynamic_state_3_blend != 0) !=
                dynamic_features.has_extended_dynamic_state_3_blend ||
            (key.state.extended_dynamic_state_3_enables != 0) !=
                dynamic_features.has_extended_dynamic_state_3_enables ||
            (key.state.color_write_enable_dynamic != 0) !=
                dynamic_features.has_color_write_enable ||
            ((key.state.dynamic_vertex_input != 0) !=
                 dynamic_features.has_dynamic_vertex_input &&
             !(dynamic_features.has_dynamic_vertex_input &&
               ::Eden::DevVulkan::static_vertex_input &&
               EdenAttributelessStaticKey(key.state)))) {
            return;
        }

        const bool key_requests_provoking_last = key.state.provoking_vertex_last != 0;
        if (key_requests_provoking_last && !dynamic_features.has_provoking_vertex_last_mode) {
            return;
        }

        const bool key_uses_transform_feedback = key.state.xfb_enabled != 0;
        if (key_uses_transform_feedback && key_requests_provoking_last &&
            !dynamic_features.has_provoking_vertex_tf_preserve) {
            return;
        }

        workers.QueueWork([this, key, envs_ = std::move(envs), &state, &callback]() mutable {
            ShaderPools pools;
            boost::container::static_vector<Shader::Environment*, 5> env_ptrs;
            for (auto& env : envs_) {
                env_ptrs.push_back(&env);
            }
            auto pipeline{CreateGraphicsPipeline(pools, key, MakeSpan(env_ptrs),
                                                 state.statistics.get(), false)};

            std::scoped_lock lock{state.mutex};
            // Leave a failed Vulkan build absent from the prewarm map,
            // so the normal first-use path may try the key again.
#ifdef PS5_NATIVE
            if (pipeline && pipeline->CanExecuteDraw()) {
#else
            if (pipeline) {
#endif
                graphics_cache.emplace(key, std::move(pipeline));
                ++state.valid;
            }
            ++state.built;
            if (state.has_loaded) {
                callback(VideoCore::LoadCallbackStage::Build, state.built, state.total);
            }
        });
        // Workers read total while holding state.mutex. Publish reader
        // increments under the same lock to avoid a cache-prewarm data race.
        {
            std::scoped_lock count_lock{state.mutex};
            ++state.total;
        }
#ifdef PS5_NATIVE
        if (state.total == kStartupPrewarmPipelines)
            startup_reader.request_stop();
#endif
    }};
    // Only the reader observes the quota stop. Passing this token to
    // workers.WaitForRequests() would permanently request_stop() on the
    // shader-builder pool, breaking first-use shaders later in gameplay.
#ifdef PS5_NATIVE
    const std::stop_token reader_token{startup_reader.get_token()};
#else
    const std::stop_token reader_token{stop_loading};
#endif
#ifdef PS5_NATIVE
    // The progress callback below also runs before the normal drain and can
    // throw. Keep locally borrowed prewarm state alive on EVERY exceptional
    // exit, not only failures of the disk reader. No extra wait on success.
    struct PrewarmDrain {
        Common::ThreadWorker& compiler;
        bool joined{};
        ~PrewarmDrain() {
            if (!joined) compiler.WaitForRequests();
        }
    } prewarm_drain{workers};
#endif
#ifdef PS5_NATIVE
    // A damaged disk cache or an enqueue failure can interrupt the reader
    // after jobs borrowed local state. Always reach the non-cancelling drain.
    try {
#endif
        VideoCommon::LoadPipelines(reader_token, pipeline_cache_filename, CACHE_VERSION, load_compute,
                                   load_graphics);
#ifdef PS5_NATIVE
    } catch (const std::exception& error) {
        LOG_WARNING(Render_Vulkan, "PS5 shader prewarm aborted: {}", error.what());
    } catch (...) {
        LOG_WARNING(Render_Vulkan, "PS5 shader prewarm aborted: unknown exception");
    }
#endif

    LOG_INFO(Render_Vulkan, "Total Pipeline Count: {}", state.total);

    std::unique_lock lock{state.mutex};
    callback(VideoCore::LoadCallbackStage::Build, 0, state.total);
    state.has_loaded = true;
    lock.unlock();

#ifdef PS5_NATIVE
    // A cancelled title-cache load must never stop the persistent shader
    // compiler pool: StatefulThreadWorker::WaitForRequests(stop_token)
    // requests stop on every worker when that token is cancelled.
    // Only the disk reader observes cancellation. The queued <=8 jobs
    // must complete before their borrowed stack state can be destroyed.
    workers.WaitForRequests();
    prewarm_drain.joined = true;
#else
    workers.WaitForRequests(stop_loading);
#endif
#ifdef PS5_NATIVE
    // Completed attempts include failed native handles; track valid results.
    // One startup summary, never a per-frame or per-draw instrumentation cost.
    const auto startup_prewarm_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startup_prewarm_started).count();
    std::printf("EDEN_PS5_SHADER_PREWARM queued=%zu completed=%zu valid=%zu rejected=%zu prebuild_ms=%lld\n",
                state.total, state.built, state.valid, state.built - state.valid,
                static_cast<long long>(startup_prewarm_ms));
#endif

    if (use_vulkan_pipeline_cache) {
        SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                     CACHE_VERSION);
        size_t size = 0;
        vulkan_pipeline_cache.Read(&size, nullptr);
        last_cache_size.store(size, std::memory_order_relaxed);
        last_flush = std::chrono::steady_clock::now();
    }

    if (state.statistics) {
        state.statistics->Report();
    }
}

void PipelineCache::QueueVulkanPipelineCacheFlush() {
    if (!use_vulkan_pipeline_cache || vulkan_pipeline_cache_filename.empty()) {
        return;
    }
#ifdef PS5_NATIVE
    // Recover a failed background serialization without racing the owner's
    // non-atomic quota. The existing time gate still bounds retry frequency.
    if (flush_worker_failed.exchange(false, std::memory_order_acq_rel)) {
        pipelines_since_flush = VULKAN_CACHE_FLUSH_PIPELINES;
    }
#endif
    if (++pipelines_since_flush < VULKAN_CACHE_FLUSH_PIPELINES) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto megabytes = last_cache_size.load(std::memory_order_relaxed) / (1024 * 1024);
    const std::chrono::seconds interval{
        std::max<size_t>(VULKAN_CACHE_FLUSH_MIN_SECONDS, megabytes)};
    if (last_flush.time_since_epoch().count() != 0 && now - last_flush < interval) {
        return;
    }
    if (flush_in_flight.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    pipelines_since_flush = 0;
    last_flush = now;
    // A transient driver-cache read or serializer exception must not pin
    // flush_in_flight=true for the lifetime of the foreground title.
    // Keep the existing periodic budget, filename and background worker.
    try {
        serialization_thread.QueueWork([this] {
            struct ReleaseFlush {
                std::atomic_bool& in_flight;
                ~ReleaseFlush() { in_flight.store(false, std::memory_order_release); }
            } release{flush_in_flight};
            try {
                SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                             CACHE_VERSION);
                size_t size = 0;
                vulkan_pipeline_cache.Read(&size, nullptr);
                last_cache_size.store(size, std::memory_order_relaxed);
            } catch (const std::exception& error) {
#ifdef PS5_NATIVE
                // Signal a new checkpoint, but only the GPU owner may update
                // the ordinary pipeline build quota on its next call.
                flush_worker_failed.store(true, std::memory_order_release);
#endif
                LOG_WARNING(Render_Vulkan, "Deferred driver cache flush failed: {}", error.what());
            } catch (...) {
#ifdef PS5_NATIVE
                flush_worker_failed.store(true, std::memory_order_release);
#endif
                LOG_WARNING(Render_Vulkan, "Deferred driver cache flush failed: unknown exception");
            }
        });
    } catch (const std::exception& error) {
        flush_in_flight.store(false, std::memory_order_release);
        // Keep this checkpoint due for a later retry, without bypassing the
        // existing time gate if the serializer is temporarily unavailable.
        pipelines_since_flush = VULKAN_CACHE_FLUSH_PIPELINES;
        LOG_WARNING(Render_Vulkan, "Deferred cache flush queue rejected: {}", error.what());
    } catch (...) {
        flush_in_flight.store(false, std::memory_order_release);
        pipelines_since_flush = VULKAN_CACHE_FLUSH_PIPELINES;
        LOG_WARNING(Render_Vulkan, "Deferred cache flush queue rejected: unknown exception");
    }
}

GraphicsPipeline* PipelineCache::CurrentGraphicsPipelineSlowPath() {
    const auto [pair, is_new]{graphics_cache.try_emplace(graphics_key)};
    auto& pipeline{pair->second};
    if (is_new) {
#ifdef PS5_NATIVE
        // F07: first-use guest shader translation and SPIR-V generation are
        // synchronous even when the Vulkan pipeline build is queued.
        // Count EVERY cold miss with Detailed Logging, not one in 16 draws.
        // Quiet releases take no clock readings on this path.
        ::Eden::Performance::CachePhaseTimer first_use_cpu_phase{
            ::Eden::Performance::CachePhase::GraphicsPipelineCreate,
            ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
#endif
#ifdef PS5_NATIVE
        ColdShaderMissTrace cold_miss{"graphics", graphics_key.Hash(), false};
#endif
        pipeline = CreateGraphicsPipeline();
#ifdef PS5_NATIVE
        first_use_cpu_phase.Stop();
        cold_miss.Complete(static_cast<bool>(pipeline));
        if (!pipeline) {
            // Failed-key retry metadata is capped, but a burst of >=64
            // failures must not permanently deny NEW shaders their one
            // delayed recovery attempt. Rotate only on failure, never on a
            // successful draw or during the hot null-cache lookup.
            if (failed_graphics_retries.size() >= 64) {
                const auto oldest = std::min_element(
                    failed_graphics_retries.begin(), failed_graphics_retries.end(),
                    [](const auto& left, const auto& right) {
                        return left.second.eligible_at < right.second.eligible_at;
                    });
                if (oldest != failed_graphics_retries.end()) {
                    failed_graphics_retries.erase(oldest);
                }
            }
            failed_graphics_retries.try_emplace(
                graphics_key, GraphicsRetryState{
                    std::chrono::steady_clock::now() + std::chrono::seconds(2), false});
        }
#endif
    }
#ifdef PS5_NATIVE
    else if (!pipeline) {
        const auto failed = failed_graphics_retries.find(graphics_key);
        if (failed != failed_graphics_retries.end() && !failed->second.attempted &&
            std::chrono::steady_clock::now() >= failed->second.eligible_at) {
            // Publish the attempted bit BEFORE the potentially expensive
            // guest IR/SPIR-V translation. Repeated failed HUD draws stay O(1).
            failed->second.attempted = true;
            ::Eden::Performance::CachePhaseTimer retry_cpu_cost{
                ::Eden::Performance::CachePhase::GraphicsPipelineCreate,
                ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
            ColdShaderMissTrace cold_retry{"graphics", graphics_key.Hash(), true};
            pipeline = CreateGraphicsPipeline();
            retry_cpu_cost.Stop();
            cold_retry.Complete(static_cast<bool>(pipeline));
            // The null graphics_cache entry itself guarantees at most one
            // retry for this key. Retaining consumed retry metadata would
            // exhaust the 64-key budget after unrelated transient failures.
            failed_graphics_retries.erase(failed);
        }
    }
#endif
    if (!pipeline) {
        return nullptr;
    }
    if (current_pipeline) {
        current_pipeline->AddTransition(pipeline.get());
    }
    current_pipeline = pipeline.get();
    if (::Eden::DevVulkan::trace_pipelines) {
        const auto first_use = ::Eden::PipelineTrace::graphics_first_use.Record(
            ::Eden::DevVulkan::gpu_time_session.load(std::memory_order_acquire), current_pipeline);
        if (first_use != 0) {
            const auto& h = graphics_key.unique_hashes;
            char text[256];
            std::snprintf(text, sizeof(text), "G%zu p=%p new=%d va=%llx vb=%llx tc=%llx te=%llx gs=%llx fs=%llx",
                          first_use, (const void*)current_pipeline, int(is_new), (unsigned long long)h[0], (unsigned long long)h[1],
                          (unsigned long long)h[2], (unsigned long long)h[3], (unsigned long long)h[4],
                          (unsigned long long)h[5]);
            ::Eden::Report("pipeline", text);
            LOG_INFO(Render_Vulkan, "EDEN_PIPELINE_FIRST_USE {}", text);
        }
    }
    return BuiltPipeline(current_pipeline);
}

GraphicsPipeline* PipelineCache::BuiltPipeline(GraphicsPipeline* pipeline) const noexcept {
    if (pipeline->IsBuilt()) {
        return pipeline;
    }
#ifdef PS5_NATIVE
    // Prefer correct output over a missing shader draw. A not-yet-built
    // async pipeline is safe to return: ConfigureDraw records an ordered
    // scheduler-worker wait, and R378's SCOPE_EXIT publishes completion
    // even on worker failure. No waiting under the guest cache pair here.
    // The standard "skip complex first-use draws" policy stays unchanged
    // on desktop/mobile, where it can improve cold first-use frame times.
    return pipeline;
#endif
    if (!use_asynchronous_shaders) {
        return pipeline;
    }
    // If games are using a small index count, we can assume these are full screen quads.
    // Usually these shaders are only used once for building textures so we can assume they
    // can't be built async
    const auto& draw_state = maxwell3d->draw_manager.draw_state;
    if (draw_state.index_buffer.count <= 6 || draw_state.vertex_buffer.count <= 6) {
        return pipeline;
    }
    return nullptr;
}

std::unique_ptr<GraphicsPipeline> PipelineCache::CreateGraphicsPipeline(
    ShaderPools& pools, const GraphicsPipelineCacheKey& key,
    std::span<Shader::Environment* const> envs, PipelineStatistics* statistics,
    bool build_in_parallel) try {
#ifdef PS5_NATIVE
    // The optional optimizer must yield to foreground graphics translation
    // even before the first-use Vulkan pipeline reaches the worker queue.
    // Disk prewarm passes build_in_parallel=false and remains uncounted.
    GraphicsTranslationPriorityScope translation_priority{
        build_in_parallel && ::Eden::DevVulkan::fast_first_pipelines};
#endif
    auto hash = key.Hash();
    LOG_DEBUG(Render_Vulkan, "{:#016x}", hash);
    size_t env_index{0};
    std::array<Shader::IR::Program, Maxwell::MaxShaderProgram> programs;
    const bool uses_vertex_a{key.unique_hashes[0] != 0};
    const bool uses_vertex_b{key.unique_hashes[1] != 0};

    // Layer passthrough generation for devices without VK_EXT_shader_viewport_index_layer
    Shader::IR::Program* layer_source_program{};

    for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
        const bool is_emulated_stage = layer_source_program != nullptr &&
                                       index == static_cast<u32>(Maxwell::ShaderType::Geometry);
        if (key.unique_hashes[index] == 0 && is_emulated_stage) {
            auto topology = MaxwellToOutputTopology(key.state.topology);
            programs[index] = GenerateGeometryPassthrough(pools.inst, pools.block, host_info,
                                                          *layer_source_program, topology);
            continue;
        }
        if (key.unique_hashes[index] == 0) {
            continue;
        }
        Shader::Environment& env{*envs[env_index]};
        ++env_index;

        const u32 cfg_offset{static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader))};
        ::Eden::Performance::CachePhaseTimer cfg_stage_cost{
            ::Eden::Performance::CachePhase::ShaderCfg,
            ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
        auto cfg_cost = Eden::Performance::VulkanTimer(20);
        Shader::Maxwell::Flow::CFG cfg(env, pools.flow_block, cfg_offset, index == 0);
        cfg_cost.reset();
        cfg_stage_cost.Stop();
        if (!uses_vertex_a || index != 1) {
            // Normal path
            programs[index] = ([&] { auto cost = Eden::Performance::VulkanTimer(21); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderIr, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return TranslateProgram(pools.inst, pools.block, env, cfg, host_info); }());
        } else {
            // VertexB path when VertexA is present.
            auto& program_va{programs[0]};
            auto program_vb{([&] { auto cost = Eden::Performance::VulkanTimer(21); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderIr, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return TranslateProgram(pools.inst, pools.block, env, cfg, host_info); }())};
            programs[index] = MergeDualVertexPrograms(program_va, program_vb, env);
        }

        if (Settings::values.dump_guest_shaders) {
            env.Dump(hash, key.unique_hashes[index]);
        }

        if (programs[index].info.requires_layer_emulation) {
            layer_source_program = &programs[index];
        }
    }
    if (uses_vertex_b) {
        EdenRecordAttributeless({key.unique_hashes[0], key.unique_hashes[1]}, programs[1].info);
    }
    std::array<const Shader::Info*, Maxwell::MaxShaderStage> infos{};
    std::array<vk::ShaderModule, Maxwell::MaxShaderStage> modules;

    const Shader::IR::Program* previous_stage{};
    Shader::Backend::Bindings binding;
    for (size_t index = uses_vertex_a && uses_vertex_b ? 1 : 0; index < Maxwell::MaxShaderProgram;
         ++index) {
        const bool is_emulated_stage = layer_source_program != nullptr &&
                                       index == static_cast<u32>(Maxwell::ShaderType::Geometry);
        if (key.unique_hashes[index] == 0 && !is_emulated_stage) {
            continue;
        }
        UNIMPLEMENTED_IF(index == 0);

        Shader::IR::Program& program{programs[index]};
        const size_t stage_index{index - 1};
        infos[stage_index] = &program.info;

        const auto runtime_info{MakeRuntimeInfo(programs, key, program, previous_stage, device)};
        ConvertLegacyToGeneric(program, runtime_info);
        const std::vector<u32> code{([&] { auto cost = Eden::Performance::VulkanTimer(22); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderSpirv, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return EmitSPIRV(profile, runtime_info, program, binding); }())};
        device.SaveShader(code);
        modules[stage_index] = ([&] { auto cost = Eden::Performance::VulkanTimer(23); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderModule, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return BuildShader(device, code); }());

        // Text log + .spv dump. Text log is gated by gpu_log_level != Off; .spv dump
        // is independent and gated only by gpu_log_shader_dumps.
        const bool should_log = GPU::Logging::IsActive();
        const bool should_dump = Settings::values.gpu_log_shader_dumps.GetValue();
        if (should_log || should_dump) {
            static constexpr std::array stage_names{"vertex", "tess_control", "tess_eval", "geometry", "fragment"};
            const std::string shader_name = fmt::format("shader_{:016x}_{}", key.unique_hashes[index], stage_names[stage_index]);
            if (should_log) {
                const std::string shader_info = fmt::format("SPIR-V size: {} bytes, hash: {:016x}",
                    code.size() * sizeof(u32), key.unique_hashes[index]);
                GPU::Logging::GPULogger::GetInstance().LogShaderCompilation(shader_name, shader_info);
            }
            if (should_dump) {
                GPU::Logging::DumpSpirvShader(key.unique_hashes[index],
                                              std::span<const u32>(code.data(), code.size()));
            }
        }

        if (device.HasDebuggingToolAttached()) {
            const std::string name{fmt::format("Shader {:016x}", key.unique_hashes[index])};
            modules[stage_index].SetObjectNameEXT(name.c_str());
        }
        previous_stage = &program;
    }
    Common::ThreadWorker* const thread_worker{build_in_parallel ? &workers : nullptr};
    return std::make_unique<GraphicsPipeline>(
        scheduler, buffer_cache, texture_cache, vulkan_pipeline_cache, &shader_notify, device,
        descriptor_pool, guest_descriptor_queue, descriptor_buffer_ring, thread_worker, statistics,
        render_pass_cache, key, std::move(modules), infos);

} catch (const Shader::Exception& exception) {
#ifdef PS5_NATIVE
    // Failed first-use shaders can be retried. Do not rebuild their CFGs or
    // write .ash dumps on the normal quiet PS5 path: this is optional debug
    // work on the same GPU-owner thread that already failed translation.
    if (Settings::values.dump_guest_shaders) {
        try {
#endif
    auto hash = key.Hash();
    size_t env_index{0};
    for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
        if (key.unique_hashes[index] == 0) {
            continue;
        }
        Shader::Environment& env{*envs[env_index]};
        ++env_index;

        const u32 cfg_offset{static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader))};
        ::Eden::Performance::CachePhaseTimer cfg_stage_cost{
            ::Eden::Performance::CachePhase::ShaderCfg,
            ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
        auto cfg_cost = Eden::Performance::VulkanTimer(20);
        Shader::Maxwell::Flow::CFG cfg(env, pools.flow_block, cfg_offset, index == 0);
        cfg_cost.reset();
        cfg_stage_cost.Stop();
        env.Dump(hash, key.unique_hashes[index]);
    }
#ifdef PS5_NATIVE
        } catch (const std::exception& dump_error) {
            // Preserve the original shader failure even if the diagnostic
            // CFG pass or the dump file itself fails.
            LOG_WARNING(Render_Vulkan, "Failed shader diagnostic dump: {}", dump_error.what());
        } catch (...) {
            LOG_WARNING(Render_Vulkan, "Failed shader diagnostic dump: unknown exception");
        }
    }
#endif
    LOG_ERROR(Render_Vulkan, "{}", exception.what());
    return nullptr;
}

std::unique_ptr<GraphicsPipeline> PipelineCache::CreateGraphicsPipeline() {
    auto timer = Eden::Performance::VulkanTimer(17);
#ifdef PS5_NATIVE
    // Reserve priority before guest shader environment reads and pool reset.
    // The inner translation scope overlaps this reservation, so there is no
    // idle gap for speculative Vulkan optimization before the first-use build.
    GraphicsTranslationPriorityScope first_use_preparation_priority{
        ::Eden::DevVulkan::fast_first_pipelines};
#endif
    GraphicsEnvironments environments;
    // Preparing guest shader environments is distinct from CFG/IR/SPIR-V
    // translation. Keep GPU-thread ownership and source read order unchanged.
    // Quiet release does not read a clock or update counters.
    {
        ::Eden::Performance::CachePhaseTimer environment_cost{
            ::Eden::Performance::CachePhase::GraphicsShaderEnvironment,
            ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
        GetGraphicsEnvironments(environments, graphics_key.unique_hashes);
    }

    ([&] { auto cost = Eden::Performance::VulkanTimer(19); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderPoolReset, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return main_pools.ReleaseContents(); }());
    auto pipeline{
        CreateGraphicsPipeline(main_pools, graphics_key, environments.Span(), nullptr, true)};
    if (!pipeline || pipeline_cache_filename.empty()) {
        return pipeline;
    }
#ifdef PS5_NATIVE
    // Cache persistence is optional to running this shader. A rejected
    // serializer job must not discard an otherwise usable Vulkan pipeline.
    try {
#endif
    serialization_thread.QueueWork([this, key = graphics_key, envs = std::move(environments.envs)] {
        boost::container::static_vector<const GenericEnvironment*, Maxwell::MaxShaderProgram>
            env_ptrs;
        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
            if (key.unique_hashes[index] != 0) {
                env_ptrs.push_back(&envs[index]);
            }
        }
        SerializePipeline(key, env_ptrs, pipeline_cache_filename, CACHE_VERSION);
    });
#ifdef PS5_NATIVE
    } catch (const std::exception& error) {
        LOG_WARNING(Render_Vulkan, "Graphics shader cache write not queued: {}", error.what());
    } catch (...) {
        LOG_WARNING(Render_Vulkan, "Graphics shader cache write not queued: unknown exception");
    }
#endif
    QueueVulkanPipelineCacheFlush();
    return pipeline;
}

std::unique_ptr<ComputePipeline> PipelineCache::CreateComputePipeline(
    const ComputePipelineCacheKey& key, const ShaderInfo* shader) {
    auto timer = Eden::Performance::VulkanTimer(17);
#ifdef PS5_NATIVE
    // Count compute environment setup and shader pool reset as foreground
    // work, not only the downstream CFG/IR/SPIR-V translation.
    ComputeTranslationPriorityScope first_use_preparation_priority{true};
#endif
    const GPUVAddr program_base{kepler_compute->regs.code_loc.Address()};
    const auto& qmd{kepler_compute->launch_description};
    ComputeEnvironment env{*kepler_compute, *gpu_memory, program_base, qmd.program_start};
    env.SetCachedSize(shader->size_bytes);

    ([&] { auto cost = Eden::Performance::VulkanTimer(19); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderPoolReset, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return main_pools.ReleaseContents(); }());
    auto pipeline{CreateComputePipeline(main_pools, key, env, nullptr, true)};
    if (!pipeline || pipeline_cache_filename.empty()) {
        return pipeline;
    }
#ifdef PS5_NATIVE
    // Cache persistence is optional to running this shader. A rejected
    // serializer job must not discard an otherwise usable Vulkan pipeline.
    try {
#endif
    serialization_thread.QueueWork([this, key, env_ = std::move(env)] {
        SerializePipeline(key, std::array<const GenericEnvironment*, 1>{&env_},
                          pipeline_cache_filename, CACHE_VERSION);
    });
#ifdef PS5_NATIVE
    } catch (const std::exception& error) {
        LOG_WARNING(Render_Vulkan, "Compute shader cache write not queued: {}", error.what());
    } catch (...) {
        LOG_WARNING(Render_Vulkan, "Compute shader cache write not queued: unknown exception");
    }
#endif
    QueueVulkanPipelineCacheFlush();
    return pipeline;
}

std::unique_ptr<ComputePipeline> PipelineCache::CreateComputePipeline(
    ShaderPools& pools, const ComputePipelineCacheKey& key, Shader::Environment& env,
    PipelineStatistics* statistics, bool build_in_parallel) try {
#ifdef PS5_NATIVE
    // Count cold shader translation as foreground work. The pipeline
    // constructor separately counts the queued driver build; both stages
    // now prevent speculative graphics optimization from competing.
    ComputeTranslationPriorityScope translation_priority{build_in_parallel};
#endif
    auto hash = key.Hash();
    if (device.HasBrokenCompute()) {
        LOG_ERROR(Render_Vulkan, "Skipping {:#016x}", hash);
        return nullptr;
    }

    LOG_DEBUG(Render_Vulkan, "{:#016x}", hash);

    ::Eden::Performance::CachePhaseTimer cfg_stage_cost{
        ::Eden::Performance::CachePhase::ShaderCfg,
        ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};
    auto cfg_cost = Eden::Performance::VulkanTimer(20);
        Shader::Maxwell::Flow::CFG cfg{env, pools.flow_block, env.StartAddress()};
        cfg_cost.reset();
        cfg_stage_cost.Stop();

    // Dump it before error.
    if (Settings::values.dump_guest_shaders) {
        env.Dump(hash, key.unique_hash);
    }

    auto program{([&] { auto cost = Eden::Performance::VulkanTimer(21); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderIr, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return TranslateProgram(pools.inst, pools.block, env, cfg, host_info); }())};
    const VkDriverIdKHR driver_id = device.GetDriverID();
    const bool needs_shared_mem_clamp =
        driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY ||
        driver_id == VK_DRIVER_ID_ARM_PROPRIETARY;
    const u32 max_shared_memory = device.GetMaxComputeSharedMemorySize();
    if (needs_shared_mem_clamp && program.shared_memory_size > max_shared_memory) {
        LOG_WARNING(Render_Vulkan,
                    "Compute shader {:#016x} requests {}KB shared memory but device max is {}KB - clamping",
                    key.unique_hash,
                    program.shared_memory_size / 1024,
                    max_shared_memory / 1024);
        program.shared_memory_size = max_shared_memory;
    }
    const std::vector<u32> code{([&] { auto cost = Eden::Performance::VulkanTimer(22); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderSpirv, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return EmitSPIRV(profile, program); }())};
    device.SaveShader(code);
    vk::ShaderModule spv_module{([&] { auto cost = Eden::Performance::VulkanTimer(23); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderModule, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return BuildShader(device, code); }())};

    // Text log + .spv dump. Same split as the graphics path.
    const bool should_log = GPU::Logging::IsActive();
    const bool should_dump = Settings::values.gpu_log_shader_dumps.GetValue();
    if (should_log || should_dump) {
        const std::string shader_name = fmt::format("shader_{:016x}_compute", key.unique_hash);
        if (should_log) {
            const std::string shader_info = fmt::format("SPIR-V size: {} bytes, hash: {:016x}",
                code.size() * sizeof(u32), key.unique_hash);
            GPU::Logging::GPULogger::GetInstance().LogShaderCompilation(shader_name, shader_info);
        }
        if (should_dump) {
            GPU::Logging::DumpSpirvShader(key.unique_hash,
                                          std::span<const u32>(code.data(), code.size()));
        }
    }

    if (device.HasDebuggingToolAttached()) {
        const auto name{fmt::format("Shader {:016x}", key.unique_hash)};
        spv_module.SetObjectNameEXT(name.c_str());
    }
    Common::ThreadWorker* const thread_worker{build_in_parallel ? &workers : nullptr};
#ifdef PS5_NATIVE
    auto result = std::make_unique<ComputePipeline>(device, scheduler, vulkan_pipeline_cache, descriptor_pool,
                                                    guest_descriptor_queue, descriptor_buffer_ring,
                                                    thread_worker, statistics,
                                                    &shader_notify, program.info, std::move(spv_module),
                                                    key.unique_hash);
    // The shader stage hash is not the compute CACHE key: include workgroup
    // sizes and shared memory for correlation with S01's exact key.Hash().
    result->SetDiagnosticCacheKeyHash(key.Hash());
    return result;
#else
    return std::make_unique<ComputePipeline>(device, scheduler, vulkan_pipeline_cache, descriptor_pool,
                                             guest_descriptor_queue, descriptor_buffer_ring,
                                             thread_worker, statistics,
                                             &shader_notify, program.info, std::move(spv_module),
                                             key.unique_hash);
#endif

} catch (const Shader::Exception& exception) {
    LOG_ERROR(Render_Vulkan, "{}", exception.what());
    return nullptr;
}

void PipelineCache::SerializeVulkanPipelineCache(const std::filesystem::path& filename,
                                                 const vk::PipelineCache& pipeline_cache,
                                                 u32 cache_version) {
    auto timer = Eden::Performance::VulkanTimer(18);
#ifdef PS5_NATIVE
    // The foreground title may exit while background serializer work exists.
    // Serialize staged files to one writer at a time. Never erase the
    // previously verified driver cache in a write/rename failure path.
    static std::mutex save_mutex;
    const std::lock_guard save_lock{save_mutex};
    auto staging = filename;
    staging += ".new";
    try {
        std::ofstream file;
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.open(staging, std::ios::binary | std::ios::trunc);
        file.write(VULKAN_CACHE_MAGIC_NUMBER.data(), VULKAN_CACHE_MAGIC_NUMBER.size())
            .write(reinterpret_cast<const char*>(&cache_version), sizeof(cache_version));

        size_t cache_size = 0;
        std::vector<char> cache_data;
        if (pipeline_cache) {
            pipeline_cache.Read(&cache_size, nullptr);
            cache_data.resize(cache_size);
            pipeline_cache.Read(&cache_size, cache_data.data());
        }
        if (cache_size != 0)
            file.write(cache_data.data(), static_cast<std::streamsize>(cache_size));
        file.flush();
        file.close();
        std::error_code rename_error;
        std::filesystem::rename(staging, filename, rename_error);
        if (rename_error) {
            LOG_WARNING(Common_Filesystem, "Driver cache rename failed: {}", rename_error.message());
            std::error_code cleanup_error;
            std::filesystem::remove(staging, cleanup_error);
            return;
        }
        LOG_INFO(Render_Vulkan, "Saved PS5 driver pipeline cache atomically: {}",
                 Common::FS::PathToUTF8String(filename));
    } catch (const std::exception& error) {
        LOG_WARNING(Common_Filesystem, "Retained old driver cache after failed save: {}", error.what());
        std::error_code cleanup_error;
        std::filesystem::remove(staging, cleanup_error);
    }
#else
    try {
        std::ofstream file(filename, std::ios::binary);
        file.exceptions(std::ifstream::failbit);
        if (!file.is_open())
            return;
        file.write(VULKAN_CACHE_MAGIC_NUMBER.data(), VULKAN_CACHE_MAGIC_NUMBER.size())
            .write(reinterpret_cast<const char*>(&cache_version), sizeof(cache_version));
        size_t cache_size = 0;
        std::vector<char> cache_data;
        if (pipeline_cache) {
            pipeline_cache.Read(&cache_size, nullptr);
            cache_data.resize(cache_size);
            pipeline_cache.Read(&cache_size, cache_data.data());
        }
        file.write(cache_data.data(), cache_size);
    } catch (const std::ios_base::failure& error) {
        LOG_ERROR(Common_Filesystem, "{}", error.what());
    }
#endif
}

vk::PipelineCache PipelineCache::LoadVulkanPipelineCache(const std::filesystem::path& filename,
                                                         u32 expected_cache_version) {
    const auto create_pipeline_cache = [this](size_t data_size, const void* data) {
        VkPipelineCacheCreateInfo pipeline_cache_ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .initialDataSize = data_size,
            .pInitialData = data};
        try {
            return device.GetLogical().CreatePipelineCache(pipeline_cache_ci);
        } catch (const std::exception& error) {
            if (data_size == 0) throw;
            LOG_WARNING(Render_Vulkan,
                        "Ignoring incompatible driver pipeline cache: {}", error.what());
            pipeline_cache_ci.initialDataSize = 0;
            pipeline_cache_ci.pInitialData = nullptr;
            return device.GetLogical().CreatePipelineCache(pipeline_cache_ci);
        }
    };
    try {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return create_pipeline_cache(0, nullptr);
        }
        file.exceptions(std::ifstream::failbit);
        const auto end{file.tellg()};
        file.seekg(0, std::ios::beg);

        std::array<char, 8> magic_number;
        u32 cache_version;
        file.read(magic_number.data(), magic_number.size())
            .read(reinterpret_cast<char*>(&cache_version), sizeof(cache_version));
        if (magic_number != VULKAN_CACHE_MAGIC_NUMBER || cache_version != expected_cache_version) {
            file.close();
            if (Common::FS::RemoveFile(filename)) {
                if (magic_number != VULKAN_CACHE_MAGIC_NUMBER) {
                    LOG_ERROR(Common_Filesystem, "Invalid Vulkan driver pipeline cache file");
                }
                if (cache_version != expected_cache_version) {
                    LOG_INFO(Common_Filesystem, "Deleting old Vulkan driver pipeline cache");
                }
            } else {
                LOG_ERROR(Common_Filesystem,
                          "Invalid Vulkan pipeline cache file and failed to delete it in \"{}\"",
                          Common::FS::PathToUTF8String(filename));
            }
            return create_pipeline_cache(0, nullptr);
        }

        static constexpr size_t header_size = magic_number.size() + sizeof(cache_version);
        const size_t cache_size = static_cast<size_t>(end) - header_size;
        std::vector<char> cache_data(cache_size);
        file.read(cache_data.data(), cache_size);

        LOG_INFO(Render_Vulkan,
                 "Loaded Vulkan driver pipeline cache: ", Common::FS::PathToUTF8String(filename));

        return create_pipeline_cache(cache_size, cache_data.data());

    } catch (const std::ios_base::failure& e) {
        LOG_ERROR(Common_Filesystem, "{}", e.what());
        if (!Common::FS::RemoveFile(filename)) {
            LOG_ERROR(Common_Filesystem, "Failed to delete Vulkan driver pipeline cache file {}",
                      Common::FS::PathToUTF8String(filename));
        }

        return create_pipeline_cache(0, nullptr);
    }
}

} // namespace Vulkan
