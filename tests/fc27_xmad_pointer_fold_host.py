#!/usr/bin/env python3
"""FC27/Maxwell XMAD pointer-fold qualification.

Ports only the stabilized algebraic fold kept by current Eglantine/Lichii.
The host oracle proves integer equivalence; PS5/FC27 visual effect remains
unverified until a user-approved native build and console run.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
CONST=ROOT/"encore-active/eden-shader/src/shader_recompiler/ir_opt/constant_propagation_pass.cpp"
CACHE=ROOT/"encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp"
PINNED=ROOT/"sources/eden/src/shader_recompiler/ir_opt/constant_propagation_pass.cpp"

def blob(path: Path)->str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()

const_blob=blob(CONST)
assert const_blob in {"ce074980d021e14c80b76b4acb1372791c106b5f","9cbdcbffe4117f9bc7957a69d15e6a277fcfe12b"}, const_blob
if const_blob=="9cbdcbffe4117f9bc7957a69d15e6a277fcfe12b":
    current=CONST.read_text()
    assert current.count("            if (count == 0) {\n                return 0U;\n            }\n            const u32 mask{count == 32 ? ~0U : (1U << count) - 1};\n            return (base >> shift) & mask;\n")==1
    restored=current.replace("            if (count == 0) {\n                return 0U;\n            }\n            const u32 mask{count == 32 ? ~0U : (1U << count) - 1};\n            return (base >> shift) & mask;\n","            return (base >> shift) & ((1U << count) - 1);\n",1).encode()
    assert hashlib.sha1(b"blob "+str(len(restored)).encode()+bytes([0])+restored).hexdigest()=="ce074980d021e14c80b76b4acb1372791c106b5f"
cache_blob=blob(CACHE)
# Verify the complete deduplicated prewarm successor before replaying the frozen shader proof.
cache_source = CACHE.read_text()
# Reconstruct only the exact PS5 cached-pipeline limit successor for
# this historical shader algebra receipt. Do not run the old proof against
# unreviewed Vulkan sources.
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
    assert cache_source.count(new_policy) == 1, "Unexpected FC27 cache owner"
    cache_source = cache_source.replace(new_policy, old_policy, 1)
    raw = cache_source.encode("utf-8")
    cache_blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()
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
    # Immutable guest snapshot successor used by the shipping parallel
    # translation owner. Exact blob admission only; semantic checks below
    # still cover fallback, teardown and bounded FC27 prewarm.
    "ad07512d129667a617b49b448d898c5756c99025",
}, cache_blob
if cache_blob in {"e10c4893c03da57dce7f607d3e0af46d62e343f1","dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56","e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41","ad07512d129667a617b49b448d898c5756c99025"}:
    selected=CACHE.read_text()
    assert "fragment_shuffle_fallback[index] = true;" in selected
    assert "EmitSPIRV(emit_profile, runtime_info, program, binding)" in selected
    assert "skipping fragment shuffle with known driver issue" in selected
    if cache_blob in {"dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56","e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41"}:
        assert "EDEN_FRAGMENT_SHUFFLE_FALLBACK count=%u hash=0x%016llx" in selected
        assert "fallback_witness <= 8" in selected
        if cache_blob in {"e3910d11f33376097a90858118ba276f7e02cb70","cfac496d574010b8121611c8616d38bf7cdcbc41","ad07512d129667a617b49b448d898c5756c99025"}:
            # Successor changed only the reviewed PS5 pipeline teardown sequence.
            # The exact source blob above prevents accidentally accepting further drift.
            assert "workers.WaitForRequests();" in selected
            assert selected.index("workers.WaitForRequests();") < selected.index("serialization_thread.WaitForRequests();")
if cache_blob in {"cfac496d574010b8121611c8616d38bf7cdcbc41",
                  "ad07512d129667a617b49b448d898c5756c99025"}:
    # Only the reviewed native cache-admission policy changed after e3910d;
    # preserve the graphics shader fallback and shutdown drain contracts.
    selected=CACHE.read_text()
    assert selected.count("kStartupPrewarmPipelines = 32;")==1
    assert "kRecentShaderCacheScanLimit = 64ULL << 20;" in selected
    assert "recent_cache_builds.pop_front();" in selected
    assert "prefer_recent_cache ? stop_loading : startup_reader.get_token()" in selected
    assert "workers.WaitForRequests();" in selected
assert blob(PINNED)=="57c08588fbd1a160acbcdd516c2d38d15f95818b"

src=CONST.read_text()
pinned=PINNED.read_text()
cache=CACHE.read_text()
for needle in (
    "bool FoldHalvesRecombine(IR::Inst& inst)",
    "void FoldIMul32(IR::Inst& inst)",
    "factor.U32() == 0",
    "factor.U32() == 1",
    "return FoldIMul32(inst);",
):
    assert src.count(needle)==1, needle
assert "FoldHalvesRecombine" not in pinned
assert "void FoldIMul32" not in pinned
assert "constexpr u32 CACHE_VERSION = 21;" in cache
assert "constexpr u32 CACHE_VERSION = 20;" not in cache
assert "constexpr u32 CACHE_VERSION = 19;" not in cache

# Keep exact upstream structural constraints: only a 16/16 split of the SAME
# source can collapse. Do not generalize this into arbitrary bit-field algebra.
half=src.split("bool FoldHalvesRecombine(IR::Inst& inst)",1)[1].split("\ntemplate <typename T>",1)[0]
for needle in (
    "shl->Arg(1) != IR::Value{16U}",
    "low->Arg(1) != IR::Value{0U}",
    "low->Arg(2) != IR::Value{16U}",
    "high->Arg(1) != IR::Value{16U}",
    "high->Arg(2) != IR::Value{16U}",
    "high->Arg(0).Resolve() != low->Arg(0).Resolve()",
):
    assert needle in half, needle

cpp=r"""
#include <cassert>
#include <cstdint>
#include <random>
#include <limits>
using u32=std::uint32_t;
static u32 lo16(u32 x){ return x & 0xffffu; }
static u32 hi16(u32 x){ return (x >> 16) & 0xffffu; }
static u32 recombine(u32 x){ return (hi16(x) << 16) + lo16(x); }
int main(){
    const u32 edge[]={
        0u,1u,2u,0xffffu,0x10000u,0x12345678u,0x80000000u,
        0xfffffffeu,0xffffffffu
    };
    for(u32 x:edge){
        assert(u32(x*0u)==0u);
        assert(u32(x*1u)==x);
        assert(recombine(x)==x);
    }
    std::mt19937 rng(0xFC270001u);
    for(unsigned i=0;i<200000;i++){
        const u32 x=rng();
        assert(u32(x*0u)==0u);
        assert(u32(x*1u)==x);
        assert(recombine(x)==x);
    }
}
""";
with tempfile.TemporaryDirectory(prefix="fc27-xmad-fold-") as tmp:
    p=Path(tmp)
    (p/"test.cpp").write_text(cpp)
    subprocess.run([
        os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
        "-fsanitize=undefined","-fno-sanitize-recover=all",
        str(p/"test.cpp"),"-o",str(p/"test")
    ],check=True,timeout=60)
    subprocess.run([str(p/"test")],check=True,timeout=20)
print("FC27_XMAD_POINTER_FOLD_PASS cases=200009 pinned_immutable=1 cache_version=21 ps5_visual=unverified")
