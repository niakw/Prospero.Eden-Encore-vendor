#!/usr/bin/env python3
"""Exact, source-only reversal for the bounded FC27 cached-pipeline admission change."""
import hashlib

NONE_RT_BLOB = "d222fb5565cdf8db30c6d3d464f107ff26a042c8"
PARALLEL_TRANSLATION_METRIC_BLOB = "52470b6160f6049801d963960adf6952c79a45cf"
PARALLEL_TRANSLATION_METRIC_REVERSES = [
    [
        "    std::array<std::exception_ptr, Maxwell::MaxShaderProgram> parallel_errors{};\n    // CachePhaseTimer's per-key shader attribution is intentionally thread-local.\n    // Give every stage worker a private accumulator, then merge after the owner\n    // joins them so EDEN_VULKAN_SHADER_MISS keeps the pre-parallel accounting.\n    std::array<::Eden::Performance::ShaderMissStageCosts, Maxwell::MaxShaderProgram>\n        parallel_stage_costs{};\n    if (parallel_translate_stages) {",
        "    std::array<std::exception_ptr, Maxwell::MaxShaderProgram> parallel_errors{};\n    if (parallel_translate_stages) {"
    ],
    [
        "                [this, env, stage_pools, cfg_offset, exits_to_dispatcher, index,\n                 &parallel_programs, &parallel_errors, &parallel_stage_costs] {\n                    struct ShaderMissStageScope {\n                        ::Eden::Performance::ShaderMissStageCosts* previous{\n                            ::Eden::Performance::shader_miss_stage_active};\n                        explicit ShaderMissStageScope(\n                            ::Eden::Performance::ShaderMissStageCosts* current) noexcept {\n                            ::Eden::Performance::shader_miss_stage_active = current;\n                        }\n                        ~ShaderMissStageScope() {\n                            ::Eden::Performance::shader_miss_stage_active = previous;\n                        }\n                    } shader_miss_stage_scope{&parallel_stage_costs[index]};\n                    struct PreflushedTexturePoolScope {",
        "                [this, env, stage_pools, cfg_offset, exits_to_dispatcher, index,\n                 &parallel_programs, &parallel_errors] {\n                    struct PreflushedTexturePoolScope {"
    ],
    [
        "        shader_translation_workers.WaitForRequests();\n        if (auto* const owner_stage_costs =\n                ::Eden::Performance::shader_miss_stage_active) {\n            for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {\n                if (key.unique_hashes[index] == 0) {\n                    continue;\n                }\n                // These are CPU-work totals, matching the former serial keyed\n                // timers. The enclosing miss cpu_ns remains the elapsed critical\n                // path and therefore reflects the parallel overlap.\n                owner_stage_costs->cfg_ns += parallel_stage_costs[index].cfg_ns;\n                owner_stage_costs->ir_ns += parallel_stage_costs[index].ir_ns;\n            }\n        }\n        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {",
        "        shader_translation_workers.WaitForRequests();\n        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {"
    ]
]
PARALLEL_TRANSLATION_BLOB = "b28a190e92ca80b0385148a0b558828353326f12"
PARALLEL_TRANSLATION_REVERSES = [
    [
        "#include <cstdio>\n#include <exception>\n#include <stdexcept>\n// ENCORE_PS5_CACHE_DIAG_BEGIN",
        "#include <cstdio>\n// ENCORE_PS5_CACHE_DIAG_BEGIN"
    ],
    [
        "#ifdef PS5_NATIVE\nnamespace VideoCommon {\nvoid SetShaderTexturePoolPreflushedForThread(bool preflushed) noexcept;\n}\n#endif\n\nnamespace Vulkan {\n#ifdef PS5_NATIVE",
        "namespace Vulkan {\n#ifdef PS5_NATIVE"
    ],
    [
        "      use_vulkan_pipeline_cache{Settings::values.use_vulkan_driver_pipeline_cache.GetValue()},\n#ifdef PS5_NATIVE\n      shader_translation_workers(2, \"VkShaderTranslate\"),\n#endif\n      workers(device.HasBrokenParallelShaderCompiling() ? 1ULL : GetTotalPipelineWorkers(),\n              \"VkPipelineBuilder\", {}, Common::ThreadPlacement::Background),",
        "      use_vulkan_pipeline_cache{Settings::values.use_vulkan_driver_pipeline_cache.GetValue()},\n      workers(device.HasBrokenParallelShaderCompiling() ? 1ULL : GetTotalPipelineWorkers(),\n              \"VkPipelineBuilder\", {}, Common::ThreadPlacement::Background),"
    ],
    [
        "std::unique_ptr<GraphicsPipeline> PipelineCache::CreateGraphicsPipeline(\n    ShaderPools& pools, const GraphicsPipelineCacheKey& key,\n    std::span<Shader::Environment* const> envs, PipelineStatistics* statistics,\n    bool build_in_parallel, bool parallel_translate_stages) try {",
        "std::unique_ptr<GraphicsPipeline> PipelineCache::CreateGraphicsPipeline(\n    ShaderPools& pools, const GraphicsPipelineCacheKey& key,\n    std::span<Shader::Environment* const> envs, PipelineStatistics* statistics,\n    bool build_in_parallel) try {"
    ],
    [
        "    // Layer passthrough generation for devices without VK_EXT_shader_viewport_index_layer\n    Shader::IR::Program* layer_source_program{};\n\n#ifdef PS5_NATIVE\n    // Lichii's stage-parallel model adapted to Encore's active renderer:\n    // every stage owns its IR pools until SPIR-V emission has consumed the\n    // resulting Program. Only NS1 first-use calls can enable this flag; NS2\n    // native keeps its authenticated GMMU/channel translation synchronous.\n    std::array<std::unique_ptr<ShaderPools>, Maxwell::MaxShaderProgram> parallel_pools{};\n    std::array<std::unique_ptr<Shader::IR::Program>, Maxwell::MaxShaderProgram>\n        parallel_programs{};\n    std::array<std::exception_ptr, Maxwell::MaxShaderProgram> parallel_errors{};\n    if (parallel_translate_stages) {\n        size_t parallel_env_index{};\n        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {\n            if (key.unique_hashes[index] == 0) {\n                continue;\n            }\n            Shader::Environment* const env = envs[parallel_env_index++];\n            const u32 cfg_offset{\n                static_cast<u32>(env->StartAddress() + sizeof(Shader::ProgramHeader))};\n            parallel_pools[index] = std::make_unique<ShaderPools>();\n            ShaderPools* const stage_pools = parallel_pools[index].get();\n            const bool exits_to_dispatcher = index == 0;\n            shader_translation_workers.QueueWork(\n                [this, env, stage_pools, cfg_offset, exits_to_dispatcher, index,\n                 &parallel_programs, &parallel_errors] {\n                    struct PreflushedTexturePoolScope {\n                        PreflushedTexturePoolScope() {\n                            VideoCommon::SetShaderTexturePoolPreflushedForThread(true);\n                        }\n                        ~PreflushedTexturePoolScope() {\n                            VideoCommon::SetShaderTexturePoolPreflushedForThread(false);\n                        }\n                    } texture_pool_scope;\n                    try {\n                        ::Eden::Performance::CachePhaseTimer cfg_stage_cost{\n                            ::Eden::Performance::CachePhase::ShaderCfg,\n                            ::Eden::Performance::detailed_gpu_profile.load(\n                                std::memory_order_relaxed)};\n                        auto cfg_cost = Eden::Performance::VulkanTimer(20);\n                        Shader::Maxwell::Flow::CFG cfg(\n                            *env, stage_pools->flow_block, cfg_offset, exits_to_dispatcher);\n                        cfg_cost.reset();\n                        cfg_stage_cost.Stop();\n                        auto program = ([&] {\n                            auto cost = Eden::Performance::VulkanTimer(21);\n                            ::Eden::Performance::CachePhaseTimer stage_cost{\n                                ::Eden::Performance::CachePhase::ShaderIr,\n                                ::Eden::Performance::detailed_gpu_profile.load(\n                                    std::memory_order_relaxed)};\n                            return TranslateProgram(stage_pools->inst, stage_pools->block,\n                                                    *env, cfg, host_info);\n                        }());\n                        parallel_programs[index] =\n                            std::make_unique<Shader::IR::Program>(std::move(program));\n                    } catch (...) {\n                        parallel_errors[index] = std::current_exception();\n                    }\n                });\n        }\n        // The GPU owner intentionally waits here. While it waits, the TIC pool\n        // that it flushed cannot be made GPU-dirty by a later draw/dispatch.\n        shader_translation_workers.WaitForRequests();\n        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {\n            if (parallel_errors[index]) {\n                std::rethrow_exception(parallel_errors[index]);\n            }\n            if (key.unique_hashes[index] != 0 && !parallel_programs[index]) {\n                throw std::runtime_error(\"graphics shader translation worker returned no program\");\n            }\n        }\n    }\n#endif\n\n    for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {",
        "    // Layer passthrough generation for devices without VK_EXT_shader_viewport_index_layer\n    Shader::IR::Program* layer_source_program{};\n\n    for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {"
    ],
    [
        "#ifdef PS5_NATIVE\n        if (parallel_translate_stages) {\n            if (!uses_vertex_a || index != 1) {\n                programs[index] = std::move(*parallel_programs[index]);\n            } else {\n                auto& program_va{programs[0]};\n                auto program_vb{std::move(*parallel_programs[index])};\n                programs[index] = MergeDualVertexPrograms(program_va, program_vb, env);\n            }\n        } else\n#endif\n        {\n            const u32 cfg_offset{\n                static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader))};\n            ::Eden::Performance::CachePhaseTimer cfg_stage_cost{\n                ::Eden::Performance::CachePhase::ShaderCfg,\n                ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};\n            auto cfg_cost = Eden::Performance::VulkanTimer(20);\n            Shader::Maxwell::Flow::CFG cfg(env, pools.flow_block, cfg_offset, index == 0);\n            cfg_cost.reset();\n            cfg_stage_cost.Stop();\n            if (!uses_vertex_a || index != 1) {\n                // Normal path\n                programs[index] = ([&] { auto cost = Eden::Performance::VulkanTimer(21); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderIr, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return TranslateProgram(pools.inst, pools.block, env, cfg, host_info); }());\n            } else {\n                // VertexB path when VertexA is present.\n                auto& program_va{programs[0]};\n                auto program_vb{([&] { auto cost = Eden::Performance::VulkanTimer(21); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderIr, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return TranslateProgram(pools.inst, pools.block, env, cfg, host_info); }())};\n                programs[index] = MergeDualVertexPrograms(program_va, program_vb, env);\n            }\n        }\n",
        "        const u32 cfg_offset{static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader))};\n        ::Eden::Performance::CachePhaseTimer cfg_stage_cost{\n            ::Eden::Performance::CachePhase::ShaderCfg,\n            ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};\n        auto cfg_cost = Eden::Performance::VulkanTimer(20);\n        Shader::Maxwell::Flow::CFG cfg(env, pools.flow_block, cfg_offset, index == 0);\n        cfg_cost.reset();\n        cfg_stage_cost.Stop();\n        if (!uses_vertex_a || index != 1) {\n            // Normal path\n            programs[index] = ([&] { auto cost = Eden::Performance::VulkanTimer(21); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderIr, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return TranslateProgram(pools.inst, pools.block, env, cfg, host_info); }());\n        } else {\n            // VertexB path when VertexA is present.\n            auto& program_va{programs[0]};\n            auto program_vb{([&] { auto cost = Eden::Performance::VulkanTimer(21); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderIr, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return TranslateProgram(pools.inst, pools.block, env, cfg, host_info); }())};\n            programs[index] = MergeDualVertexPrograms(program_va, program_vb, env);\n        }\n"
    ],
    [
        "        auto pipeline = CreateGraphicsPipeline(\n            main_pools, graphics_key,\n            std::span<Shader::Environment* const>{ptrs.data(), active}, nullptr, true, false);",
        "        auto pipeline = CreateGraphicsPipeline(\n            main_pools, graphics_key,\n            std::span<Shader::Environment* const>{ptrs.data(), active}, nullptr, true);"
    ],
    [
        "    ([&] { auto cost = Eden::Performance::VulkanTimer(19); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderPoolReset, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return main_pools.ReleaseContents(); }());\n#ifdef PS5_NATIVE\n    bool parallel_translate_stages{};\n    const size_t shader_stage_count = std::count_if(\n        graphics_key.unique_hashes.begin(), graphics_key.unique_hashes.end(),\n        [](u64 hash) { return hash != 0; });\n    if (shader_stage_count >= 2) {\n        // Lichii upstream bound: flushing at most 4 MiB keeps this owner-thread\n        // operation cheaper than serializing multiple expensive IR translations.\n        constexpr u64 MaxFlushedTexturePoolBytes = u64{4} << 20;\n        const auto& tex_header = maxwell3d->regs.tex_header;\n        const u64 pool_bytes =\n            (u64{static_cast<u32>(tex_header.limit)} + 1) *\n            sizeof(Tegra::Texture::TICEntry);\n        const GPUVAddr pool_base = tex_header.Address();\n        const u64 last_offset = pool_bytes - 1;\n        if (pool_bytes <= MaxFlushedTexturePoolBytes &&\n            pool_base <= std::numeric_limits<GPUVAddr>::max() - last_offset) {\n            const GPUVAddr pool_last = pool_base + last_offset;\n            if (gpu_memory->IsWithinGPUAddressRange(pool_last)) {\n                // Flush once on the GPU owner. Translation workers use only\n                // ReadBlockUnsafe for TICs until this owner joins them.\n                gpu_memory->FlushRegion(pool_base, pool_bytes);\n                parallel_translate_stages = true;\n            }\n        }\n    }\n    auto pipeline{CreateGraphicsPipeline(main_pools, graphics_key, environments.Span(),\n                                         nullptr, true, parallel_translate_stages)};\n#else\n    auto pipeline{\n        CreateGraphicsPipeline(main_pools, graphics_key, environments.Span(), nullptr, true)};\n#endif",
        "    ([&] { auto cost = Eden::Performance::VulkanTimer(19); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderPoolReset, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return main_pools.ReleaseContents(); }());\n    auto pipeline{\n        CreateGraphicsPipeline(main_pools, graphics_key, environments.Span(), nullptr, true)};"
    ]
]
RECENT_128_BLOB = "79660c5cc4ff046f24e7ffa50ab066a86857698c"
RECENT_32_BLOB = "a0e2a45b9b0f080b739d97abb92e9485a0063f73"
NONE_RT_GUARD = (
    "                // Unused color slots are encoded as RenderTargetFormat::NONE.\n"
    "                // They have no attachment whose numeric class needs decoding.\n"
    "                // Avoid the generic surface fallback/log and preserve the\n"
    "                // historical default Float type, matching Lichii's path.\n"
    "                if (format == Tegra::RenderTargetFormat::NONE) {\n"
    "                    info.color_output_types[i] = Shader::AttributeType::Float;\n"
    "                    continue;\n"
    "                }\n"
)
RECENT_BUDGET_REVERSES = [
    ("    // FC27: the old fixed 32-entry PREFIX admitted 18 compute and only\n    // 14 graphics shaders from a 28.7 MiB cache. Thousands of newer records\n    // were ignored and the GPU owner still translated 303 new graphics keys.\n    // A recorded FC27 warm cache contains 710 valid pipeline records, yet the\n    // 32-entry prewarm left 361 guest shaders to translate during the match.\n    // For a modest, already populated cache, admit the newest 128 builders\n    // before gameplay; preserve the 32-entry prefix for large/absent caches.\n    // This shifts some first-use work to loading without skipping any draw.\n    constexpr size_t kStartupPrewarmPipelines = 32;\n    constexpr size_t kRecentPrewarmPipelines = 128;\n    constexpr std::uintmax_t kRecentShaderCacheScanLimit = 64ULL << 20;\n    const bool prefer_recent_cache =\n        env_before.present && env_before.bytes > 12 &&\n        env_before.bytes <= kRecentShaderCacheScanLimit;\n    const size_t prewarm_pipeline_limit =\n        prefer_recent_cache ? kRecentPrewarmPipelines : kStartupPrewarmPipelines;\n", "    // FC27: the old fixed 32-entry PREFIX admitted 18 compute and only\n    // 14 graphics shaders from a 28.7 MiB cache. Thousands of newer records\n    // were ignored and the GPU owner still translated 303 new graphics keys.\n    // Parse modest caches once and retain only the newest 32 valid builders;\n    // a large cache keeps the existing fast prefix cap to avoid startup I/O.\n    constexpr size_t kStartupPrewarmPipelines = 32;\n    constexpr std::uintmax_t kRecentShaderCacheScanLimit = 64ULL << 20;\n    const bool prefer_recent_cache =\n        env_before.present && env_before.bytes > 12 &&\n        env_before.bytes <= kRecentShaderCacheScanLimit;\n"),
    ("if (recent_cache_builds.size() == prewarm_pipeline_limit)", "if (recent_cache_builds.size() == kStartupPrewarmPipelines)"),
    ("if (state.total == prewarm_pipeline_limit)", "if (state.total == kStartupPrewarmPipelines)"),
    ("static_cast<long long>(startup_prewarm_ms), prewarm_pipeline_limit);", "static_cast<long long>(startup_prewarm_ms), kStartupPrewarmPipelines);"),
]

def git_blob(text: str) -> str:
    raw = text.encode("utf-8")
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()

def normalize_parallel_translation(text: str) -> str:
    current = git_blob(text)
    if current == PARALLEL_TRANSLATION_METRIC_BLOB:
        for newer, earlier in PARALLEL_TRANSLATION_METRIC_REVERSES:
            assert text.count(newer) == 1, ("Changed parallel timing owner", newer[:90])
            text = text.replace(newer, earlier, 1)
        assert git_blob(text) == PARALLEL_TRANSLATION_BLOB, (
            "Parallel timing predecessor drift")
        current = PARALLEL_TRANSLATION_BLOB
    if current == PARALLEL_TRANSLATION_BLOB:
        for newer, earlier in PARALLEL_TRANSLATION_REVERSES:
            assert text.count(newer) == 1, ("Changed parallel translation owner", newer[:90])
            text = text.replace(newer, earlier, 1)
        assert git_blob(text) == NONE_RT_BLOB, "Parallel translation predecessor drift"
    return text

def normalize_none_render_target(text: str) -> str:
    text = normalize_parallel_translation(text)
    current = git_blob(text)
    if current == NONE_RT_BLOB:
        assert text.count(NONE_RT_GUARD) == 1, "FC27 NONE render-target guard drift"
        text = text.replace(NONE_RT_GUARD, "", 1)
        assert git_blob(text) == RECENT_128_BLOB, "FC27 NONE render-target predecessor drift"
    else:
        assert current == RECENT_128_BLOB, "Unreviewed cached-pipeline source"
    return text

def reverse_adaptive_prewarm(text: str) -> str:
    text = normalize_none_render_target(text)
    assert git_blob(text) == RECENT_128_BLOB, "Unreviewed cached-pipeline source"
    for current, prior in RECENT_BUDGET_REVERSES:
        assert text.count(current) == 1, ("Changed prewarm owner", current[:90])
        text = text.replace(current, prior, 1)
    assert git_blob(text) == RECENT_32_BLOB, "Recent prewarm predecessor drift"
    return text
