#!/usr/bin/env python3
"""NS1 R23 #35: 8 maintained source owner receipts and real draw-run method ASan."""
from pathlib import Path
from native_msaa_handle_receipt import restore_pre_rebind
import hashlib
import os
import re
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
EXPECTED={
  "encore-active/eden-shader/src/shader_recompiler/host_translate_info.h": "af549e742edca3ce3e790ea5343136094a42b197",
  "encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp": "85c523e435ee8803ef3d2087567aa1a4b5d09369",
  "encore-active/eden-shader/src/shader_recompiler/shader_info.h": "d847e7485a9657a6a016354fd70fb423112857ff",
  "encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp": "d7563d41b74d780f4512899e2fda1b2f362f5523",
  "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_graphics_pipeline.h": "d49090a476f6a9bf61026331f59c9fbb1d918729",
  "encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp": "417b9b953bae2dcae511e4166821881f5c443aa1",
  "encore-active/eden-vulkan/vulkan_rasterizer.cpp": "2530e3754435175810cbf24cb20446e177d1f19f",
  "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_rasterizer.h": "cb35d183c8a9c2ea05f5eb8949a167ea295f8ce7"
}
def gitblob(p):
 b=p.read_bytes()
 return hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
# Preserve the R23 frozen owner receipts while independently verifying later
# QueryCache teardown and C++20 host-info designated-init order corrections.
POST_TEARDOWN = "RasterizerVulkan::~RasterizerVulkan() {\n    // QueryCache has a shorter lifetime than BufferCache. BufferCache::~\n    // triggers a final scheduler.Finish() AFTER QueryCache is destroyed.\n    // Drain outstanding queries while alive, then revoke the borrowed pointer\n    // to prevent the shutdown path from reading freed QueryCache storage.\n    scheduler.WaitWorker();\n    scheduler.Finish();\n    scheduler.ClearQueryCache();\n}"
PRE_TEARDOWN = "RasterizerVulkan::~RasterizerVulkan() {\n    scheduler.WaitWorker();\n    scheduler.Finish();\n}"
POST_HOST_INFO_ORDER = "        .support_viewport_mask = device.IsNvViewportArray2Supported(),\n        .support_geometry_shader_passthrough = device.IsNvGeometryShaderPassthroughSupported(),\n        .support_conditional_barrier = device.SupportsConditionalBarriers(),"
PRE_HOST_INFO_ORDER = "        .support_geometry_shader_passthrough = device.IsNvGeometryShaderPassthroughSupported(),\n        .support_conditional_barrier = device.SupportsConditionalBarriers(),\n        .support_viewport_mask = device.IsNvViewportArray2Supported(),"
for path,sha in EXPECTED.items():
 raw=(ROOT/path).read_bytes()
 normalized=raw.decode("utf-8")
 if path.endswith("/vulkan_rasterizer.cpp"):
  normalized=restore_pre_rebind(normalized)
  assert normalized.count(POST_TEARDOWN)==1, "FC27 lifetime guard missing"
  normalized=normalized.replace(POST_TEARDOWN,PRE_TEARDOWN,1)
 elif path.endswith("/texture_pass.cpp") and gitblob(ROOT/path) in {"3d9625f1981d6500e7b294018e0659464869d6f3","889456a31ddeb87e411ce4d0d2d41078b9593244"}:
  texture_blob=gitblob(ROOT/path)
  if texture_blob=="889456a31ddeb87e411ce4d0d2d41078b9593244":
   # Vendor cb10b809 / upstream Lichii #1914 adds only the found-bit
   # descriptor bound. Reconstruct its exact predecessor before applying the
   # older dynamic-table normalization below.
   found_bit="""// The elements a dynamic offset can select below the clamp to the last one: a bit index found by
// FLO (FindUMsb, or 31 minus it for FLO.SH) takes one of 32. Its -1 for no bit set is past any
// count, so it takes the last element as it does in a longer array.
// Upstream Lichii 5e33a765 / #1914.
u32 DynamicDescriptorIndexLimit(const IR::U32& dynamic_offset, u32 size_shift) {
    const IR::Inst* const offset = dynamic_offset.InstRecursive();
    if (!offset || offset->GetOpcode() != IR::Opcode::ShiftLeftLogical32 ||
        !offset->Arg(1).IsImmediate() || offset->Arg(1).U32() != size_shift) {
        return ~0U;
    }
    const auto is_bit_index = [](const IR::Value& value) {
        const IR::Inst* inst = value.IsImmediate() ? nullptr : value.InstRecursive();
        if (inst && inst->GetOpcode() == IR::Opcode::BitwiseXor32 && inst->Arg(1).IsImmediate() &&
            inst->Arg(1).U32() == 31) {
            inst = inst->Arg(0).IsImmediate() ? nullptr : inst->Arg(0).InstRecursive();
        }
        return inst && (inst->GetOpcode() == IR::Opcode::FindUMsb32 ||
                        inst->GetOpcode() == IR::Opcode::FindSMsb32);
    };
    const IR::Value index = offset->Arg(0);
    const IR::Inst* const select = index.IsImmediate() ? nullptr : index.InstRecursive();
    if (select && select->GetOpcode() == IR::Opcode::SelectU32) {
        return is_bit_index(select->Arg(1)) && is_bit_index(select->Arg(2)) ? 32U : ~0U;
    }
    return is_bit_index(index) ? 32U : ~0U;
}

"""
   post_count="""        .count = (std::min)(DynamicDescriptorCount(base_offset, size_shift,
                                                   (std::min)({DESCRIPTOR_MAX_COUNT, sampled_limit,
                                                               resource_limit})),
                            DynamicDescriptorIndexLimit(dynamic_offset, size_shift)),
"""
   pre_count="""        .count = DynamicDescriptorCount(base_offset, size_shift, (std::min)({DESCRIPTOR_MAX_COUNT, sampled_limit, resource_limit})),
"""
   assert normalized.count(found_bit)==1 and normalized.count(post_count)==1
   normalized=normalized.replace(found_bit,"",1).replace(post_count,pre_count,1)
   predecessor=normalized.encode("utf-8")
   assert hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()=="3d9625f1981d6500e7b294018e0659464869d6f3"
  post_or="        // A dynamic texture table may be ORed with one fixed sampler word.\n        // Keep the table as the primary operand so its shader-side index and\n        // descriptor count survive. Two dynamic tables still cannot be\n        // represented by the current descriptor ABI.\n        if (lhs->count > 1 && rhs->count > 1) {\n            return std::nullopt;\n        }\n        if (rhs->count > 1 ||\n            (lhs->count == 1 &&\n             (lhs->shift_left > 0 || lhs->index > rhs->index || lhs->offset > rhs->offset))) {\n            std::swap(lhs, rhs);\n        }\n        return ConstBufferAddr{\n            .index = lhs->index,\n            .offset = lhs->offset,\n            .shift_left = lhs->shift_left,\n            .secondary_index = rhs->index,\n            .secondary_offset = rhs->offset,\n            .secondary_shift_left = rhs->shift_left,\n            .dynamic_offset = lhs->dynamic_offset,\n            .count = lhs->count,\n            .has_secondary = true,\n        };"
  pre_or="        if (lhs->count > 1 || rhs->count > 1) {\n            return std::nullopt;\n        }\n        if (lhs->shift_left > 0 || lhs->index > rhs->index || lhs->offset > rhs->offset) {\n            std::swap(lhs, rhs);\n        }\n        return ConstBufferAddr{\n            .index = lhs->index,\n            .offset = lhs->offset,\n            .shift_left = lhs->shift_left,\n            .secondary_index = rhs->index,\n            .secondary_offset = rhs->offset,\n            .secondary_shift_left = rhs->shift_left,\n            .dynamic_offset = {},\n            .count = 1,\n            .has_secondary = true,\n        };"
  post_zero="    IR::Inst* const offset_inst{offset.InstRecursive()};\n    u32 base_offset{};\n    IR::U32 dynamic_offset{offset};\n    if (offset_inst->GetOpcode() == IR::Opcode::IAdd32) {\n        if (offset_inst->Arg(0).IsImmediate()) {\n            base_offset = offset_inst->Arg(0).U32();\n            dynamic_offset = IR::U32{offset_inst->Arg(1)};\n        } else if (offset_inst->Arg(1).IsImmediate()) {\n            base_offset = offset_inst->Arg(1).U32();\n            dynamic_offset = IR::U32{offset_inst->Arg(0)};\n        } else {\n            return std::nullopt;\n        }\n    } else if (offset_inst->GetOpcode() != IR::Opcode::ShiftLeftLogical32) {\n        return std::nullopt;\n    }"
  pre_zero="    IR::Inst* const offset_inst{offset.InstRecursive()};\n    if (offset_inst->GetOpcode() != IR::Opcode::IAdd32) {\n        return std::nullopt;\n    }\n    u32 base_offset{};\n    IR::U32 dynamic_offset;\n    if (offset_inst->Arg(0).IsImmediate()) {\n        base_offset = offset_inst->Arg(0).U32();\n        dynamic_offset = IR::U32{offset_inst->Arg(1)};\n    } else if (offset_inst->Arg(1).IsImmediate()) {\n        base_offset = offset_inst->Arg(1).U32();\n        dynamic_offset = IR::U32{offset_inst->Arg(0)};\n    } else {\n        return std::nullopt;\n    }"
  assert normalized.count(post_or)==1 and normalized.count(post_zero)==1
  normalized=normalized.replace(post_or,pre_or,1).replace(post_zero,pre_zero,1)
 elif path.endswith("/vk_graphics_pipeline.h") and gitblob(ROOT/path)=="bafff9a441a60b3dfb7e7d5a7a260741f45f59dc":
  # Lichii f8f29b5 waited-pipeline inline build adds only the callable build
  # owner and claim bit. Remove those reviewed members before the frozen R23
  # header receipt.
  assert normalized.count("#include <functional>\n")==1
  run_build="""    // Lichii f8f29b5: first use may claim a queued build instead of waiting
    // behind unrelated worker jobs.
    void RunBuild();

"""
  build_members="""    std::function<void()> build_task;
    std::atomic_bool build_claimed{false};
"""
  assert normalized.count(run_build)==1 and normalized.count(build_members)==1
  normalized=normalized.replace("#include <functional>\n","",1).replace(run_build,"",1).replace(build_members,"",1)
  predecessor=normalized.encode("utf-8")
  assert hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()=="d49090a476f6a9bf61026331f59c9fbb1d918729"
 elif path.endswith("/vk_rasterizer.h") and gitblob(ROOT/path)=="7c754c82c12c4f4c934abd3d7f2cdb25222e8d2a":
  # FC27/Lichii #397 adds one batch hook. Remove exactly that declaration
  # before checking the frozen R23 header receipt.
  cpu_race_decl="    void OnCacheInvalidations(std::span<const std::pair<DAddr, u64>> ranges) override;\n"
  assert normalized.count(cpu_race_decl)==1
  normalized=normalized.replace(cpu_race_decl,"",1)
  restored=normalized.encode("utf-8")
  assert hashlib.sha1(b"blob "+str(len(restored)).encode()+bytes([0])+restored).hexdigest()==EXPECTED[path]
 elif path.endswith("/vk_graphics_pipeline_cost.cpp") and gitblob(ROOT/path) in {"efa8fd4d73c111655fced67fe7dd1e6b7c0178ba","526aa16b22ba3918c29ce85c884d606dcfd185a1"}:
  graphics_blob=gitblob(ROOT/path)
  if graphics_blob=="526aa16b22ba3918c29ce85c884d606dcfd185a1":
   waited_ctor="    configure_func = ConfigureFunc(spv_modules, stage_infos);\n    build_task = std::move(func);\n    if (worker_thread) {\n#ifdef PS5_NATIVE\n        if (::Eden::DevVulkan::fast_first_pipelines) {\n            pending_first_use_graphics_builds.fetch_add(1, std::memory_order_release);\n            try {\n                worker_thread->QueueWork([this] {\n                    SCOPE_EXIT {\n                        pending_first_use_graphics_builds.fetch_sub(1, std::memory_order_release);\n                    };\n                    RunBuild();\n                });\n            } catch (...) {\n                pending_first_use_graphics_builds.fetch_sub(1, std::memory_order_release);\n                throw;\n            }\n        } else\n#endif\n        {\n            worker_thread->QueueWork([this] { RunBuild(); });\n        }\n        build_handed_off = true;\n    } else {\n        build_handed_off = true;\n        RunBuild();\n    }\n}\n\nvoid GraphicsPipeline::RunBuild() {\n    if (build_claimed.exchange(true, std::memory_order_acq_rel)) {\n        return;\n    }\n    build_task();\n    build_task = nullptr;\n}\n"
   predecessor_ctor="    configure_func = ConfigureFunc(spv_modules, stage_infos);\n    if (worker_thread) {\n#ifdef PS5_NATIVE\n        if (::Eden::DevVulkan::fast_first_pipelines) {\n            // Cold first-use compilation has priority over speculative optimization.\n            // Publish BEFORE enqueuing, including work waiting in the builder queue.\n            pending_first_use_graphics_builds.fetch_add(1, std::memory_order_release);\n            try {\n                worker_thread->QueueWork([build = std::move(func)]() mutable {\n                    SCOPE_EXIT {\n                        pending_first_use_graphics_builds.fetch_sub(1, std::memory_order_release);\n                    };\n                    build();\n                });\n            } catch (...) {\n                pending_first_use_graphics_builds.fetch_sub(1, std::memory_order_release);\n                throw;\n            }\n        } else\n#endif\n        {\n            worker_thread->QueueWork(std::move(func));\n        }\n        // QueueWork returned successfully; the worker now owns completion,\n        // including paths where pipeline validation or compilation fails.\n        build_handed_off = true;\n    } else {\n        // Synchronous func() owns the completion callback from this point.\n        build_handed_off = true;\n        func();\n    }\n}\n"
   waited_callback="        // Wait for the pipeline to be built\n        scheduler.Record([this](vk::CommandBuffer) {\n            RunBuild();\n            // Builder published the pipeline before this ordered GPU callback.\n            // Acquire its completion without taking a redundant build mutex.\n            // An unfinished build still follows the original condition wait.\n            if (is_built.load(std::memory_order_acquire)) return;\n#ifdef PS5_NATIVE\n            // Publication can race the initial fast acquire. The build owner\n            // holds this mutex when publishing, so a false result here means\n            // the condition wait must actually release the lock.\n            // Include contention acquiring build_mutex in the keyed\n            // diagnostic; a genuinely blocked condvar wait must not omit\n            // the GPU worker's preceding mutex delay.\n            ::Eden::Performance::PipelineWaitTrace keyed_wait;\n            std::unique_lock lock{build_mutex};\n            keyed_wait.MarkLockAcquired();\n            if (is_built.load(std::memory_order_acquire)) {\n                lock.unlock();\n                // A publisher may complete while this GPU callback waits\n                // for build_mutex. Do not call condvar; report only genuine\n                // (>1ms) mutex contention, never an instant ready callback.\n                if (keyed_wait.WasBlockedOnMutex())\n                    keyed_wait.Complete(\"graphics\", key.Hash(), CanExecuteDraw());\n                return;\n            }\n#endif\n            auto timer = Eden::Performance::VulkanTimer(16);\n            // Only unfinished builds contribute a worker wait; no clock in quiet mode.\n            ::Eden::Performance::CachePhaseTimer pipeline_ready_wait{\n                ::Eden::Performance::CachePhase::GraphicsPipelineWait,\n                ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};\n#ifndef PS5_NATIVE\n            std::unique_lock lock{build_mutex};\n#endif\n            build_condvar.wait(lock, [this] { return is_built.load(std::memory_order::relaxed); });\n#ifdef PS5_NATIVE\n            lock.unlock();\n            pipeline_ready_wait.Stop();\n            timer.reset(); // Exclude diagnostic print I/O from VulkanTimer(16).\n            keyed_wait.Complete(\"graphics\", key.Hash(), CanExecuteDraw());\n#endif"
   predecessor_callback="        // Wait for the pipeline to be built\n        scheduler.Record([this](vk::CommandBuffer) {\n            // Builder published the pipeline before this ordered GPU callback.\n            // Acquire its completion without taking a redundant build mutex.\n            // An unfinished build still follows the original condition wait.\n            if (is_built.load(std::memory_order_acquire)) return;\n#ifdef PS5_NATIVE\n            // Publication can race the initial fast acquire. The build owner\n            // holds this mutex when publishing, so a false result here means\n            // the condition wait must actually release the lock.\n            // Include contention acquiring build_mutex in the keyed\n            // diagnostic; a genuinely blocked condvar wait must not omit\n            // the GPU worker's preceding mutex delay.\n            ::Eden::Performance::PipelineWaitTrace keyed_wait;\n            std::unique_lock lock{build_mutex};\n            keyed_wait.MarkLockAcquired();\n            if (is_built.load(std::memory_order_acquire)) {\n                lock.unlock();\n                // A publisher may complete while this GPU callback waits\n                // for build_mutex. Do not call condvar; report only genuine\n                // (>1ms) mutex contention, never an instant ready callback.\n                if (keyed_wait.WasBlockedOnMutex())\n                    keyed_wait.Complete(\"graphics\", key.Hash(), CanExecuteDraw());\n                return;\n            }\n#endif\n            auto timer = Eden::Performance::VulkanTimer(16);\n            // Only unfinished builds contribute a worker wait; no clock in quiet mode.\n            ::Eden::Performance::CachePhaseTimer pipeline_ready_wait{\n                ::Eden::Performance::CachePhase::GraphicsPipelineWait,\n                ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)};\n#ifndef PS5_NATIVE\n            std::unique_lock lock{build_mutex};\n#endif\n            build_condvar.wait(lock, [this] { return is_built.load(std::memory_order::relaxed); });\n#ifdef PS5_NATIVE\n            lock.unlock();\n            pipeline_ready_wait.Stop();\n            timer.reset(); // Exclude diagnostic print I/O from VulkanTimer(16).\n            keyed_wait.Complete(\"graphics\", key.Hash(), CanExecuteDraw());\n#endif"
   assert normalized.count(waited_ctor)==1 and normalized.count(waited_callback)==1
   normalized=normalized.replace(waited_ctor,predecessor_ctor,1).replace(waited_callback,predecessor_callback,1)
   predecessor=normalized.encode("utf-8")
   assert hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()=="efa8fd4d73c111655fced67fe7dd1e6b7c0178ba"
  fixed="const u32 second_offset{desc.secondary_cbuf_offset};"
  indexed="const u32 second_offset{desc.secondary_cbuf_offset + index_offset};"
  assert normalized.count(fixed)==1 and indexed not in normalized
  normalized=normalized.replace(fixed,indexed,1)
 elif path.endswith("/vk_pipeline_cache_cost.cpp"):
  # FC27 XMAD pointer folding changes generated shader IR, so the selected
  # cache owner advances 19 -> 20. Reverse only that reviewed version bump
  # in the audit copy before reconstructing the frozen R23 receipt.
  raw_cache_blob=gitblob(ROOT/path)
  if raw_cache_blob=="e3910d11f33376097a90858118ba276f7e02cb70":
   current_destructor="""PipelineCache::~PipelineCache() {
    EdenResetStaticVertexInputBinding();
#ifdef PS5_NATIVE
    // Lichii 499e2e4: queued builders capture pipeline objects by pointer.
    // Drain them while the cache still owns every pipeline; otherwise member
    // destruction can race a late builder during title teardown.
    workers.WaitForRequests();
    // First-use shader environments are serialized on this worker. Let all
    // accepted writes finish before destroying the worker and VkPipelineCache,
    // or a later launch re-translates shaders that were never persisted.
    // These are shutdown-only drains, never waits in the draw hot path.
    serialization_thread.WaitForRequests();
#endif
    if (use_vulkan_pipeline_cache && !vulkan_pipeline_cache_filename.empty()) {
        SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                     CACHE_VERSION);
    }
}
"""
   predecessor_destructor="""PipelineCache::~PipelineCache() {
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
"""
   assert normalized.count(current_destructor)==1
   normalized=normalized.replace(current_destructor,predecessor_destructor,1)
   predecessor=normalized.encode("utf-8")
   assert hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()=="dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56"
   raw_cache_blob="dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56"
  if raw_cache_blob=="dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56":
   witness_block="""            static std::atomic<unsigned> fragment_shuffle_witness{};
            const unsigned fallback_witness =
                fragment_shuffle_witness.fetch_add(1, std::memory_order_relaxed) + 1;
            if (fallback_witness <= 8) {
                std::fprintf(stderr,
                             "EDEN_FRAGMENT_SHUFFLE_FALLBACK count=%u hash=0x%016llx\\n",
                             fallback_witness,
                             static_cast<unsigned long long>(key.unique_hashes[index]));
            }
"""
   assert normalized.count(witness_block)==1
   normalized=normalized.replace(witness_block,"",1)
   predecessor=normalized.encode("utf-8")
   assert hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()=="e10c4893c03da57dce7f607d3e0af46d62e343f1"
   raw_cache_blob="e10c4893c03da57dce7f607d3e0af46d62e343f1"
  if raw_cache_blob=="e10c4893c03da57dce7f607d3e0af46d62e343f1":
   post_programs="""    std::array<Shader::IR::Program, Maxwell::MaxShaderProgram> programs;
#ifdef PS5_NATIVE
    // Eden #4503 drops known-broken non-NVIDIA fragment shaders entirely.
    // On PS5 that visibly removes guest draws. Keep the exact predicate but
    // route only matching programs through the newer Lichii/Eglantine
    // broadcast/select SPIR-V fallback.
    std::array<bool, Maxwell::MaxShaderProgram> fragment_shuffle_fallback{};
#endif
"""
   pre_programs="    std::array<Shader::IR::Program, Maxwell::MaxShaderProgram> programs;\n"
   post_skip="""        // Avoid native subgroup shuffle for the known PositionXY fragment
        // pattern on non-NVIDIA drivers. Desktop/mobile preserve Eden #4503's
        // fail-closed skip. PS5 instead uses the newer upstream broadcast
        // fallback so the whole guest draw is not silently discarded.
        if (device.GetDriverID() != VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
            Shader::Optimization::HasBrokenPattern(programs[index])) {
#ifdef PS5_NATIVE
            fragment_shuffle_fallback[index] = true;
            LOG_WARNING(Render_Vulkan,
                        "0x{:016x}: using PS5 fragment shuffle fallback",
                        key.unique_hashes[index]);
#else
            LOG_WARNING(Render_Vulkan,
                        "0x{:016x}: skipping fragment shuffle with known driver issue",
                        key.unique_hashes[index]);
            return nullptr;
#endif
        }"""
   pre_skip="""        // Avoid compiling a known-broken fragment shuffle sourced from
        // PositionXY on non-NVIDIA drivers (notably RADV/RDNA2). Recognize
        // the IR pattern, not a broad title or vendor blacklist.
        if (device.GetDriverID() != VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
            Shader::Optimization::HasBrokenPattern(programs[index])) {
            LOG_WARNING(Render_Vulkan,
                        "0x{:016x}: skipping fragment shuffle with known driver issue",
                        key.unique_hashes[index]);
            return nullptr;
        }"""
   post_emit="""        auto emit_profile = profile;
#ifdef PS5_NATIVE
        emit_profile.has_broken_spirv_fragment_shuffle = fragment_shuffle_fallback[index];
#endif
        const std::vector<u32> code{([&] { auto cost = Eden::Performance::VulkanTimer(22); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderSpirv, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return EmitSPIRV(emit_profile, runtime_info, program, binding); }())};"""
   pre_emit="""        const std::vector<u32> code{([&] { auto cost = Eden::Performance::VulkanTimer(22); ::Eden::Performance::CachePhaseTimer stage_cost{::Eden::Performance::CachePhase::ShaderSpirv, ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)}; return EmitSPIRV(profile, runtime_info, program, binding); }())};"""
   assert normalized.count(post_programs)==1 and normalized.count(post_skip)==1 and normalized.count(post_emit)==1
   normalized=normalized.replace(post_programs,pre_programs,1).replace(post_skip,pre_skip,1).replace(post_emit,pre_emit,1)
   predecessor=normalized.encode("utf-8")
   assert hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()=="be5d48abd44996502b0829a6e0501d130a151455"
   raw_cache_blob="be5d48abd44996502b0829a6e0501d130a151455"
  if raw_cache_blob=="be5d48abd44996502b0829a6e0501d130a151455":
   assert normalized.count("constexpr u32 CACHE_VERSION = 21;")==1
   normalized=normalized.replace("constexpr u32 CACHE_VERSION = 21;",
                                 "constexpr u32 CACHE_VERSION = 19;",1)
  elif raw_cache_blob=="36aae4b17ef8e9681f62fabdd4cf37f09d0beefb":
   assert normalized.count("constexpr u32 CACHE_VERSION = 20;")==1
   normalized=normalized.replace("constexpr u32 CACHE_VERSION = 20;",
                                 "constexpr u32 CACHE_VERSION = 19;",1)
  else:
   assert raw_cache_blob=="c7e3ca901ea7ab82ec679c23cf43a1c556ac7e41", raw_cache_blob
  # Only strip the seven explicitly delimited, once-per-title PS5 cache
  # diagnostics; preserve the original R23 golden owner SHA and every draw
  # method byte. A genuine R23 source change still fails the exact receipt.
  diag=r"(?ms)^[ \t]*// ENCORE_PS5_CACHE_DIAG_BEGIN\n.*?^[ \t]*// ENCORE_PS5_CACHE_DIAG_END\n"
  assert normalized.count("ENCORE_PS5_CACHE_DIAG_BEGIN")==7
  assert normalized.count("ENCORE_PS5_CACHE_DIAG_END")==7
  normalized, removed=re.subn(diag,"",normalized)
  assert removed==7, ("unexpected PS5 diagnostics scope", removed)
  assert normalized.count(POST_HOST_INFO_ORDER)==1, "C++20 host-info order regressed"
  normalized=normalized.replace(POST_HOST_INFO_ORDER,PRE_HOST_INFO_ORDER,1)
 encoded=normalized.encode("utf-8")
 digest=hashlib.sha1(b"blob "+str(len(encoded)).encode()+bytes([0])+encoded).hexdigest()
 # Later driver-cache preflight (vendor #115) changes only includes, the
 # cache-header helper, native save staging and native disk reader. The
 # exact reviewed original blob (and post-R23 normalized receipt) are
 # both required for this successor; retain the historical R23 reference.
 if path.endswith("/vk_pipeline_cache_cost.cpp") and gitblob(ROOT/path) in {
     "c7e3ca901ea7ab82ec679c23cf43a1c556ac7e41",
     "36aae4b17ef8e9681f62fabdd4cf37f09d0beefb",
     "be5d48abd44996502b0829a6e0501d130a151455",
     "e10c4893c03da57dce7f607d3e0af46d62e343f1",
     "dcc4c9cbb4e3cec48ab4d1dece9f8708196c5f56",
     "e3910d11f33376097a90858118ba276f7e02cb70",
 }:
  assert digest=="10d76dd2b7b02fe6ce89afa5c122194442ebf77f", (path,digest)
 else:
  assert digest==sha,(path,gitblob(ROOT/path),digest,sha)
shader=(ROOT/"encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp").read_text()
profile=(ROOT/"encore-active/eden-shader/src/shader_recompiler/host_translate_info.h").read_text()
pipeline=(ROOT/"encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp").read_text()
rast=(ROOT/"encore-active/eden-vulkan/vulkan_rasterizer.cpp").read_text()
header=(ROOT/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_rasterizer.h").read_text()
assert shader.count("IsAttributeSourcedHandle(")>=2
assert shader.count("ATTRIBUTE_HANDLE_CBUF_INDEX")>=2
assert "support_attribute_texture_handle{}" in profile
assert "single_lane_geometry_subgroups{}" in profile, "lost subgroup support"
assert "ResolveConditionalPhi(operand, block)" in shader, "lost conditional Phi shader fix"
assert "MaxwellDepthClampClip(" in rast, "lost EDS3 depth clamp"
assert "desc.cbuf_index == Shader::ATTRIBUTE_HANDLE_CBUF_INDEX" in pipeline
assert "attribute_handle_runs.size() > 1" in rast
assert "CanExecuteRecordedDraw()" in rast and "DrawMeshTasksEXT" in rast
assert "struct AttributeHandleRun" in header
start="void RasterizerVulkan::CollectAttributeHandleRuns(bool is_indexed) {"
end="\nvoid RasterizerVulkan::Draw(bool is_indexed, u32 instance_count) {"
assert rast.count(start)==1 and rast.count(end)==1
method=start+rast.split(start,1)[1].split(end,1)[0]
test=r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
using u32=std::uint32_t;
using GPUVAddr=std::uint64_t;
namespace Tegra::Engines {
struct Maxwell3D {
 struct Regs {
  enum class PrimitiveTopology {Triangles, Lines};
  struct VertexAttribute {
   enum class Type { UInt, SInt, Float};
   enum class Size {Invalid, X32};
   struct Buf {u32 i=0;u32 Value() const{return i;}};
   bool constant=true;
   Size size=Size::Invalid;
   Type type=Type::Float;
   Buf buffer{};
   u32 offset=0;
  };
  struct VertexStream {
   bool enabled=false;
   GPUVAddr address=1000;
   u32 stride=4;
   bool IsEnabled() const{return enabled;}
   GPUVAddr Address() const{return address;}
  };
  std::array<VertexAttribute,16> vertex_attrib_format{};
  std::array<VertexStream,16> vertex_streams{};
 } regs;
 struct DrawManager {
  struct DrawState {
   struct Index {
    u32 count=0;
    GPUVAddr IndexStart() const{return 3000;}
    size_t FormatSizeInBytes() const{return 4;}
   } index_buffer;
   struct Vert {u32 count=0, first=0;} vertex_buffer;
   Regs::PrimitiveTopology topology=Regs::PrimitiveTopology::Triangles;
   u32 base_index=0;
  } draw_state;
 } draw_manager;
};
}
namespace Vulkan {
using Maxwell=Tegra::Engines::Maxwell3D::Regs;
struct Memory {
 std::map<GPUVAddr,u32> values;
 template<typename T> T Read(GPUVAddr address) const {
  const auto it=values.find(address);
  return it==values.end()? T{}:static_cast<T>(it->second);
 }
 void ReadBlockUnsafe(GPUVAddr address,void* dest,size_t n) const {
  const u32 value=Read<u32>(address);
  std::memcpy(dest,&value,n);
 }
};
struct RasterizerVulkan {
 struct AttributeHandleRun {u32 first{},count{},handle{};};
 std::vector<AttributeHandleRun> attribute_handle_runs;
 Tegra::Engines::Maxwell3D* maxwell3d{};
 Memory* gpu_memory{};
 void CollectAttributeHandleRuns(bool);
};
""" + method + r"""
}
int main(){
 Tegra::Engines::Maxwell3D engine{};
 Vulkan::Memory mem{};
 Vulkan::RasterizerVulkan raster{};
 raster.maxwell3d=&engine;
 raster.gpu_memory=&mem;
 auto& attr=engine.regs.vertex_attrib_format[0];
 attr.constant=false;attr.size=Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size::X32;attr.type=Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type::UInt;
 auto& stream=engine.regs.vertex_streams[0];stream.enabled=true;
 engine.draw_manager.draw_state.vertex_buffer.count=9;
 for(u32 i=0;i<9;i++) mem.values[1000+i*4]=i<3?11:(i<6?22:11);
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.size()==3);
 assert(raster.attribute_handle_runs[0].first==0 && raster.attribute_handle_runs[0].count==3 &&
        raster.attribute_handle_runs[0].handle==11);
 assert(raster.attribute_handle_runs[1].first==3 && raster.attribute_handle_runs[1].count==3 &&
        raster.attribute_handle_runs[1].handle==22);
 assert(raster.attribute_handle_runs[2].first==6 && raster.attribute_handle_runs[2].count==3 &&
        raster.attribute_handle_runs[2].handle==11);
 raster.attribute_handle_runs.clear();
 engine.draw_manager.draw_state.index_buffer.count=9;
 for(u32 i=0;i<9;i++)mem.values[3000+i*4]=8-i;
 raster.CollectAttributeHandleRuns(true);
 assert(raster.attribute_handle_runs.size()==3);
 assert(raster.attribute_handle_runs[0].handle==11);
 assert(raster.attribute_handle_runs[1].handle==22);
 assert(raster.attribute_handle_runs[2].handle==11);
 raster.attribute_handle_runs.clear();
 engine.draw_manager.draw_state.vertex_buffer.count=0;
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.empty());
 raster.attribute_handle_runs.clear();
 engine.draw_manager.draw_state.vertex_buffer.count=9;
 attr.constant=true;
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.size()==1);
 assert(raster.attribute_handle_runs[0].count==9);
 raster.attribute_handle_runs.clear();
 attr.constant=false;
 engine.draw_manager.draw_state.topology=Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology::Lines;
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.size()==1);
 assert(raster.attribute_handle_runs[0].count==9);
}
""";
cxx = os.getenv("CXX","clang++")
apple_clang_without_lsan = (
    sys.platform == "darwin" and
    "Apple clang" in subprocess.check_output(
        [cxx, "--version"], text=True, stderr=subprocess.STDOUT)
)
with tempfile.TemporaryDirectory(prefix="r23-attribute-handle-") as td:
 d=Path(td)
 (d/"run.cpp").write_text(test)
 subprocess.run([cxx,"-std=c++20","-O1","-g",
                 "-fsanitize=address,undefined","-fno-omit-frame-pointer",
                 str(d/"run.cpp"),"-o",str(d/"run")],check=True,timeout=70)
 subprocess.run([str(d/"run")],check=True,timeout=25,
                env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1:detect_leaks=" + ("0" if apple_clang_without_lsan else "1")})
print("R23_35_VERTEX_ATTRIBUTE_TEXTURE_RUNS_PASS sources=8 paths=6")
