"""Exact post-R23 MSAA handle fallback receipt. Historic blob stays immutable."""
import hashlib
OLD="5b04321fa45d6b5e6af1d4b17bcf695348d0dd90"
NEW="efd77028f0710ea9e50cbbe77b04eebf3f306610"
CURRENT_CPU_RACE="1c40cc3b968c76fc505f5cea63218b93e89b4529"
CURRENT_DRAWTEXTURE="aa3ff8baf97f075d1adf44a9d19335c89403c8e1"
DRAW_TEXTURE_BEFORE="void RasterizerVulkan::DrawTexture() {\n    SCOPE_EXIT { SyncProbe(scheduler, \"draw_texture\", nullptr); };\n\n    SCOPE_EXIT {\n        gpu.TickWork();\n    };\n    FlushWork();\n    // Apply queued guest page remaps before reading cache-owned texture views.\n    // InnerInvalidation itself locks texture_cache.mutex.\n    gpu_memory->FlushCaching();\n\n    std::scoped_lock l{texture_cache.mutex};\n    texture_cache.SynchronizeDescriptors(false);\n    texture_cache.UpdateRenderTargets(false);\n\n#ifdef PS5_NATIVE\n    // Raw blit uses its own viewport/scissor and pipeline. The next\n    // guest draw restores dynamic state after Scheduler::InvalidateState.\n    // Avoid wasted state setup while texture_cache.mutex is held.\n#else\n    UpdateDynamicStates();\n#endif\n\n    query_cache.NotifySegment(true);\n    query_cache.CounterEnable(VideoCommon::QueryType::ZPassPixelCount64, maxwell3d->regs.zpass_pixel_count_enable);\n    const auto& draw_texture_state = maxwell3d->draw_manager.draw_texture_state;\n    const auto& sampler = texture_cache.GetSampler(draw_texture_state.src_sampler, false);\n    const auto& texture = texture_cache.GetImageView(draw_texture_state.src_texture);\n    const auto* framebuffer = texture_cache.GetFramebuffer();\n\n    const bool src_rescaling = texture_cache.IsRescaling() && texture.IsRescaled();\n    const bool dst_rescaling = texture_cache.IsRescaling() && framebuffer->IsRescaled();\n\n    const auto ScaleSrc = [&](auto dim_f) -> s32 {\n        auto dim = static_cast<s32>(dim_f);\n        return src_rescaling ? Settings::values.resolution_info.ScaleUp(dim) : dim;\n    };\n\n    const auto ScaleDst = [&](auto dim_f) -> s32 {\n        auto dim = static_cast<s32>(dim_f);\n        return dst_rescaling ? Settings::values.resolution_info.ScaleUp(dim) : dim;\n    };\n\n    Region2D dst_region = {Offset2D{.x = ScaleDst(draw_texture_state.dst_x0),\n                                    .y = ScaleDst(draw_texture_state.dst_y0)},\n                           Offset2D{.x = ScaleDst(draw_texture_state.dst_x1),\n                                    .y = ScaleDst(draw_texture_state.dst_y1)}};\n    Region2D src_region = {Offset2D{.x = ScaleSrc(draw_texture_state.src_x0),\n                                    .y = ScaleSrc(draw_texture_state.src_y0)},\n                           Offset2D{.x = ScaleSrc(draw_texture_state.src_x1),\n                                    .y = ScaleSrc(draw_texture_state.src_y1)}};\n    Extent3D src_size = {static_cast<u32>(ScaleSrc(texture.size.width)),\n                         static_cast<u32>(ScaleSrc(texture.size.height)), texture.size.depth};\n    blit_image.BlitColor(framebuffer, texture.RenderTarget(), texture.ImageHandle(),\n                         sampler->Handle(), dst_region, src_region, src_size);\n#ifdef PS5_NATIVE\n    // Raw-image blit directly binds its own Vulkan graphics pipeline.\n    // It does not invalidate Scheduler's cached guest graphics state.\n    // Ensure the following guest draw rebinds pipeline and descriptors.\n    scheduler.InvalidateState();\n#endif\n}\n"
DRAW_TEXTURE_AFTER="void RasterizerVulkan::DrawTexture() {\n    SCOPE_EXIT { SyncProbe(scheduler, \"draw_texture\", nullptr); };\n\n    SCOPE_EXIT {\n        gpu.TickWork();\n    };\n    FlushWork();\n    // Apply queued guest page remaps before reading cache-owned texture views.\n    // InnerInvalidation itself locks texture_cache.mutex.\n    gpu_memory->FlushCaching();\n\n    std::scoped_lock l{texture_cache.mutex};\n    texture_cache.SynchronizeDescriptors(false);\n    texture_cache.UpdateRenderTargets(false);\n\n#ifdef PS5_NATIVE\n    // Raw blit uses its own viewport/scissor and pipeline. The next\n    // guest draw restores dynamic state after Scheduler::InvalidateState.\n    // Avoid wasted state setup while texture_cache.mutex is held.\n#else\n    UpdateDynamicStates();\n#endif\n\n    query_cache.NotifySegment(true);\n    query_cache.CounterEnable(VideoCommon::QueryType::ZPassPixelCount64, maxwell3d->regs.zpass_pixel_count_enable);\n    const auto& draw_texture_state = maxwell3d->draw_manager.draw_texture_state;\n    const auto& sampler = texture_cache.GetSampler(draw_texture_state.src_sampler, false);\n    const auto& texture = texture_cache.GetImageView(draw_texture_state.src_texture);\n    const auto* framebuffer = texture_cache.GetFramebuffer();\n\n    const bool src_rescaling = texture_cache.IsRescaling() && texture.IsRescaled();\n    const bool dst_rescaling = texture_cache.IsRescaling() && framebuffer->IsRescaled();\n\n    const auto ScaleSrc = [&](auto dim_f) -> s32 {\n        auto dim = static_cast<s32>(dim_f);\n        return src_rescaling ? Settings::values.resolution_info.ScaleUp(dim) : dim;\n    };\n\n    const auto ScaleDst = [&](auto dim_f) -> s32 {\n        auto dim = static_cast<s32>(dim_f);\n        return dst_rescaling ? Settings::values.resolution_info.ScaleUp(dim) : dim;\n    };\n\n    Region2D dst_region = {Offset2D{.x = ScaleDst(draw_texture_state.dst_x0),\n                                    .y = ScaleDst(draw_texture_state.dst_y0)},\n                           Offset2D{.x = ScaleDst(draw_texture_state.dst_x1),\n                                    .y = ScaleDst(draw_texture_state.dst_y1)}};\n    Region2D src_region = {Offset2D{.x = ScaleSrc(draw_texture_state.src_x0),\n                                    .y = ScaleSrc(draw_texture_state.src_y0)},\n                           Offset2D{.x = ScaleSrc(draw_texture_state.src_x1),\n                                    .y = ScaleSrc(draw_texture_state.src_y1)}};\n    Extent3D src_size = {static_cast<u32>(ScaleSrc(texture.size.width)),\n                         static_cast<u32>(ScaleSrc(texture.size.height)), texture.size.depth};\n\n    std::optional<VkRect2D> scissor;\n    if (maxwell3d->regs.scissor_test[0].enable) {\n        u32 up_scale = 1;\n        u32 down_shift = 0;\n        if (dst_rescaling) {\n            up_scale = Settings::values.resolution_info.up_scale;\n            down_shift = Settings::values.resolution_info.down_shift;\n        }\n        scissor = GetScissorState(maxwell3d->regs, 0, up_scale, down_shift);\n    }\n\n    const auto& fb_images = framebuffer->Images();\n    const auto fb_end = fb_images.begin() + framebuffer->NumImages();\n    const bool src_is_target =\n        std::find(fb_images.begin(), fb_end, texture.ImageHandle()) != fb_end;\n    if (src_is_target && src_rescaling == dst_rescaling &&\n        src_region.start.x == dst_region.start.x && src_region.start.y == dst_region.start.y &&\n        src_region.end.x == dst_region.end.x && src_region.end.y == dst_region.end.y &&\n        maxwell3d->regs.blend.enable[0] == 0) {\n        return;\n    }\n\n    blit_image.BlitColor(framebuffer, texture.RenderTarget(), texture.ImageHandle(),\n                         sampler->Handle(), dst_region, src_region, src_size, scissor);\n}\n"
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
    if digest==CURRENT_DRAWTEXTURE:
        assert source.count("#include <optional>\n")==1
        assert source.count(DRAW_TEXTURE_AFTER)==1 and DRAW_TEXTURE_BEFORE not in source
        source=source.replace("#include <optional>\n","",1).replace(DRAW_TEXTURE_AFTER,DRAW_TEXTURE_BEFORE,1)
        digest=blob(source.encode())
        assert digest==CURRENT_CPU_RACE, ("DrawTexture normalization changed unrelated rasterizer bytes",digest)
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

# The selected FC27 texture owner adds one reviewed Vulkan depth-compare type fix.
# Replay its exact one-hunk inverse before accepting the historic R23 receipt.
def verified_depth_compare_sha(path):
    raw=path.read_bytes()
    actual=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
    if actual != "4301e9dbc19c4e07e7c24af23177cf5fa2142f1a":
        return actual
    newer="                // Depth-compare sampling returns floating-point values in Vulkan even when the\n                // TIC format itself is integer or unknown. Never build an integer sampled image.\n                const bool is_integer{flags.is_depth == 0 && is_texture_pixel_format_integer(cbuf)};"
    prior="                const bool is_integer{is_texture_pixel_format_integer(cbuf)};"
    selected=raw.decode("utf-8")
    assert selected.count(newer)==1 and prior not in selected
    historical=selected.replace(newer,prior,1).encode("utf-8")
    previous=hashlib.sha1(b"blob "+str(len(historical)).encode()+bytes([0])+historical
    ).hexdigest()
    assert previous=="30c9a612a88fa6cd832a40ad0f717b4944db45a1", ("Unexpected depth-compare predecessor drift",previous)
    return previous

