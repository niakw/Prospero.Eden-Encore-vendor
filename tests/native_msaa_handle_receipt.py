"""Exact post-R23 MSAA handle fallback receipt. Historic blob stays immutable."""
import hashlib
OLD="5b04321fa45d6b5e6af1d4b17bcf695348d0dd90"
NEW="efd77028f0710ea9e50cbbe77b04eebf3f306610"
CURRENT_CPU_RACE="1c40cc3b968c76fc505f5cea63218b93e89b4529"
CPU_RACE_CONSTRUCTOR="""    buffer_cache.SetPendingCpuWrites(
        [this](const std::function<void(DAddr, size_t)>& callback) {
            gpu.ForEachPendingCpuWrite(callback);
        });
"""
CPU_RACE_BATCH="""void RasterizerVulkan::OnCacheInvalidations(std::span<const std::pair<DAddr, u64>> ranges) {
    // Remove the complete CPU-write batch from GPU-owned ranges before any
    // individual invalidation can download stale bytes over a later write.
    {
        std::scoped_lock lock{buffer_cache.mutex};
        buffer_cache.ClaimCpuWrites(ranges);
    }
    for (const auto& [addr, size] : ranges) OnCacheInvalidation(addr, size);
}

"""
BEFORE="    // Mesh draw workgroups are not indexed vertices. Only standard vertex\n    // draw calls may be split into handle-homogeneous runs.\n    attribute_handle_pipeline = nullptr;\n    attribute_handle_runs.clear();\n    if (split_attribute_handles && !mesh_draw && pipeline->UsesAttributeHandle()) {\n        CollectAttributeHandleRuns(is_indexed);\n        if (!attribute_handle_runs.empty()) {\n            attribute_handle_pipeline = pipeline;\n            pipeline->SetAttributeHandle(attribute_handle_runs.front().handle);\n        }\n    }\n    bool pipeline_ok = pipeline->Configure(is_indexed);\n    if (!pipeline_ok && pipeline->UsesForcedMsaaShadow()) {\n        // The shader may read/store its destination or the texture cache\n        // may have ended the shadow pass while binding descriptors.\n        // Resolve and RETRY this draw via the unchanged guest 1x pipeline.\n        if (native_msaa_shadow) (void)native_msaa_shadow->EndPrepared();\n        native_msaa_clear.reset();\n        native_samples = VK_SAMPLE_COUNT_1_BIT;\n        pipeline = pipeline_cache.CurrentGraphicsPipeline();\n        if (pipeline) {\n            prepared_pipeline = pipeline;\n            probe_pipeline = pipeline;\n            pipeline->SetEngine(maxwell3d, gpu_memory);\n            pipeline_ok = pipeline->Configure(is_indexed);\n        }\n    }\n"
AFTER="    // Mesh draw workgroups are not indexed vertices. Only standard vertex\n    // draw calls may be split into handle-homogeneous runs.\n    const auto prepare_attribute_handles = [&] {\n        attribute_handle_pipeline = nullptr;\n        attribute_handle_runs.clear();\n        if (split_attribute_handles && !pipeline->IsMeshPipeline() && pipeline->UsesAttributeHandle()) {\n            CollectAttributeHandleRuns(is_indexed);\n            if (!attribute_handle_runs.empty()) {\n                attribute_handle_pipeline = pipeline;\n                pipeline->SetAttributeHandle(attribute_handle_runs.front().handle);\n            }\n        }\n    };\n    prepare_attribute_handles();\n    bool pipeline_ok = pipeline->Configure(is_indexed);\n    if (!pipeline_ok && pipeline->UsesForcedMsaaShadow()) {\n        // The shader may read/store its destination or the texture cache\n        // may have ended the shadow pass while binding descriptors.\n        // Resolve and RETRY this draw via the unchanged guest 1x pipeline.\n        if (native_msaa_shadow) (void)native_msaa_shadow->EndPrepared();\n        native_msaa_clear.reset();\n        native_samples = VK_SAMPLE_COUNT_1_BIT;\n        pipeline = pipeline_cache.CurrentGraphicsPipeline();\n        if (pipeline) {\n            prepared_pipeline = pipeline;\n            probe_pipeline = pipeline;\n            pipeline->SetEngine(maxwell3d, gpu_memory);\n            // The 1x pipeline owns its own descriptor/attribute state. The\n            // rejected shadow is not a valid owner for later split draws.\n            prepare_attribute_handles();\n            pipeline_ok = pipeline->Configure(is_indexed);\n        }\n    }\n"
def blob(raw):
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()
def restore_pre_rebind(source):
    digest=blob(source.encode())
    if digest==CURRENT_CPU_RACE:
        assert source.count(CPU_RACE_CONSTRUCTOR)==1
        assert source.count(CPU_RACE_BATCH)==1
        source=source.replace(CPU_RACE_CONSTRUCTOR,"",1).replace(CPU_RACE_BATCH,"",1)
        digest=blob(source.encode())
        assert digest==NEW, ("CPU-write race normalization changed unrelated rasterizer bytes",digest)
    if digest==OLD:
        return source
    assert digest==NEW, ("unreviewed rasterizer change",digest)
    assert source.count(AFTER)==1 and BEFORE not in source
    restored=source.replace(AFTER,BEFORE,1)
    assert blob(restored.encode())==OLD
    return restored
