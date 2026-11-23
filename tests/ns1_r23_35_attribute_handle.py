#!/usr/bin/env python3
"""NS1 R23 #35: 8 maintained source owner receipts and real draw-run method ASan."""
from pathlib import Path
from native_msaa_handle_receipt import restore_pre_rebind, verified_depth_compare_sha, restore_integrated_rasterizer, INTEGRATED_RASTERIZER, GUEST_CACHE_LOCK_SUCCESSOR
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
# Reviewed FC27 successor: pin each complete changed production blob
# while retaining the immutable historical R23 oracle for unchanged owners.
CURRENT_ATTRIBUTE_SOURCE={
  "encore-active/eden-shader/src/shader_recompiler/shader_info.h": "b0ec194089a0ec9ebb0ff46461f4607c3a7ba889",
  "encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp": "4301e9dbc19c4e07e7c24af23177cf5fa2142f1a",
  "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_graphics_pipeline.h": "c1ed0200afd3666056dfe0ef60705015a923dac7",
  "encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp": "df36d210d04411b87e1fdad7fbd0689b1e4d3488",
  "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_rasterizer.h": "6e0bf7ab2a3a2147f9db0c630070db30d9b35d56",
  "encore-active/eden-vulkan/vulkan_rasterizer.cpp": GUEST_CACHE_LOCK_SUCCESSOR,
}
# Two byte-sealed PS5-only transition-hint changes sit on TOP of the
# original FC27 attribute-handle successor. Reconstruct its exact existing
# receipt instead of treating the new optional hint cache as an R23 change.
BOUNDED_GRAPHICS_REVERSE={
  "encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp":("f3c82781675b6979a8be96a333184e048ef57f81","2834a18fe7638749ec83cc8f5feb1fcde6c71a9a","void GraphicsPipeline::AddTransition(GraphicsPipeline* transition) {\n#ifdef PS5_NATIVE\n    // Only a fast-path hint: the canonical graphics_cache still owns ALL\n    // pipelines. A pipeline with hundreds of different successors must not\n    // accumulate unbounded state or scan every former successor on each miss.\n    // The two HUD/overlay hot indices remain pinned across ring replacement.\n    if (transition_keys.size() >= kNativeTransitionHintLimit) {\n        size_t slot = next_transition_replacement;\n        for (size_t skipped = 0; skipped < 2; ++skipped) {\n            if (slot != hot_transition_indices[0] &&\n                slot != hot_transition_indices[1]) break;\n            slot = (slot + 1) % kNativeTransitionHintLimit;\n        }\n        // Both vectors are already fully allocated here. Replacing a hint\n        // cannot invalidate a live shader pipeline or allocate GPU objects.\n        transition_keys[slot] = transition->key;\n        transitions[slot] = transition;\n        next_transition_replacement = (slot + 1) % kNativeTransitionHintLimit;\n        return;\n    }\n    // Before the hint ring is full, preserve the exception-safe pairing\n    // of keys and pointers on the GPU owner.\n    try {\n        transition_keys.push_back(transition->key);\n    } catch (...) {\n        return;\n    }\n#else\n    transition_keys.push_back(transition->key);\n#endif\n#ifdef PS5_NATIVE\n    try {\n        transitions.push_back(transition);\n    } catch (...) {\n        transition_keys.pop_back();\n        return;\n    }\n#else\n    transitions.push_back(transition);\n#endif\n}","void GraphicsPipeline::AddTransition(GraphicsPipeline* transition) {\n#ifdef PS5_NATIVE\n    // Both appends are optional memoization. The canonical graphics cache\n    // already owns the target and will resolve a missing edge on demand.\n    try {\n        transition_keys.push_back(transition->key);\n    } catch (...) {\n        return;\n    }\n#else\n    transition_keys.push_back(transition->key);\n#endif\n#ifdef PS5_NATIVE\n    // Both arrays must describe the same transition index. An allocation\n    // failure while appending the pointer must not leave an orphaned key:\n    // Next() would later index past transitions and corrupt a guest draw.\n    try {\n        transitions.push_back(transition);\n    } catch (...) {\n        // Transition memoization is optional. The primary graphics cache\n        // already owns the pipeline; a later lookup can use its slow path.\n        // Do not abort a guest draw solely because this edge could not fit.\n        transition_keys.pop_back();\n        return;\n    }\n#else\n    transitions.push_back(transition);\n#endif\n}"),
  "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_graphics_pipeline.h":("cc53133dd516e2d1d245326866ab3ce0c852b36b","515a766a6cc4d6fcba7692ad396f45396b746d28","    // Transition hints are not the pipeline owners. Limit their memory\n    // growth and the O(N) unsuccessful Next() search on shader-heavy titles.\n    // The canonical graphics_cache remains complete and handles all misses.\n    static constexpr size_t kNativeTransitionHintLimit = 64;\n    // GPU owner only: the two hot indices stay valid across vector growth\n    // and are never replaced when the bounded ring rolls over.\n    std::array<size_t, 2> hot_transition_indices{};\n    size_t next_transition_replacement{};\n","    // GPU owner only. Two valid indices remain stable across vector growth.\n    std::array<size_t, 2> hot_transition_indices{};\n"),
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
 if path in CURRENT_ATTRIBUTE_SOURCE:
  current=gitblob(ROOT/path)
  assert current==CURRENT_ATTRIBUTE_SOURCE[path], ("FC27 maintained successor drift",path)
  if path.endswith("/vk_rasterizer.h"):
   selected=raw.decode()
   lines=selected.split("\n")
   inverse=(
    (65, ["                           Scheduler& scheduler, Tegra::MemoryManager* const& gpu_memory);","","    bool BufferCopy(GPUVAddr start_address, GPUVAddr end_address, u64 amount) override;",""], ["                           Scheduler& scheduler);","","    bool BufferCopy(GPUVAddr start_address, GPUVAddr end_address, u64 amount) override;",""]),
    (86, ["    // The GPU address space changes on channel binds. Reference the","    // rasterizer's active pointer, not a stale construction-time channel.","    Tegra::MemoryManager* const& gpu_memory;","};","","class RasterizerVulkan final : public VideoCore::RasterizerInterface,"], ["};","","class RasterizerVulkan final : public VideoCore::RasterizerInterface,"]),
    (222, ["    void UpdateBlending(Tegra::Engines::Maxwell3D::Regs& regs,","                        const GraphicsPipeline* pipeline);","    void UpdateColorWriteEnable(Tegra::Engines::Maxwell3D::Regs& regs);","","    void UpdateVertexInput(Tegra::Engines::Maxwell3D::Regs& regs);"], ["    void UpdateBlending(Tegra::Engines::Maxwell3D::Regs& regs);","    void UpdateColorWriteEnable(Tegra::Engines::Maxwell3D::Regs& regs);","","    void UpdateVertexInput(Tegra::Engines::Maxwell3D::Regs& regs);"]),
    (269, ["","#ifdef PS5_NATIVE","    // Cached once per native rasterizer, never queried on each hot-path QMD."], ["    vk::Event wfi_event;","","#ifdef PS5_NATIVE","    // Cached once per native rasterizer, never queried on each hot-path QMD."]),
    (280, ["    u32 last_color_output_mask{};","    bool mesh_draw{}; ///< the draw being prepared uses a task/mesh pipeline","};",""], ["    bool mesh_draw{}; ///< the draw being prepared uses a task/mesh pipeline","};",""]),
   )
   for index,new,prior in reversed(inverse):
    assert lines[index:index+len(new)]==new, "Integrated rasterizer header hunk drift"
    lines[index:index+len(new)]=prior
   predecessor="\n".join(lines).encode()
   digest=hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()
   assert digest=="6467fd4d704de6b77deb7e6cba4b8e33f70f5a08", ("Rasterizer header predecessor drift",digest)
  if path.endswith("/shader_info.h"):
   old_source=raw.decode()
   assert old_source.count("    bool uses_sample_mask{};\n")==1
   old_source=old_source.replace("    bool uses_sample_mask{};\n", "", 1)
   assert old_source.count("    /// Components (bit 0 = R ... bit 3 = A) of each render target the fragment shader writes.\n    std::array<u8, 8> frag_color_components{};\n")==1
   old_source=old_source.replace("    /// Components (bit 0 = R ... bit 3 = A) of each render target the fragment shader writes.\n    std::array<u8, 8> frag_color_components{};\n", "", 1)
   data=old_source.encode()
   assert hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()=="d847e7485a9657a6a016354fd70fb423112857ff"
  if path.endswith("/texture_pass.cpp"):
   assert verified_depth_compare_sha(ROOT/path)=="30c9a612a88fa6cd832a40ad0f717b4944db45a1"
  if path.endswith("/vulkan_rasterizer.cpp"):
   restored=restore_integrated_rasterizer(raw.decode())
   assert gitblob(ROOT/path)==GUEST_CACHE_LOCK_SUCCESSOR
   data=restored.encode()
   assert hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()=="71d097b9a83028dfad9370792c905bb3bfccfd5a"
  if path in BOUNDED_GRAPHICS_REVERSE:
   new_sha, old_sha, new_hunk, old_hunk = BOUNDED_GRAPHICS_REVERSE[path]
   current_text=raw.decode("utf-8")
   if path.endswith("/vk_graphics_pipeline_cost.cpp"):
    assert current_text.count("    // Maxwell leaves render-target components not written by the fragment shader untouched.\n    // Vulkan otherwise permits undefined values there, which is visible on AMD/RADV.\n    for (size_t rt = 0; rt < Maxwell::NumRenderTargets; ++rt) {\n        color_output_mask |= u32{stage_infos[NUM_STAGES - 1].frag_color_components[rt]}\n                             << (rt * 4);\n    }\n")==1
    current_text=current_text.replace("    // Maxwell leaves render-target components not written by the fragment shader untouched.\n    // Vulkan otherwise permits undefined values there, which is visible on AMD/RADV.\n    for (size_t rt = 0; rt < Maxwell::NumRenderTargets; ++rt) {\n        color_output_mask |= u32{stage_infos[NUM_STAGES - 1].frag_color_components[rt]}\n                             << (rt * 4);\n    }\n", "", 1)
    assert current_text.count("        write_mask &= ColorOutputMask(index);\n")==1
    current_text=current_text.replace("        write_mask &= ColorOutputMask(index);\n", "", 1)
   elif path.endswith("/vk_graphics_pipeline.h"):
    assert current_text.count("    [[nodiscard]] VkColorComponentFlags ColorOutputMask(size_t render_target) const noexcept {\n        return (color_output_mask >> (render_target * 4)) & 0xF;\n    }\n\n    [[nodiscard]] u32 ColorOutputMask() const noexcept {\n        return color_output_mask;\n    }\n\n")==1
    current_text=current_text.replace("    [[nodiscard]] VkColorComponentFlags ColorOutputMask(size_t render_target) const noexcept {\n        return (color_output_mask >> (render_target * 4)) & 0xF;\n    }\n\n    [[nodiscard]] u32 ColorOutputMask() const noexcept {\n        return color_output_mask;\n    }\n\n", "", 1)
    assert current_text.count("    u32 color_output_mask{};\n")==1
    current_text=current_text.replace("    u32 color_output_mask{};\n", "", 1)
   data=current_text.encode("utf-8")
   digest=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
   assert digest==new_sha, ("Integrated color-output ABI mismatch",path,digest,new_sha)
   assert current_text.count(new_hunk)==1
   prior_text=current_text.replace(new_hunk,old_hunk,1)
   if path.endswith("/vk_graphics_pipeline_cost.cpp"):
    # Reverse the independent build-task lifetime fix on the audit COPY.
    lifetime_new = ("    // The build callback publishes is_built before it returns. Cache teardown\n"
                    "    // may then destroy this pipeline; never access its members afterward.\n"
                    "    // Transfer ownership while this instance is still guaranteed alive.\n"
                    "    auto task = std::move(build_task);\n"
                    "    build_task = nullptr;\n"
                    "    task();")
    lifetime_old = "    build_task();\n    build_task = nullptr;"
    assert prior_text.count(lifetime_new) == 1
    prior_text = prior_text.replace(lifetime_new, lifetime_old, 1)
   prior = prior_text.encode("utf-8")
   assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()==old_sha, ("FC27 graphics hint predecessor drift", path)
   assert "kNativeTransitionHintLimit = 64" in raw.decode("utf-8") or "next_transition_replacement" in raw.decode("utf-8")
  continue
 if path.endswith("/vk_pipeline_cache_cost.cpp") and gitblob(ROOT/path) in {"b28a190e92ca80b0385148a0b558828353326f12","cfac496d574010b8121611c8616d38bf7cdcbc41","a0e2a45b9b0f080b739d97abb92e9485a0063f73","79660c5cc4ff046f24e7ffa50ab066a86857698c","d222fb5565cdf8db30c6d3d464f107ff26a042c8"}:
  # Reviewed newest-32 native prewarm overlay. The complete current shader
  # owner is cryptographically pinned; the engine's Vulkan/R23 audit separately
  # reverses its entire exact LoadDiskResources hunk to the historical blob.
  # This older attribute-handle test should still reject unknown sources,
  # but must not mistake a bounded shader reader change for an ABI change.
  selected=raw.decode("utf-8")
  cache_blob_current=gitblob(ROOT/path)
  if cache_blob_current in {"b28a190e92ca80b0385148a0b558828353326f12","79660c5cc4ff046f24e7ffa50ab066a86857698c","d222fb5565cdf8db30c6d3d464f107ff26a042c8"}:
   from ps5_recent_prewarm_receipt import reverse_adaptive_prewarm
   selected=reverse_adaptive_prewarm(selected)
   cache_blob_current="a0e2a45b9b0f080b739d97abb92e9485a0063f73"
  if cache_blob_current=="a0e2a45b9b0f080b739d97abb92e9485a0063f73":
   newer="        // Vulkan requires signed/unsigned fragment outputs to match the\n        // attachment's numeric class. Previously this was enabled only on\n        // MoltenVK: native PS5 RADV would emit floats for integer targets,\n        // making color writes poison/undefined (Vulkan shader interfaces).\n        // Keep the existing desktop policy; fix the PS5 renderer's ABI.\n        const bool require_typed_fragment_outputs =\n#ifdef PS5_NATIVE\n            true;\n#else\n            device.IsMoltenVK();\n#endif\n        if (require_typed_fragment_outputs) {"
   older="        if (device.IsMoltenVK()) {"
   assert selected.count(newer)==1
   predecessor=selected.replace(newer,older,1).encode()
   assert hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()=="cfac496d574010b8121611c8616d38bf7cdcbc41"
  assert "constexpr u32 CACHE_VERSION = 21;" in selected
  assert "kStartupPrewarmPipelines = 32;" in selected
  assert "kRecentShaderCacheScanLimit = 64ULL << 20;" in selected
  assert "recent_cache_builds.pop_front();" in selected
  assert "workers.WaitForRequests();" in selected
  assert "fragment_shuffle_fallback[index] = true;" in selected
  continue
 normalized=raw.decode("utf-8")
 if path.endswith("/vulkan_rasterizer.cpp"):
  normalized=restore_pre_rebind(normalized)
  assert normalized.count(POST_TEARDOWN)==1, "FC27 lifetime guard missing"
  normalized=normalized.replace(POST_TEARDOWN,PRE_TEARDOWN,1)
 elif path.endswith("/texture_pass.cpp") and gitblob(ROOT/path)=="3d9625f1981d6500e7b294018e0659464869d6f3":
  post_or="        // A dynamic texture table may be ORed with one fixed sampler word.\n        // Keep the table as the primary operand so its shader-side index and\n        // descriptor count survive. Two dynamic tables still cannot be\n        // represented by the current descriptor ABI.\n        if (lhs->count > 1 && rhs->count > 1) {\n            return std::nullopt;\n        }\n        if (rhs->count > 1 ||\n            (lhs->count == 1 &&\n             (lhs->shift_left > 0 || lhs->index > rhs->index || lhs->offset > rhs->offset))) {\n            std::swap(lhs, rhs);\n        }\n        return ConstBufferAddr{\n            .index = lhs->index,\n            .offset = lhs->offset,\n            .shift_left = lhs->shift_left,\n            .secondary_index = rhs->index,\n            .secondary_offset = rhs->offset,\n            .secondary_shift_left = rhs->shift_left,\n            .dynamic_offset = lhs->dynamic_offset,\n            .count = lhs->count,\n            .has_secondary = true,\n        };"
  pre_or="        if (lhs->count > 1 || rhs->count > 1) {\n            return std::nullopt;\n        }\n        if (lhs->shift_left > 0 || lhs->index > rhs->index || lhs->offset > rhs->offset) {\n            std::swap(lhs, rhs);\n        }\n        return ConstBufferAddr{\n            .index = lhs->index,\n            .offset = lhs->offset,\n            .shift_left = lhs->shift_left,\n            .secondary_index = rhs->index,\n            .secondary_offset = rhs->offset,\n            .secondary_shift_left = rhs->shift_left,\n            .dynamic_offset = {},\n            .count = 1,\n            .has_secondary = true,\n        };"
  post_zero="    IR::Inst* const offset_inst{offset.InstRecursive()};\n    u32 base_offset{};\n    IR::U32 dynamic_offset{offset};\n    if (offset_inst->GetOpcode() == IR::Opcode::IAdd32) {\n        if (offset_inst->Arg(0).IsImmediate()) {\n            base_offset = offset_inst->Arg(0).U32();\n            dynamic_offset = IR::U32{offset_inst->Arg(1)};\n        } else if (offset_inst->Arg(1).IsImmediate()) {\n            base_offset = offset_inst->Arg(1).U32();\n            dynamic_offset = IR::U32{offset_inst->Arg(0)};\n        } else {\n            return std::nullopt;\n        }\n    } else if (offset_inst->GetOpcode() != IR::Opcode::ShiftLeftLogical32) {\n        return std::nullopt;\n    }"
  pre_zero="    IR::Inst* const offset_inst{offset.InstRecursive()};\n    if (offset_inst->GetOpcode() != IR::Opcode::IAdd32) {\n        return std::nullopt;\n    }\n    u32 base_offset{};\n    IR::U32 dynamic_offset;\n    if (offset_inst->Arg(0).IsImmediate()) {\n        base_offset = offset_inst->Arg(0).U32();\n        dynamic_offset = IR::U32{offset_inst->Arg(1)};\n    } else if (offset_inst->Arg(1).IsImmediate()) {\n        base_offset = offset_inst->Arg(1).U32();\n        dynamic_offset = IR::U32{offset_inst->Arg(0)};\n    } else {\n        return std::nullopt;\n    }"
  assert normalized.count(post_or)==1 and normalized.count(post_zero)==1
  normalized=normalized.replace(post_or,pre_or,1).replace(post_zero,pre_zero,1)
 elif path.endswith("/vk_rasterizer.h") and gitblob(ROOT/path)=="7c754c82c12c4f4c934abd3d7f2cdb25222e8d2a":
  # FC27/Lichii #397 adds one batch hook. Remove exactly that declaration
  # before checking the frozen R23 header receipt.
  cpu_race_decl="    void OnCacheInvalidations(std::span<const std::pair<DAddr, u64>> ranges) override;\n"
  assert normalized.count(cpu_race_decl)==1
  normalized=normalized.replace(cpu_race_decl,"",1)
  restored=normalized.encode("utf-8")
  assert hashlib.sha1(b"blob "+str(len(restored)).encode()+bytes([0])+restored).hexdigest()==EXPECTED[path]
 elif path.endswith("/vk_graphics_pipeline_cost.cpp") and gitblob(ROOT/path)=="efa8fd4d73c111655fced67fe7dd1e6b7c0178ba":
  fixed="const u32 second_offset{desc.secondary_cbuf_offset};"
  indexed="const u32 second_offset{desc.secondary_cbuf_offset + index_offset};"
  assert normalized.count(fixed)==1 and indexed not in normalized
  normalized=normalized.replace(fixed,indexed,1)
 elif path.endswith("/vk_pipeline_cache_cost.cpp"):
  # FC27 XMAD pointer folding changes generated shader IR, so the selected
  # cache owner advances 19 -> 20. Reverse only that reviewed version bump
  # in the audit copy before reconstructing the frozen R23 receipt.
  raw_cache_blob=gitblob(ROOT/path)
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
assert "desc.cbuf_index != Shader::ATTRIBUTE_HANDLE_CBUF_INDEX" in pipeline
assert "attribute_handle_source = desc.cbuf_offset;" in pipeline
assert ".offset = *attribute_source" in shader
assert "Generic31W" in shader
assert "attribute_handle_source = pipeline->AttributeHandleSource();" in rast
assert "attribute_handle_source.reset();" in rast
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
#include <optional>
#include <vector>
using u32=std::uint32_t;
using GPUVAddr=std::uint64_t;
namespace Shader::IR {
enum class Attribute : u32 { Generic0X=32, Generic31W=159 };
}
namespace Tegra::Engines {
struct Maxwell3D {
 struct Regs {
  enum class PrimitiveTopology {Triangles, Lines};
  struct VertexAttribute {
   enum class Type { UInt, SInt, Float};
   enum class Size {Invalid, Size_R32, Size_R32_G32, Size_R32_G32_B32, Size_R32_G32_B32_A32};
   u32 ComponentCount() const {
     switch(size) {
     case Size::Size_R32: return 1;
     case Size::Size_R32_G32: return 2;
     case Size::Size_R32_G32_B32: return 3;
     case Size::Size_R32_G32_B32_A32: return 4;
     default: return 0;
     }
   }
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
 std::optional<u32> attribute_handle_source{};
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
 attr.constant=false;attr.size=Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size::Size_R32;attr.type=Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type::UInt;
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
 // A different integer attribute precedes the shader's real handle.
 auto& other=engine.regs.vertex_attrib_format[1];
 other.constant=false;
 other.size=Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size::Size_R32;
 other.type=Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type::UInt;
 other.buffer.i=1;
 auto& other_stream=engine.regs.vertex_streams[1];
 other_stream.enabled=true;
 other_stream.address=2000;
 for(u32 i=0;i<9;i++) mem.values[2000+i*4]=i<3?41:(i<6?42:41);
 raster.attribute_handle_source=36;
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.size()==3);
 assert(raster.attribute_handle_runs[0].handle==41);
 assert(raster.attribute_handle_runs[1].handle==42);
 assert(raster.attribute_handle_runs[2].handle==41);
 raster.attribute_handle_runs.clear();
 // A known shader source must not alias another integer attribute when
 // its own slot is missing or has an unsupported format.
 raster.attribute_handle_source=40; // Generic2X: no valid vertex slot
 raster.CollectAttributeHandleRuns(false);
 assert(raster.attribute_handle_runs.size()==1);
 assert(raster.attribute_handle_runs[0].count==9 &&
        raster.attribute_handle_runs[0].handle==0);
 raster.attribute_handle_runs.clear();
 raster.attribute_handle_source.reset();
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
