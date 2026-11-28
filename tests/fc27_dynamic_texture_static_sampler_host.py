#!/usr/bin/env python3
"""FC27 dynamic texture-table + fixed sampler qualification.

Exact-title provenance: seedds/nxnative documents EA SPORTS FC 27
0100C49025D3E000 using a dynamically indexed c2 texture-handle table with one
fixed sampler word. Lichii #65 preserves the array through BitwiseOr32 and
does not advance the secondary sampler per element.

This test proves the selected Encore owners implement only that behavior.
It is source/host proof, not a PS5 visual pass.
"""
from pathlib import Path
from native_msaa_handle_receipt import verified_depth_compare_sha
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
TEX=ROOT/"encore-active/eden/src/shader_recompiler/ir_opt/texture_pass.cpp"
GFX=ROOT/"encore-active/eden/src/video_core/renderer_vulkan/vk_graphics_pipeline_cost.cpp"
GFX_HEADER=ROOT/"encore-active/eden/src/video_core/renderer_vulkan/vk_graphics_pipeline.h"
CMP=ROOT/"encore-active/eden/src/video_core/renderer_vulkan/vk_compute_pipeline_cost.cpp"
CACHE=ROOT/"encore-active/eden/src/video_core/renderer_vulkan/vk_pipeline_cache_cost.cpp"

def blob(path: Path)->str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()

# Known successor of the original FC27 texture-table owner: vendor cb10b809
# adds only Lichii #1914's found-bit descriptor bound. Its dedicated host test
# validates that helper; retain the FC27 fixed-sampler contract below.
assert verified_depth_compare_sha(TEX)=="30c9a612a88fa6cd832a40ad0f717b4944db45a1"
gfx_blob=blob(GFX)
if gfx_blob=="df36d210d04411b87e1fdad7fbd0689b1e4d3488":
    selected=GFX.read_text()
    reverse = (
        ("    // Maxwell leaves render-target components not written by the fragment shader untouched.\n    // Vulkan otherwise permits undefined values there, which is visible on AMD/RADV.\n    for (size_t rt = 0; rt < Maxwell::NumRenderTargets; ++rt) {\n        color_output_mask |= u32{stage_infos[NUM_STAGES - 1].frag_color_components[rt]}\n                             << (rt * 4);\n    }\n", ""),
        ("        write_mask &= ColorOutputMask(index);\n", ""),
    )
    for newer, earlier in reverse:
        assert newer in selected, "Unexpected GFX source change"
        selected=selected.replace(newer, earlier, 1)
    data=selected.encode("utf-8")
    previous=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
    assert previous=="f3c82781675b6979a8be96a333184e048ef57f81", ("Unexpected source predecessor",previous)
    gfx_blob="f3c82781675b6979a8be96a333184e048ef57f81"
assert gfx_blob in {"efa8fd4d73c111655fced67fe7dd1e6b7c0178ba","526aa16b22ba3918c29ce85c884d606dcfd185a1","2834a18fe7638749ec83cc8f5feb1fcde6c71a9a",
                    "bae8f3c36fc4e595e9b630ca55cc50496468e173","f3c82781675b6979a8be96a333184e048ef57f81"} 
cmp_blob=blob(CMP)
assert cmp_blob in {"7d9fa72ce2afff5a7b1b75a05fc894b1d99ce6a2","7122e9ad2d0d5e8af812223c85d0d9cdb6ae5600","bb9552866e26e502839eed646be9af6ea0a0db0a"} 
cache_blob=blob(CACHE)
# Verify the complete deduplicated prewarm successor before replaying the frozen shader proof.
cache_source = CACHE.read_text()
# The only change to the selected Vulkan prewarm owner is authenticated by
# its whole source blob and inverted ONLY on an in-memory historical copy.
if cache_blob == "61450e8b8c5e76a59660649ed13941eb50e089cb":
    for newer, earlier in (
        ("        // The GPU owner would otherwise sit idle until every stage completes.\n        // Queue the preceding stages, then translate the final active stage\n        // locally using exactly the same immutable snapshot and private pools.\n        size_t parallel_env_index{};\n        const size_t parallel_stage_count = envs.size();\n        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {", "        size_t parallel_env_index{};\n        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {"),
        ("            auto translate_stage =\n                [this, env, stage_pools, cfg_offset, exits_to_dispatcher, index,", "            shader_translation_workers.QueueWork(\n                [this, env, stage_pools, cfg_offset, exits_to_dispatcher, index,"),
        ("                };\n            if (parallel_env_index == parallel_stage_count) {\n                translate_stage();\n            } else {\n                shader_translation_workers.QueueWork(std::move(translate_stage));\n            }\n        }\n        // The GPU owner intentionally waits here. Workers see only immutable", "                });\n        }\n        // The GPU owner intentionally waits here. Workers see only immutable"),
    ):
        assert cache_source.count(newer) == 1
        cache_source = cache_source.replace(newer, earlier, 1)
    raw_prior = cache_source.encode("utf-8")
    cache_blob = hashlib.sha1(b"blob " + str(len(raw_prior)).encode() + bytes([0]) + raw_prior).hexdigest()
    assert cache_blob == "44b0edc24e847d4cbcd47de0ed86241787bda38a"
if cache_blob == "44b0edc24e847d4cbcd47de0ed86241787bda38a":
    new_policy = "    // FC27 PS5 25/11: 598 valid cached keys, only 128 restored in 704 ms,\n    // and 243 first-use translations during gameplay. Prebuild the newest\n    // 768 DISTINCT cached keys before guest Run when the existing file-size\n    // limit allows it. Keep the 32-entry prefix for large/absent caches;\n    // this changes no GPU barriers, shader output or draw ordering.\n    constexpr size_t kStartupPrewarmPipelines = 32;\n    constexpr size_t kRecentPrewarmPipelines = 768;"
    old_policy = "    // For a modest, already populated cache, admit the newest 128 builders\n    // before gameplay; preserve the 32-entry prefix for large/absent caches.\n    // This shifts some first-use work to loading without skipping any draw.\n    constexpr size_t kStartupPrewarmPipelines = 32;\n    constexpr size_t kRecentPrewarmPipelines = 128;"
    assert cache_source.count(new_policy) == 1, "FC27 cache source drift"
    cache_source = cache_source.replace(new_policy, old_policy, 1)
    data = cache_source.encode("utf-8")
    cache_blob = hashlib.sha1(b"blob " + str(len(data)).encode() + bytes([0]) + data).hexdigest()
    assert cache_blob == "7db0462ad24aa4c6356ce4efa0086b356f171be6", cache_blob
if cache_blob == "7db0462ad24aa4c6356ce4efa0086b356f171be6":
    guard = "\n        // Disk records are untrusted: the six guest programs include both\n        // VertexA and VertexB, even though Vulkan exposes five graphics stages.\n        size_t env_index = 0;\n        for (size_t index = 0; index < key.unique_hashes.size(); ++index) {\n            if (key.unique_hashes[index] == 0) continue;\n            if (env_index == envs.size() ||\n                envs[env_index].ShaderStage() != (index == 0 ? Shader::Stage::VertexA\n                                                            : Shader::StageFromIndex(index - 1))) {\n                return;\n            }\n            ++env_index;\n        }\n        if (env_index == 0 || env_index != envs.size()) return;\n"
    capacity = "boost::container::static_vector<Shader::Environment*, Maxwell::MaxShaderProgram> env_ptrs;"
    assert cache_source.count(guard) == cache_source.count(capacity) == 1
    cache_source = cache_source.replace(guard, "", 1).replace(capacity, "boost::container::static_vector<Shader::Environment*, 5> env_ptrs;", 1)
    raw = cache_source.encode("utf-8")
    cache_blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()
    assert cache_blob == "cdcef0c6ec4cdada8521af43f856e66f98d5b0a9"
if cache_blob == "cdcef0c6ec4cdada8521af43f856e66f98d5b0a9":
    for newer, earlier in (
        ("#include \"diagnostics.h\"\n#include <cstddef>\n#include <deque>\n#include <list>\n#include <variant>\n#include <fstream>\n#include <iostream>\n#include <memory>", "#include \"diagnostics.h\"\n#include <cstddef>\n#include <deque>\n#include <fstream>\n#include <iostream>\n#include <memory>"),
        ("        env_before.bytes <= kRecentShaderCacheScanLimit;\n    const size_t prewarm_pipeline_limit =\n        prefer_recent_cache ? kRecentPrewarmPipelines : kStartupPrewarmPipelines;\n    using PrewarmKey = std::variant<ComputePipelineCacheKey, GraphicsPipelineCacheKey>;\n    std::list<std::pair<PrewarmKey, Common::UniqueFunction<void>>> recent_cache_builds;\n    const auto startup_prewarm_started = std::chrono::steady_clock::now();\n    std::stop_source startup_reader;\n#endif", "        env_before.bytes <= kRecentShaderCacheScanLimit;\n    const size_t prewarm_pipeline_limit =\n        prefer_recent_cache ? kRecentPrewarmPipelines : kStartupPrewarmPipelines;\n    std::deque<Common::UniqueFunction<void>> recent_cache_builds;\n    const auto startup_prewarm_started = std::chrono::steady_clock::now();\n    std::stop_source startup_reader;\n#endif"),
        ("    if (device.IsKhrPipelineExecutablePropertiesEnabled()) {\n        state.statistics = std::make_unique<PipelineStatistics>(device);\n    }\n    const auto schedule_prewarm = [&]([[maybe_unused]] const auto& key,\n                                      Common::UniqueFunction<void> build) {\n#ifdef PS5_NATIVE\n        // ThreadWorker does not catch task exceptions. Keep the first failure\n        // until all accepted jobs have finished borrowing this loading state.", "    if (device.IsKhrPipelineExecutablePropertiesEnabled()) {\n        state.statistics = std::make_unique<PipelineStatistics>(device);\n    }\n    const auto schedule_prewarm = [&](Common::UniqueFunction<void> build) {\n#ifdef PS5_NATIVE\n        // ThreadWorker does not catch task exceptions. Keep the first failure\n        // until all accepted jobs have finished borrowing this loading state."),
        ("            // No driver work while scanning the file. Move-only jobs own all\n            // shader environments and cannot outlive this loading function.\n            // Discard oldest unstarted work without creating GPU pipelines.\n            // Live misses append to the disk cache, including keys skipped by\n            // earlier bounded prewarms. Retain the most recent occurrence of\n            // each full typed key, not 128 possibly duplicate records.\n            // The bounded list owns only unstarted jobs; no live GPU resource\n            // or shader environment used by another worker is replaced.\n            const PrewarmKey candidate{key};\n            const auto duplicate = std::find_if(\n                recent_cache_builds.begin(), recent_cache_builds.end(),\n                [&](const auto& entry) { return entry.first == candidate; });\n            if (duplicate != recent_cache_builds.end())\n                recent_cache_builds.erase(duplicate);\n            if (recent_cache_builds.size() == prewarm_pipeline_limit)\n                recent_cache_builds.pop_front();\n            recent_cache_builds.emplace_back(candidate, std::move(build));\n            return;\n        }\n#endif", "            // No driver work while scanning the file. Move-only jobs own all\n            // shader environments and cannot outlive this loading function.\n            // Discard oldest unstarted work without creating GPU pipelines.\n            if (recent_cache_builds.size() == prewarm_pipeline_limit)\n                recent_cache_builds.pop_front();\n            recent_cache_builds.emplace_back(std::move(build));\n            return;\n        }\n#endif"),
        ("        ComputePipelineCacheKey key;\n        file.read(reinterpret_cast<char*>(&key), sizeof(key));\n\n        schedule_prewarm(key, [this, key, env_ = std::move(env), &state, &callback]() mutable {\n            ShaderPools pools;\n            auto pipeline{CreateComputePipeline(pools, key, env_, state.statistics.get(), false)};\n            std::scoped_lock lock{state.mutex};", "        ComputePipelineCacheKey key;\n        file.read(reinterpret_cast<char*>(&key), sizeof(key));\n\n        schedule_prewarm([this, key, env_ = std::move(env), &state, &callback]() mutable {\n            ShaderPools pools;\n            auto pipeline{CreateComputePipeline(pools, key, env_, state.statistics.get(), false)};\n            std::scoped_lock lock{state.mutex};"),
        ("            return;\n        }\n\n        schedule_prewarm(key, [this, key, envs_ = std::move(envs), &state, &callback]() mutable {\n            ShaderPools pools;\n            boost::container::static_vector<Shader::Environment*, 5> env_ptrs;\n            for (auto& env : envs_) {", "            return;\n        }\n\n        schedule_prewarm([this, key, envs_ = std::move(envs), &state, &callback]() mutable {\n            ShaderPools pools;\n            boost::container::static_vector<Shader::Environment*, 5> env_ptrs;\n            for (auto& env : envs_) {"),
        ("                                   load_graphics);\n#ifdef PS5_NATIVE\n        if (prefer_recent_cache && !stop_loading.stop_requested()) {\n            for (auto& entry : recent_cache_builds) {\n                workers.QueueWork(std::move(entry.second));\n                std::scoped_lock lock{state.mutex};\n                ++state.total;\n            }", "                                   load_graphics);\n#ifdef PS5_NATIVE\n        if (prefer_recent_cache && !stop_loading.stop_requested()) {\n            for (auto& build : recent_cache_builds) {\n                workers.QueueWork(std::move(build));\n                std::scoped_lock lock{state.mutex};\n                ++state.total;\n            }"),
    ):
        assert cache_source.count(newer) == 1, "Distinct prewarm owner drift"
        cache_source = cache_source.replace(newer, earlier, 1)
    raw = cache_source.encode("utf-8")
    cache_blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()
    assert cache_blob == "c92e3dbbf4de08030589d9776dded974accf11eb", cache_blob
if cache_blob in {"fc537323bb9f63d70ef5f8cedd65886e6a0099c0", "b246cf9562f8f0c37d287f7049874f04561a4b8f", "206d845256bbdc1d5f8d660aeed718ad4f607063", "c92e3dbbf4de08030589d9776dded974accf11eb"}:
    guarded = cache_source
    if cache_blob == "c92e3dbbf4de08030589d9776dded974accf11eb":
        for current, previous in (
            ("        bool has_loaded{};\n#ifdef PS5_NATIVE\n        std::exception_ptr worker_error;\n#endif\n        // ENCORE_PS5_CACHE_DIAG_BEGIN", "        bool has_loaded{};\n        // ENCORE_PS5_CACHE_DIAG_BEGIN"),
            ("    const auto schedule_prewarm = [&](Common::UniqueFunction<void> build) {\n#ifdef PS5_NATIVE\n        // ThreadWorker does not catch task exceptions. Keep the first failure\n        // until all accepted jobs have finished borrowing this loading state.\n        build = Common::UniqueFunction<void>{[&state, task = std::move(build)]() mutable {\n            try {\n                task();\n            } catch (...) {\n                std::scoped_lock lock{state.mutex};\n                if (!state.worker_error) state.worker_error = std::current_exception();\n            }\n        }};\n", "    const auto schedule_prewarm = [&](Common::UniqueFunction<void> build) {\n#ifdef PS5_NATIVE\n"),
            ("    prewarm_drain.joined = true;\n    // Propagate the original failure on the loading owner, not std::terminate\n    // on a worker. Never advertise a partial failed cache as a successful load.\n    if (state.worker_error) std::rethrow_exception(state.worker_error);\n#else\n    workers.WaitForRequests(stop_loading);", "    prewarm_drain.joined = true;\n#else\n    workers.WaitForRequests(stop_loading);"),
        ):
            assert guarded.count(current) == 1
            guarded = guarded.replace(current, previous, 1)
        raw = guarded.encode()
        assert hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest() == "206d845256bbdc1d5f8d660aeed718ad4f607063"
        cache_blob = "206d845256bbdc1d5f8d660aeed718ad4f607063"
    if cache_blob == "206d845256bbdc1d5f8d660aeed718ad4f607063":
        for current, previous in (
            ("    if (parallel_translate_stages) {\n        // Disk prewarm uses FileEnvironment and can run without a bound\n        // graphics channel. Only the live NS1 owner may read these registers.\n        const auto& snapshot_tex_header = maxwell3d->regs.tex_header;\n        snapshot_texture_base = snapshot_tex_header.Address();\n        snapshot_texture_limit = static_cast<u32>(snapshot_tex_header.limit);\n        snapshot_via_header_index =\n            maxwell3d->regs.sampler_binding == Maxwell::SamplerBinding::ViaHeaderBinding;\n        snapshot_viewport_transform_state =\n            maxwell3d->regs.viewport_scale_offset_enabled;\n        const u64 snapshot_texture_bytes =\n            (u64{snapshot_texture_limit} + 1) * sizeof(Tegra::Texture::TICEntry);\n        bool snapshot_ok =\n", "    if (parallel_translate_stages) {\n        bool snapshot_ok =\n"),
            ("    GPUVAddr snapshot_texture_base{};\n    u32 snapshot_texture_limit{};\n    bool snapshot_via_header_index{};\n    u32 snapshot_viewport_transform_state{};\n", "    const auto& snapshot_tex_header = maxwell3d->regs.tex_header;\n    const GPUVAddr snapshot_texture_base = snapshot_tex_header.Address();\n    const u32 snapshot_texture_limit = static_cast<u32>(snapshot_tex_header.limit);\n    const bool snapshot_via_header_index =\n        maxwell3d->regs.sampler_binding == Maxwell::SamplerBinding::ViaHeaderBinding;\n    const u32 snapshot_viewport_transform_state =\n        maxwell3d->regs.viewport_scale_offset_enabled;\n    const u64 snapshot_texture_bytes =\n        (u64{snapshot_texture_limit} + 1) * sizeof(Tegra::Texture::TICEntry);\n"),
        ):
            assert guarded.count(current) == 1
            guarded = guarded.replace(current, previous, 1)
        raw = guarded.encode()
        assert hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest() == "b246cf9562f8f0c37d287f7049874f04561a4b8f"
        cache_blob = "b246cf9562f8f0c37d287f7049874f04561a4b8f"
    if cache_blob == "b246cf9562f8f0c37d287f7049874f04561a4b8f":
        for current, previous in (
        ("envs = std::move(serialized_environments.envs)", "envs = std::move(environments.envs)"),
        ("    auto pipeline{\n        CreateGraphicsPipeline(main_pools, graphics_key, environments.Span(), nullptr, true)};\n    auto& serialized_environments = environments;\n#endif", "    auto pipeline{\n        CreateGraphicsPipeline(main_pools, graphics_key, environments.Span(), nullptr, true)};\n#endif"),
        ("    // Keep the original cached shader bytes and pristine environment state for a\n    // single serial retry. Workers can mutate their environment's lookup caches.\n    std::optional<GraphicsEnvironments> parallel_environments;\n    if (parallel_translate_stages) {\n        try {\n            parallel_environments.emplace();\n            parallel_environments->envs = environments.envs;\n            size_t active{};\n            for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {\n                if (graphics_key.unique_hashes[index] != 0) {\n                    parallel_environments->env_ptrs[active++] =\n                        &parallel_environments->envs[index];\n                }\n            }\n        } catch (const std::bad_alloc&) {\n            parallel_environments.reset();\n        }\n    }\n    std::unique_ptr<GraphicsPipeline> pipeline;\n    if (parallel_environments) {\n        try {\n            pipeline = CreateGraphicsPipeline(main_pools, graphics_key,\n                                              parallel_environments->Span(), nullptr, true, true);\n        } catch (const ParallelGraphicsRetry&) {\n            parallel_environments.reset();\n        }\n    }\n    if (!parallel_environments) {\n        pipeline = CreateGraphicsPipeline(main_pools, graphics_key, environments.Span(),\n                                          nullptr, true, false);\n    }\n    auto& serialized_environments = parallel_environments ? *parallel_environments : environments;", "    auto pipeline{CreateGraphicsPipeline(main_pools, graphics_key, environments.Span(),\n                                         nullptr, true, parallel_translate_stages)};"),
        ("                throw ParallelGraphicsRetry{};\n            }\n        }\n    } catch (const std::bad_alloc&) {\n        // The drain guard has joined any accepted jobs before this handler.\n        throw ParallelGraphicsRetry{};\n    }\n#endif", "                throw std::runtime_error(\"graphics shader translation worker returned no program\");\n            }\n        }\n    }\n#endif"),
        ("                try {\n                    std::rethrow_exception(parallel_errors[index]);\n                } catch (const std::out_of_range&) {\n                    // The immutable snapshot can reject a valid owner-thread read.\n                    throw ParallelGraphicsRetry{};\n                }", "                std::rethrow_exception(parallel_errors[index]);"),
        ("    if (parallel_translate_stages) try {\n        // QueueWork and pool allocation", "    if (parallel_translate_stages) {\n        // QueueWork and pool allocation"),
        ("namespace {\n#ifdef PS5_NATIVE\n// Internal signal: only the NS1 parallel preparation path may request a serial retry.\nstruct ParallelGraphicsRetry {};\n// Detailed-only", "namespace {\n#ifdef PS5_NATIVE\n// Detailed-only"),
        ):
            assert guarded.count(current) == 1
            guarded = guarded.replace(current, previous, 1)
        prior = guarded.encode("utf-8")
        assert hashlib.sha1(b"blob " + str(len(prior)).encode() + bytes([0]) + prior).hexdigest() == "fc537323bb9f63d70ef5f8cedd65886e6a0099c0"
    drain_guard = "        // QueueWork and pool allocation may throw after earlier tasks borrowed\n        // these locals. Drain before unwinding, without stopping persistent workers.\n        struct TranslationDrain {\n            Common::ThreadWorker& worker;\n            bool joined{};\n            ~TranslationDrain() {\n                if (!joined) worker.WaitForRequests();\n            }\n        } translation_drain{shader_translation_workers};\n"
    joined = "        translation_drain.joined = true;\n"
    assert guarded.count(drain_guard) == guarded.count(joined) == 1
    assert guarded.index(drain_guard) < guarded.index("size_t parallel_env_index{};")
    assert guarded.index(joined) > guarded.index("shader_translation_workers.WaitForRequests();", guarded.index(drain_guard))
    predecessor = guarded.replace(drain_guard, "", 1).replace(joined, "", 1).encode("utf-8")
    cache_blob = hashlib.sha1(b"blob " + str(len(predecessor)).encode() + bytes([0]) + predecessor).hexdigest()
    assert cache_blob == "ad07512d129667a617b49b448d898c5756c99025", cache_blob
if cache_blob in {"52470b6160f6049801d963960adf6952c79a45cf",
                  "b28a190e92ca80b0385148a0b558828353326f12",
                  "d222fb5565cdf8db30c6d3d464f107ff26a042c8",
                  "79660c5cc4ff046f24e7ffa50ab066a86857698c"}:
    from ps5_recent_prewarm_receipt import reverse_adaptive_prewarm
    source_cache_pre_admission = reverse_adaptive_prewarm(CACHE.read_text())
    cache_blob = "a0e2a45b9b0f080b739d97abb92e9485a0063f73"
else:
    source_cache_pre_admission = CACHE.read_text()
if cache_blob=="a0e2a45b9b0f080b739d97abb92e9485a0063f73":
    selected=source_cache_pre_admission
    reverse = (
        ("        // Vulkan requires signed/unsigned fragment outputs to match the\n        // attachment's numeric class. Previously this was enabled only on\n        // MoltenVK: native PS5 RADV would emit floats for integer targets,\n        // making color writes poison/undefined (Vulkan shader interfaces).\n        // Keep the existing desktop policy; fix the PS5 renderer's ABI.\n        const bool require_typed_fragment_outputs =\n#ifdef PS5_NATIVE\n            true;\n#else\n            device.IsMoltenVK();\n#endif\n        if (require_typed_fragment_outputs) {", "        if (device.IsMoltenVK()) {"),
    )
    for newer, earlier in reverse:
        assert newer in selected, "Unexpected CACHE source change"
        selected=selected.replace(newer, earlier, 1)
    data=selected.encode("utf-8")
    previous=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
    assert previous=="cfac496d574010b8121611c8616d38bf7cdcbc41", ("Unexpected source predecessor",previous)
    cache_blob="cfac496d574010b8121611c8616d38bf7cdcbc41"
assert cache_blob in {
    "be5d48abd44996502b0829a6e0501d130a151455",
    "e10c4893c03da57dce7f607d3e0af46d62e343f1",
    "dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56",
    "e3910d11f33376097a90858118ba276f7e02cb70",
    "cfac496d574010b8121611c8616d38bf7cdcbc41",
    "ad07512d129667a617b49b448d898c5756c99025",
}, cache_blob
if cache_blob in {"e10c4893c03da57dce7f607d3e0af46d62e343f1","dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56","e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41","ad07512d129667a617b49b448d898c5756c99025"}:
    selected=CACHE.read_text()
    assert "fragment_shuffle_fallback[index] = true;" in selected
    assert "EmitSPIRV(emit_profile, runtime_info, program, binding)" in selected
    assert "skipping fragment shuffle with known driver issue" in selected
    if cache_blob in {"dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56","e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41","ad07512d129667a617b49b448d898c5756c99025"}:
        assert "EDEN_FRAGMENT_SHUFFLE_FALLBACK count=%u hash=0x%016llx" in selected
        assert "fallback_witness <= 8" in selected

if cache_blob in {"cfac496d574010b8121611c8616d38bf7cdcbc41","ad07512d129667a617b49b448d898c5756c99025"}:
    # Only the reviewed native cache-admission policy changed after e3910d;
    # preserve the graphics shader fallback and shutdown drain contracts.
    selected=CACHE.read_text()
    assert selected.count("kStartupPrewarmPipelines = 32;")==1
    assert "kRecentShaderCacheScanLimit = 64ULL << 20;" in selected
    assert "recent_cache_builds.pop_front();" in selected
    assert "prefer_recent_cache ? stop_loading : startup_reader.get_token()" in selected
    assert "workers.WaitForRequests();" in selected
tex=TEX.read_text()
assert "Upstream Lichii 5e33a765 / #1914" in tex
assert "DynamicDescriptorIndexLimit(dynamic_offset, size_shift)" in tex
gfx=GFX.read_text()
cmp=CMP.read_text()
cache=CACHE.read_text()
if gfx_blob=="526aa16b22ba3918c29ce85c884d606dcfd185a1":
    assert "void GraphicsPipeline::RunBuild()" in gfx
    assert "build_claimed.exchange(true, std::memory_order_acq_rel)" in gfx
if cmp_blob=="7122e9ad2d0d5e8af812223c85d0d9cdb6ae5600":
    assert "void ComputePipeline::RunBuild()" in cmp
    assert "build_claimed.exchange(true, std::memory_order_acq_rel)" in cmp
if cache_blob in {"e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41","ad07512d129667a617b49b448d898c5756c99025"}:
    assert cache.index("workers.WaitForRequests();") < cache.index("serialization_thread.WaitForRequests();")

# Dynamic + static composition. Two dynamic arrays remain deliberately rejected.
for needle in (
    "if (lhs->count > 1 && rhs->count > 1)",
    "if (rhs->count > 1 ||",
    ".dynamic_offset = lhs->dynamic_offset,",
    ".count = lhs->count,",
    "IR::U32 dynamic_offset{offset};",
    "offset_inst->GetOpcode() != IR::Opcode::ShiftLeftLogical32",
):
    assert tex.count(needle)==1,needle
assert "if (lhs->count > 1 || rhs->count > 1)" not in tex

# The texture table advances; the secondary sampler word is fixed.
# Repeated samples of ONE array must not consume multiple descriptor-array
# budget slots; exactly the identity merged by Descriptors::Add is counted.
assert "using ArrayKey = std::array<u32, 11>;" in tex
assert "std::ranges::find(arrays, key) == arrays.end()" in tex
assert "return static_cast<u32>(arrays.size());" in tex
assert "cbuf.count, DynamicDescriptorSizeShift(cbuf.dynamic_offset)" in tex

assert gfx.count("const u32 second_offset{desc.secondary_cbuf_offset};")==1
assert "desc.secondary_cbuf_offset + index_offset" not in gfx
assert cmp.count("const u32 secondary_offset{desc.secondary_cbuf_offset};")==1
assert "desc.secondary_cbuf_offset + index_offset" not in cmp
assert "constexpr u32 CACHE_VERSION = 21;" in cache

# Existing descriptor budgeting stays active; this patch does not blindly pull
# Lichii's later NS2 handle-table machinery into NS1.
for needle in ("DynamicDescriptorCount(", "DynamicSampledTextureCap(",
               "DESCRIPTOR_MAX_COUNT = 1024"):
    assert needle in tex
assert "HandleOp::Index" not in tex

# The selected GPU-owner transition graph must stay bounded. Evicted
# transition hints reenter the canonical graphics_cache; no draw/pipeline is
# dropped and two frequently alternating HUD edges remain reachable.
def method(text,marker):
    start=text.index(marker)
    brace=text.index("{",start)
    depth=0
    for i in range(brace,len(text)):
        if text[i]=="{": depth+=1
        elif text[i]=="}":
            depth-=1
            if depth==0:return text[start:i+1]
    raise AssertionError(marker)

if gfx_blob in {"bae8f3c36fc4e595e9b630ca55cc50496468e173","f3c82781675b6979a8be96a333184e048ef57f81"}:
    selected_header=GFX_HEADER.read_text()
    header_blob=blob(GFX_HEADER)
    if header_blob=="c1ed0200afd3666056dfe0ef60705015a923dac7":
        selected=GFX_HEADER.read_text()
        reverse = (
            ("    [[nodiscard]] VkColorComponentFlags ColorOutputMask(size_t render_target) const noexcept {\n        return (color_output_mask >> (render_target * 4)) & 0xF;\n    }\n\n    [[nodiscard]] u32 ColorOutputMask() const noexcept {\n        return color_output_mask;\n    }\n\n", ""),
            ("    u32 color_output_mask{};\n", ""),
        )
        for newer, earlier in reverse:
            assert newer in selected, "Unexpected GFX_HEADER source change"
            selected=selected.replace(newer, earlier, 1)
        data=selected.encode("utf-8")
        previous=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
        assert previous=="cc53133dd516e2d1d245326866ab3ce0c852b36b", ("Unexpected source predecessor",previous)
        header_blob="cc53133dd516e2d1d245326866ab3ce0c852b36b"
    assert header_blob=="cc53133dd516e2d1d245326866ab3ce0c852b36b"
    transition_next=method(selected_header,
        "GraphicsPipeline* Next(const GraphicsPipelineCacheKey& current_key) noexcept")
    transition_add=method(gfx,"void GraphicsPipeline::AddTransition(")
    transition_members=selected_header.split(
        "    std::vector<GraphicsPipelineCacheKey> transition_keys;",1)[1].split(
        "    std::array<vk::ShaderModule, NUM_STAGES> spv_modules;",1)[0]
    transition_cpp=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>
struct GraphicsPipelineCacheKey {
    unsigned id{};
    bool operator==(const GraphicsPipelineCacheKey&) const = default;
};
struct GraphicsPipeline {
    const GraphicsPipelineCacheKey key;
    explicit GraphicsPipeline(unsigned id):key{id}{}
    void AddTransition(GraphicsPipeline* transition);
    std::vector<GraphicsPipelineCacheKey> transition_keys;
""" + transition_members + transition_next + r"""
};
""" + transition_add + r"""
int main(){
    GraphicsPipeline source{9999};
    std::vector<std::unique_ptr<GraphicsPipeline>> full;
    for(unsigned i=0;i<320;i++){
        full.push_back(std::make_unique<GraphicsPipeline>(i));
        source.AddTransition(full.back().get());
        assert(source.transition_keys.size()==source.transitions.size());
        assert(source.transition_keys.size()<=64);
        if(i==11) {
            assert(source.Next(full[0]->key)==full[0].get());
            assert(source.Next(full[1]->key)==full[1].get());
        }
    }
    // The two hot overlay/HUD transitions survive ring replacement.
    assert(source.Next(full[0]->key)==full[0].get());
    assert(source.Next(full[1]->key)==full[1].get());
    assert(source.Next(full[319]->key)==full[319].get());
    assert(source.Next(source.key)==&source);
    // Hint retirement can miss, but the full canonical owner is retained.
    assert(source.Next(full[2]->key)==nullptr);
    assert(full[2]->key.id==2);
}
"""
else:
    transition_cpp=None

cpp=r"""
#include <cassert>
#include <cstdint>
#include <optional>
#include <random>
#include <utility>
using u32=std::uint32_t;
struct D {
    u32 index, offset, shift, count, dynamic_tag;
    bool secondary=false;
};
static std::optional<std::pair<D,D>> compose(D lhs,D rhs) {
    if (lhs.secondary || rhs.secondary) return std::nullopt;
    if (lhs.count>1 && rhs.count>1) return std::nullopt;
    if (rhs.count>1 ||
        (lhs.count==1 && (lhs.shift>0 || lhs.index>rhs.index || lhs.offset>rhs.offset))) {
        std::swap(lhs,rhs);
    }
    return std::pair{lhs,rhs};
}
static u32 descriptor_count(u32 base,u32 shift,u32 limit) {
    if (limit==0) limit=1;
    const u32 max_bytes=16u*limit;
    if (shift>=31 || base>=max_bytes) return 1;
    const u32 stride=1u<<shift;
    const u32 available=max_bytes-base;
    if (available<8) return 1;
    const u32 count=1u+(available-8u)/stride;
    return count<limit?count:limit;
}
int main(){
    // FC27-style c2 table starting at 0x168 with 8-byte stride plus fixed c2+0x570 sampler.
    D table{2,0x168,0,128,0xFC27}, sampler{2,0x570,0,1,0};
    for(bool reverse: {false,true}) {
        auto pair=reverse?compose(sampler,table):compose(table,sampler);
        assert(pair);
        assert(pair->first.count==128);
        assert(pair->first.offset==0x168);
        assert(pair->first.dynamic_tag==0xFC27);
        assert(pair->second.count==1);
        assert(pair->second.offset==0x570);
        for(u32 i=0;i<128;i++) {
            const u32 index_offset=i<<3;
            const u32 texture_word=pair->first.offset+index_offset;
            const u32 sampler_word=pair->second.offset;
            assert(texture_word==0x168u+i*8u);
            assert(sampler_word==0x570u);
        }
    }
    // Zero-based shifted table form from Lichii #65.
    assert(descriptor_count(0,4,128)==128);
    assert(descriptor_count(0x168,3,128)>=64);
    // Two independently dynamic tables are intentionally not representable.
    assert(!compose(D{2,0,0,64,1},D{3,0,0,64,2}));

    std::mt19937 rng(0xFC270065u);
    for(unsigned n=0;n<200000;n++) {
        const u32 i=rng()%128u;
        const u32 texture=0x168u+(i<<3);
        const u32 sampler_addr=0x570u;
        assert(texture>=0x168u);
        assert(sampler_addr==0x570u);
    }
}
""";
with tempfile.TemporaryDirectory(prefix="fc27-dyn-tex-") as tmp:
    p=Path(tmp)
    (p/"test.cpp").write_text(cpp)
    subprocess.run([
        os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
        "-fsanitize=undefined","-fno-sanitize-recover=all",
        str(p/"test.cpp"),"-o",str(p/"test")
    ],check=True,timeout=60)
    subprocess.run([str(p/"test")],check=True,timeout=20)
    if transition_cpp is not None:
        (p/"transitions.cpp").write_text(transition_cpp)
        subprocess.run([
            os.getenv("CXX","clang++"),"-std=c++20","-DPS5_NATIVE","-O1","-g",
            "-fsanitize=undefined","-fno-sanitize-recover=all",
            str(p/"transitions.cpp"),"-o",str(p/"transitions")
        ],check=True,timeout=60)
        subprocess.run([str(p/"transitions")],check=True,timeout=20)

print("FC27_DYNAMIC_TEXTURE_STATIC_SAMPLER_PASS table_gt32=1 sampler_fixed=1 zero_base_shift=1 cache=21 ps5_visual=unverified")
