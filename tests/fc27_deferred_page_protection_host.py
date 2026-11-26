#!/usr/bin/env python3
"""FC27 #194 safety rollback: compile the exact synchronous rasterizer CPU-write owner.

This historical test path deliberately remains for the existing CI callers.
Do not re-enable deferred cached-page untracking without a PS5-safe scheduler
ownership gate matching upstream Lichii #1518 and console visual evidence.
"""
from pathlib import Path
from native_msaa_handle_receipt import restore_integrated_rasterizer, INTEGRATED_RASTERIZER, GUEST_CACHE_LOCK_SUCCESSOR
import hashlib
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
R=ROOT/"encore-active/eden-vulkan/vulkan_rasterizer.cpp"
D=ROOT/"encore-active/eden-core/overrides/core/device_memory_manager.inc"
T=ROOT/"encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h"
def blob(path):
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\x00"+raw).hexdigest()
# Current maintained runtime owners. The CPU-write rollback still requires
# synchronous protection even though DMA clean-image invalidation was added.
# NS2 adds only the strict Ampere syncpoint-fence wrapper to this TU; strip
# that exact method before replaying the historical FC27 rasterizer receipt.
NATIVE_SYNC_BLOCK="""#ifdef PS5_NATIVE
bool RasterizerVulkan::SignalNativeSyncPoint(u32 value) {
    fence_manager.SignalNativeSyncPoint(value);
    return true;
}
#endif

"""
raster_text=R.read_text()
def text_blob(text):
    raw=text.encode()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\x00"+raw).hexdigest()
# The indirect argument read does not change synchronous page protection.
# Keep the synchronous write-protection receipt independent of readback extent.
if text_blob(raster_text) == "39b2ed67e51937aecad666de4113ce792d32132d":
    newer, older = "VideoCore::RasterizerDownloadArea RasterizerVulkan::GetFlushArea(DAddr addr, u64 size) {\n    ::Eden::Performance::DiagnosticTimer guest_read_timer(::Eden::Performance::guest_cpu_read);\n    const VideoCore::RasterizerDownloadArea clean_area{\n        .start_address = Common::AlignDown(addr, Core::DEVICE_PAGESIZE),\n        .end_address = Common::AlignUp(addr + size, Core::DEVICE_PAGESIZE),\n        .preemtive = true,\n    };\n    // A clean/never-held texture page does not prove that a GPU-written\n    // storage buffer is current in guest memory. Query each cache independently:\n    // never-owned pages remain lock-free, and no two cache locks are nested.\n    std::optional<VideoCore::RasterizerDownloadArea> texture_area;\n    if (!::Eden::ImagePages::NeverHeld(addr, size)) {\n        ::Eden::Performance::GuestCacheLock(texture_cache.mutex, true);\n        std::lock_guard lock{texture_cache.mutex, std::adopt_lock};\n        texture_area = texture_cache.GetFlushArea(addr, size);\n    }\n    if (texture_area && !texture_area->preemtive) {\n        // The existing GPU sync flushes both cache domains.\n        return *texture_area;\n    }\n    auto area = texture_area.value_or(clean_area);\n    // Core::Memory caches the returned interval, not just the original\n    // scalar read. A preemptive texture can extend over other GPU-written\n    // buffers. Check EVERY byte the caller may subsequently read without\n    // re-entering GetFlushArea. Keep the original texture extent when clean.\n    const DAddr buffer_start = area.start_address;\n    const u64 buffer_size = area.end_address - buffer_start;\n    if (!::Eden::BufferPages::NeverRegistered(buffer_start, buffer_size)) {\n        ::Eden::Performance::GuestCacheLock(buffer_cache.mutex, false);\n        std::lock_guard lock{buffer_cache.mutex, std::adopt_lock};\n        if (buffer_cache.IsRegionGpuModified(buffer_start, buffer_size)) {\n            // Preserve the real GPU ownership and the existing ordered\n            // FlushRegion on the GPU worker. Do not certify stale siblings.\n            area.preemtive = false;\n        }\n    }\n    return area;\n}\n", "VideoCore::RasterizerDownloadArea RasterizerVulkan::GetFlushArea(DAddr addr, u64 size) {\n    ::Eden::Performance::DiagnosticTimer guest_read_timer(::Eden::Performance::guest_cpu_read);\n    const VideoCore::RasterizerDownloadArea clean_area{\n        .start_address = Common::AlignDown(addr, Core::DEVICE_PAGESIZE),\n        .end_address = Common::AlignUp(addr + size, Core::DEVICE_PAGESIZE),\n        .preemtive = true,\n    };\n    // A clean/never-held texture page does not prove that a GPU-written\n    // storage buffer is current in guest memory. Query each cache independently:\n    // never-owned pages remain lock-free, and no two cache locks are nested.\n    std::optional<VideoCore::RasterizerDownloadArea> texture_area;\n    if (!::Eden::ImagePages::NeverHeld(addr, size)) {\n        ::Eden::Performance::GuestCacheLock(texture_cache.mutex, true);\n        std::lock_guard lock{texture_cache.mutex, std::adopt_lock};\n        texture_area = texture_cache.GetFlushArea(addr, size);\n    }\n    if (texture_area && !texture_area->preemtive) {\n        // The existing GPU sync flushes both cache domains.\n        return *texture_area;\n    }\n    if (!::Eden::BufferPages::NeverRegistered(addr, size)) {\n        ::Eden::Performance::GuestCacheLock(buffer_cache.mutex, false);\n        std::lock_guard lock{buffer_cache.mutex, std::adopt_lock};\n        if (buffer_cache.IsRegionGpuModified(\n                clean_area.start_address, clean_area.end_address - clean_area.start_address)) {\n            // GPU::OnCPURead must not return early for buffer-only GPU writes.\n            // Keep any texture-covered extent, including its sibling bytes.\n            return VideoCore::RasterizerDownloadArea{\n                .start_address = texture_area\n                    ? (std::min)(texture_area->start_address, clean_area.start_address)\n                    : clean_area.start_address,\n                .end_address = texture_area\n                    ? (std::max)(texture_area->end_address, clean_area.end_address)\n                    : clean_area.end_address,\n                .preemtive = false,\n            };\n        }\n    }\n    return texture_area.value_or(clean_area);\n}\n"
    assert raster_text.count(newer) == 1
    raster_text = raster_text.replace(newer, older, 1)
    assert text_blob(raster_text) == "d8c093893b882175c46fb07c87193468da5f0f7a"
if text_blob(raster_text) == "d8c093893b882175c46fb07c87193468da5f0f7a":
    for newer, older in (("        // Indirect arguments are read, not overwritten. Preserve GPU dirty bytes.\n        const auto post_op = VideoCommon::ObtainBufferOperation::DoNothing;", "        const auto post_op = VideoCommon::ObtainBufferOperation::DiscardWrite;"), ("        buffer->MarkUsage(offset, 12);\n        scheduler.RequestOutsideRenderPassOperationContext();\n        // Lichii #1246: keep the input barrier in the dispatch command.", "        scheduler.RequestOutsideRenderPassOperationContext();\n        // Lichii #1246: keep the input barrier in the dispatch command.")):
        assert raster_text.count(newer) == 1
        raster_text = raster_text.replace(newer, older, 1)
    assert text_blob(raster_text) == "4a7ba88e47d686e3d277bee9df48f9b0d81a1248"

# Compute dispatch barrier folding changes only scheduler command granularity.
# Reverse both exact #1246 hunks before replaying the CPU-write safety receipt.
if "Lichii #1246" in raster_text:
    indirect_new="        scheduler.RequestOutsideRenderPassOperationContext();\n        // Lichii #1246: keep the input barrier in the dispatch command. Vulkan\n        // ordering is unchanged, but one scheduler/worker command disappears.\n        scheduler.Record([pipeline, indirect_buffer = buffer->Handle(),\n                          indirect_offset = offset](vk::CommandBuffer cmdbuf) {\n            if (::Eden::DevVulkan::compute_barriers) {\n                cmdbuf.PipelineBarrier(vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER,\n                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |\n                                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,\n                                       0, COMPUTE_INPUT_BARRIER);\n            }\n            if (!pipeline->IsBound()) {\n                return;\n            }"
    indirect_old="        scheduler.RequestOutsideRenderPassOperationContext();\n        if (::Eden::DevVulkan::compute_barriers) {\n            scheduler.Record([](vk::CommandBuffer cmdbuf) {\n                cmdbuf.PipelineBarrier(vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER,\n                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,\n                                       0, COMPUTE_INPUT_BARRIER);\n            });\n        }\n        scheduler.Record([pipeline, indirect_buffer = buffer->Handle(),\n                          indirect_offset = offset](vk::CommandBuffer cmdbuf) {\n            if (!pipeline->IsBound()) {\n                return;\n            }"
    direct_new="    static constexpr VkMemoryBarrier READ_BARRIER{\n        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,\n        .pNext = nullptr,\n        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,\n        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,\n    };\n    // Lichii #1246: this barrier was a separate scheduler command although\n    // nothing records Vulkan work between it and the dispatch.\n    scheduler.Record([pipeline, dim](vk::CommandBuffer cmdbuf) {\n        cmdbuf.PipelineBarrier(vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER,\n                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, READ_BARRIER);\n        if (!pipeline->IsBound()) {"
    direct_old="    static constexpr VkMemoryBarrier READ_BARRIER{\n        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,\n        .pNext = nullptr,\n        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,\n        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,\n    };\n    scheduler.Record([](vk::CommandBuffer cmdbuf) { cmdbuf.PipelineBarrier(vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,\n                               0, READ_BARRIER); });\n    scheduler.Record([pipeline, dim](vk::CommandBuffer cmdbuf) {\n        if (!pipeline->IsBound()) {"
    assert raster_text.count(indirect_new)==raster_text.count(direct_new)==1
    raster_text=raster_text.replace(indirect_new,indirect_old,1).replace(direct_new,direct_old,1)
    assert text_blob(raster_text)=="7049700a833c9e6cb239de8b01b5015dd7e3d163"
# DMA-only submit ownership is independent from the historical CPU-write
# protection contract. Authenticate and reverse only that exact successor.
if text_blob(raster_text)=="7049700a833c9e6cb239de8b01b5015dd7e3d163":
    newer="""void RasterizerVulkan::FlushCommands() {
#ifdef PS5_NATIVE
    // draw_counter measures graphics batching, not whether Vulkan work exists.
    // DMA fills/copies and image transfers can record scheduler commands without
    // any graphics draw. Conversely, a fence/explicit scheduler flush may have
    // already submitted the current recording. Use the selected native
    // scheduler's command epoch as the end-of-list authority.
    draw_counter = 0;
    if (!scheduler.RecordedSinceSubmit()) {
        return;
    }
#else
    if (draw_counter == 0) {
        return;
    }
    draw_counter = 0;
#endif
    scheduler.Flush();
}"""
    older="""void RasterizerVulkan::FlushCommands() {
    if (draw_counter == 0) {
        return;
    }
    draw_counter = 0;
    scheduler.Flush();
}"""
    assert raster_text.count(newer)==1
    raster_text=raster_text.replace(newer,older,1)
    assert text_blob(raster_text)=="f79943f596b8a95cdcb05ce74a964a2141ae10ac"
# FC27 attribute-handle index batching is PS5/NS1-only and does not change
# deferred page-protection ownership. Reverse only this exact draw-scan delta.
if text_blob(raster_text)=="f79943f596b8a95cdcb05ce74a964a2141ae10ac":
    assert raster_text.count("#ifdef PS5_NATIVE\n    // FC27 uses a vertex-attribute texture handle on indexed triangle draws.\n    // OnCPUWrite is serialized by buffer_cache.mutex before guest memcpy, so\n    // this scan must stay under that lock. Reduce only repeated GMMU walks:\n    // prefetch a small, fully-mapped index span and decode sampled indices\n    // from it. Authenticated NS2 channels retain the original per-index path.\n    struct AttributeIndexBatch {\n        std::array<u8, 256> bytes{};\n        u32 first_position{};\n        u32 position_count{};\n        size_t index_width{};\n        bool valid{};\n    } index_batch;\n#endif\n    const auto read_index = [&](u32 position) {\n        const auto& indices = state.index_buffer;\n        const size_t index_width = indices.FormatSizeInBytes();\n        if (index_width == 0 || index_width > sizeof(u32)) return u32{};\n#ifdef PS5_NATIVE\n        if (!gpu_memory->UsesNativeTiling()) {\n            if (index_batch.valid && index_batch.index_width == index_width &&\n                position >= index_batch.first_position &&\n                position - index_batch.first_position < index_batch.position_count) {\n                u32 index{};\n                const size_t byte_offset =\n                    static_cast<size_t>(position - index_batch.first_position) * index_width;\n                std::memcpy(&index, index_batch.bytes.data() + byte_offset, index_width);\n                return index;\n            }\n            if (position < total) {\n                const size_t max_positions = index_batch.bytes.size() / index_width;\n                const u32 position_count =\n                    (std::min)(total - position, static_cast<u32>(max_positions));\n                const GPUVAddr offset = static_cast<GPUVAddr>(position) * index_width;\n                const GPUVAddr start = indices.IndexStart();\n                const GPUVAddr address = start + offset;\n                const size_t read_bytes = static_cast<size_t>(position_count) * index_width;\n                const bool address_ok =\n                    (position == 0 || offset / index_width == position) &&\n                    address >= start && read_bytes != 0 &&\n                    address <= ~GPUVAddr{0} - static_cast<GPUVAddr>(read_bytes);\n                if (position_count > 1 && address_ok &&\n                    gpu_memory->IsFullyMappedRange(address, read_bytes)) {\n                    gpu_memory->ReadBlockUnsafe(address, index_batch.bytes.data(), read_bytes);\n                    index_batch.first_position = position;\n                    index_batch.position_count = position_count;\n                    index_batch.index_width = index_width;\n                    index_batch.valid = true;\n                    u32 index{};\n                    std::memcpy(&index, index_batch.bytes.data(), index_width);\n                    return index;\n                }\n            }\n        }\n#endif\n        const GPUVAddr address = indices.IndexStart() +\n                                 static_cast<GPUVAddr>(position) * index_width;\n        u32 index{};\n        gpu_memory->ReadBlockUnsafe(address, &index, index_width);\n        return index;\n    };\n    const auto read_handle = [&](u32 position) {\n        const u32 vertex =\n            is_indexed ? static_cast<u32>(state.base_index + read_index(position))\n                       : state.vertex_buffer.first + position;\n        return gpu_memory->Read<u32>(base_address +\n                                     static_cast<GPUVAddr>(vertex) * stream.stride);\n    };")==1
    raster_text=raster_text.replace("#ifdef PS5_NATIVE\n    // FC27 uses a vertex-attribute texture handle on indexed triangle draws.\n    // OnCPUWrite is serialized by buffer_cache.mutex before guest memcpy, so\n    // this scan must stay under that lock. Reduce only repeated GMMU walks:\n    // prefetch a small, fully-mapped index span and decode sampled indices\n    // from it. Authenticated NS2 channels retain the original per-index path.\n    struct AttributeIndexBatch {\n        std::array<u8, 256> bytes{};\n        u32 first_position{};\n        u32 position_count{};\n        size_t index_width{};\n        bool valid{};\n    } index_batch;\n#endif\n    const auto read_index = [&](u32 position) {\n        const auto& indices = state.index_buffer;\n        const size_t index_width = indices.FormatSizeInBytes();\n        if (index_width == 0 || index_width > sizeof(u32)) return u32{};\n#ifdef PS5_NATIVE\n        if (!gpu_memory->UsesNativeTiling()) {\n            if (index_batch.valid && index_batch.index_width == index_width &&\n                position >= index_batch.first_position &&\n                position - index_batch.first_position < index_batch.position_count) {\n                u32 index{};\n                const size_t byte_offset =\n                    static_cast<size_t>(position - index_batch.first_position) * index_width;\n                std::memcpy(&index, index_batch.bytes.data() + byte_offset, index_width);\n                return index;\n            }\n            if (position < total) {\n                const size_t max_positions = index_batch.bytes.size() / index_width;\n                const u32 position_count =\n                    (std::min)(total - position, static_cast<u32>(max_positions));\n                const GPUVAddr offset = static_cast<GPUVAddr>(position) * index_width;\n                const GPUVAddr start = indices.IndexStart();\n                const GPUVAddr address = start + offset;\n                const size_t read_bytes = static_cast<size_t>(position_count) * index_width;\n                const bool address_ok =\n                    (position == 0 || offset / index_width == position) &&\n                    address >= start && read_bytes != 0 &&\n                    address <= ~GPUVAddr{0} - static_cast<GPUVAddr>(read_bytes);\n                if (position_count > 1 && address_ok &&\n                    gpu_memory->IsFullyMappedRange(address, read_bytes)) {\n                    gpu_memory->ReadBlockUnsafe(address, index_batch.bytes.data(), read_bytes);\n                    index_batch.first_position = position;\n                    index_batch.position_count = position_count;\n                    index_batch.index_width = index_width;\n                    index_batch.valid = true;\n                    u32 index{};\n                    std::memcpy(&index, index_batch.bytes.data(), index_width);\n                    return index;\n                }\n            }\n        }\n#endif\n        const GPUVAddr address = indices.IndexStart() +\n                                 static_cast<GPUVAddr>(position) * index_width;\n        u32 index{};\n        gpu_memory->ReadBlockUnsafe(address, &index, index_width);\n        return index;\n    };\n    const auto read_handle = [&](u32 position) {\n        const u32 vertex =\n            is_indexed ? static_cast<u32>(state.base_index + read_index(position))\n                       : state.vertex_buffer.first + position;\n        return gpu_memory->Read<u32>(base_address +\n                                     static_cast<GPUVAddr>(vertex) * stream.stride);\n    };","    const auto read_handle = [&](u32 position) {\n        u32 vertex{};\n        if (is_indexed) {\n            const auto& indices = state.index_buffer;\n            const size_t index_width = indices.FormatSizeInBytes();\n            if (index_width == 0 || index_width > sizeof(u32)) return u32{};\n            const GPUVAddr address = indices.IndexStart() +\n                                     static_cast<GPUVAddr>(position) * index_width;\n            u32 index{};\n            gpu_memory->ReadBlockUnsafe(address, &index, index_width);\n            vertex = static_cast<u32>(state.base_index + index);\n        } else {\n            vertex = state.vertex_buffer.first + position;\n        }\n        return gpu_memory->Read<u32>(base_address +\n                                     static_cast<GPUVAddr>(vertex) * stream.stride);\n    };",1)
    assert text_blob(raster_text)=="fcd174ed2326f3c205365b192c98f9d2f4d82ec3"
# FC27 24/11: collapse the two cache-owner probes to one pass. This is a
# performance-only successor; restore the exact prior rasterizer before the
# older batch-lock and synchronous-protection receipts.
if text_blob(raster_text)=="fcd174ed2326f3c205365b192c98f9d2f4d82ec3":
    newer="    // One owner-witness pass for the complete collected batch, as in\n    // InnerInvalidation. Each witness is monotonic and fails closed; once both\n    // cache domains may overlap, no later range can change the lock decision.\n    bool may_claim_buffer = false;\n    bool may_touch_texture = false;\n    for (const auto& [addr, size] : ranges) {\n        if (addr == 0 || size == 0) continue;\n        if (!may_claim_buffer && !::Eden::BufferPages::NeverRegistered(addr, size))\n            may_claim_buffer = true;\n        if (!may_touch_texture && !::Eden::ImagePages::NeverHeld(addr, size))\n            may_touch_texture = true;\n        if (may_claim_buffer && may_touch_texture) break;\n    }\n    // GPU-collected write batches acquire each cache domain only once. Claim\n    // still precedes every BufferCache write/readback below.\n"
    older="    // Remove the complete CPU-write batch from GPU-owned ranges before any\n    // individual invalidation can download stale bytes over a later write.\n    // No registered buffer page in this CPU-write batch means no buffer can\n    // own GPU bytes to claim. Avoid the buffer mutex entirely in that case.\n    // NeverRegistered is monotonic and fails closed on unknown address spans.\n    bool may_claim_buffer = false;\n    for (const auto& [addr, size] : ranges) {\n        if (addr != 0 && size != 0 && !::Eden::BufferPages::NeverRegistered(addr, size)) {\n            may_claim_buffer = true;\n            break;\n        }\n    }\n    // The single-range invalidation callback takes the texture and buffer\n    // mutex once PER range. GPU-collected write batches often contain many\n    // pages; acquire each domain only once, as InnerInvalidation already\n    // does for the same texture -> buffer -> shader ordering. The prior\n    // ClaimCpuWrites still precedes every possible GPU sibling readback.\n    bool may_touch_texture = false;\n    for (const auto& [addr, size] : ranges) {\n        if (addr != 0 && size != 0 && !::Eden::ImagePages::NeverHeld(addr, size)) {\n            may_touch_texture = true;\n            break;\n        }\n    }\n"
    assert raster_text.count(newer)==1
    raster_text=raster_text.replace(newer,older,1)
    assert text_blob(raster_text)=="e4948333ed15fba8024aeb4ffaa143a2e010f991"
# FC27 24/11 batching moves ClaimCpuWrites into the one buffer-cache lock.
# Reverse only that reviewed delta before replaying this older safety receipt.
if text_blob(raster_text)=="e4948333ed15fba8024aeb4ffaa143a2e010f991":
    newer="""    if (may_claim_buffer) {
        // Lichii 5e428570 / f5edeab: claim and apply the complete buffer batch
        // under ONE acquisition. Texture state was handled above and is independent.
        // Claim must precede BufferCache::WriteMemory so a sibling GPU download
        // cannot overwrite a later CPU write from this same collected batch.
        std::scoped_lock lock{buffer_cache.mutex};
        buffer_cache.ClaimCpuWrites(ranges);
        for (const auto& [addr, size] : ranges) {
"""
    older="""    if (may_claim_buffer) {
        std::scoped_lock lock{buffer_cache.mutex};
        for (const auto& [addr, size] : ranges) {
"""
    marker="    // The single-range invalidation callback takes the texture and buffer\n"
    first="""    if (may_claim_buffer) {
        std::scoped_lock lock{buffer_cache.mutex};
        buffer_cache.ClaimCpuWrites(ranges);
    }

"""
    assert raster_text.count(newer)==raster_text.count(marker)==1
    raster_text=raster_text.replace(newer,older,1).replace(marker,first+marker,1)
    assert text_blob(raster_text)=="e5baa7f438d8caf11327989087db38ed1ca053a4"
assert raster_text.count(NATIVE_SYNC_BLOCK)==1
fc27_raster=raster_text.replace(NATIVE_SYNC_BLOCK,"")
assert text_blob(fc27_raster)==GUEST_CACHE_LOCK_SUCCESSOR
restored=restore_integrated_rasterizer(fc27_raster)
assert text_blob(restored)=="71d097b9a83028dfad9370792c905bb3bfccfd5a"
# The renderer retirement successor changes only callback lifetime, not
# synchronous page-protection publication. Keep the exact previous owner.
assert blob(D) in {"daf493ce6e863397ec44bfae467c4f7533570477", "8acbdfe9ac970d24fecd334ca7b943f6bcd958f4"}
if blob(D) == "8acbdfe9ac970d24fecd334ca7b943f6bcd958f4":
    assert D.read_text().count("NativeRendererLifetime::MappingCallbackLease lease;") == 2
# The selected NS1/NS2 TextureCache now includes 10 independently reviewed
# safety and DMA source changes; this full file SHA remains a fail-closed
# inventory, not a silent acceptance of a changed historic contract.
assert blob(T) in {
    "948960fad4d829ba874746acb6e353a8a2ed19f7",  # original integrated DMA owner
    "5c6d015cf207d63de31b577411972606145d9f2e",  # empty render-target guard
    "27ea4e0e665d3f94e173c1335cb19211efc03646",  # selected HUD/cache parity
    "bbb55dc7b1648fa1e6fd730a1b3515f2969221c7",  # selected readback batching and dirty rollback
    "5fa75d9b6b7b4f9710a6840182dc0de854e62268",  # active 16-entry HUD TIC memo parity
    "709ee61bc3a620654fa9c66eafa6482aab382d12",  # framebuffer view format guard
    "bfd98bca9204d06da8a828c9dba6d8eab7bafb63",  # selected alias copy publication parity
    "4076fb27d78e6be2893d6a09d3f9d5493e6bab59",  # selected native async readback and upload parity
    "85c2514267ca38fc0b88d1442a464cc0299aa2d8",  # selected dirty GC fallback and pressure arithmetic
    "ab226987e5e20829d156c8edb917fd4851cf478d",  # selected bounded independent GC readbacks
    "014b8a25cf34391965f7f465c372e7673596e86b",  # selected lookup and explicit readback admission
}
if blob(T) in {"27ea4e0e665d3f94e173c1335cb19211efc03646", "bbb55dc7b1648fa1e6fd730a1b3515f2969221c7", "5fa75d9b6b7b4f9710a6840182dc0de854e62268", "709ee61bc3a620654fa9c66eafa6482aab382d12", "bfd98bca9204d06da8a828c9dba6d8eab7bafb63", "4076fb27d78e6be2893d6a09d3f9d5493e6bab59", "85c2514267ca38fc0b88d1442a464cc0299aa2d8", "ab226987e5e20829d156c8edb917fd4851cf478d", "014b8a25cf34391965f7f465c372e7673596e86b"}:
    for guard in ("if (it == channel_state->sampler_ids.end())",
                  "if (pair->second != NULL_IMAGE_VIEW_ID || remapped_cached_view)",
                  "if (removed_views.empty())"):
        assert guard in T.read_text(), guard
for selected_guard in ("SplitLayeredImageCopies(copies)", "index > table.current_limit || index >= table.descriptors.size()", "constexpr u32 MaxDescriptorTableLimit = (1U << 21) - 1;", "TextureCache<P>::WriteDmaCopy(", "gpu_written_dest", "copy_ns1_volume_slice", "if (!gpu_memory || gpu_memory->UsesNativeTiling()", "alias_conversion_format", "PixelFormat::X8_D24_UNORM", "bool upload_completed = false;", "kMaxCompletedDecodesPerTick = 4"):
    assert selected_guard in T.read_text(), ("FC27/NS2 active texture guard lost",selected_guard)
# Selected NS1/NS2 TextureCache must retain the causal native protections.
texture_selected=T.read_text()
if blob(T) == "014b8a25cf34391965f7f465c372e7673596e86b":
    for guard in ("bool has_candidate = false;", "if (bytes == 0 || copies.empty())", "!map.buffer || map.mapped_span.size_bytes() < bytes", "if (map.buffer) runtime.FreeDeferredStagingBuffer(map);"):
        assert guard in texture_selected, ("selected lookup/readback guard lost", guard)
if blob(T) in {"ab226987e5e20829d156c8edb917fd4851cf478d", "014b8a25cf34391965f7f465c372e7673596e86b"}:
    for guard in ("constexpr size_t gc_batch_count = 40;", "constexpr size_t gc_batch_bytes = 32_MiB;", "if (candidates.size() < 2) return;", "if (must_download && !UseGcPrefetch(image_id, image))", "gc_retired = true;", "runtime.FreeDeferredStagingBuffer(it->second);"):
        assert guard in texture_selected, ("selected GC prefetch protection lost", guard)
if blob(T) in {"85c2514267ca38fc0b88d1442a464cc0299aa2d8", "ab226987e5e20829d156c8edb917fd4851cf478d", "014b8a25cf34391965f7f465c372e7673596e86b"}:
    for guard in ("if (FullDownloadCopies(image.info).empty())", "if (frame_tick < ticks_to_destroy) return;", "total_used_memory -= (std::min)(total_used_memory, scaled_bytes);", "total_used_memory -= (std::min)(total_used_memory, reclaimed_bytes);"):
        assert guard in texture_selected, ("selected GC protection lost", guard)
if blob(T) in {"4076fb27d78e6be2893d6a09d3f9d5493e6bab59", "85c2514267ca38fc0b88d1442a464cc0299aa2d8", "ab226987e5e20829d156c8edb917fd4851cf478d", "014b8a25cf34391965f7f465c372e7673596e86b"}:
    for guard in ("if (!download_submitted)", "bool batch_published = false;", "bool batch_consumed = false;", "MapSizeBytes(image),", "remaining_offsets"):
        assert guard in texture_selected, ("selected async readback/upload protection lost", guard)
if blob(T) == "bbb55dc7b1648fa1e6fd730a1b3515f2969221c7":
    for guard in ("DownloadStagingBuffer(bytes, true)", "std::uncaught_exceptions() > uncaught_before_readback && !pending.empty()", "std::uncaught_exceptions()", "slot_images[id].flags |= ImageFlagBits::GpuModified"):
        assert guard in texture_selected, guard
for needle in ("bool upload_completed = false;", "std::try_to_lock",
               "kMaxCompletedDecodesPerTick = 4",
               "return GetImageViewForFramebuffer(freshest_id);"):
    assert needle in texture_selected, ("selected texture protection lost",needle)
# The PS5-selected owner must also contain the previously shadow-only BCn 3D
# cancellation, allocation, and bounds protections, not just 2D decode fixes.
shadow=(ROOT/"encore-active/eden-vulkan/vulkan-cache/video_core/texture_cache/texture_cache.h").read_text()
# The selected native owner must carry the same draw-local TIC memo as the
# maintained Vulkan owner: nonadjacent positive hits may skip descriptor
# lookup, but each hit still runs PrepareImageView for dirty HUD pixels.
fill_method="void TextureCache<P>::FillImageViews(std::span<ImageViewInOut> views, bool compute, bool blacklist) {"
def fill_body(src):
    return src.split(fill_method,1)[1].split("\n}\n",1)[0]
assert fill_body(texture_selected)==fill_body(shadow), "selected FillImageViews HUD memo diverged"
for needle in ("std::array<ViewMemo, 16> view_memo;",
               "PrepareImageView(cached.id, false, false);",
               "const bool memo_enabled = !blacklist && views.size() > 1;"):
    assert needle in fill_body(texture_selected), needle
method="void TextureCache<P>::TickAsyncUnswizzle() {"
def body(src):
    return src.split(method,1)[1].split("\n}\n",1)[0]
assert body(texture_selected)==body(shadow), "selected BCn 3D GPU unswizzle protections diverged"
for needle in ("invalid_slices", "staging.mapped_span.size() < task.total_size",
               "ImageFlagBits::CpuModified | ImageFlagBits::Remapped"):
    assert needle in body(texture_selected), needle
# Keep zero-fill elision scoped to the native async conversion input only.
alloc_start="void TextureCache<P>::QueueAsyncDecode(Image& image, ImageId image_id) {"
def native_allocation(src):
    block=src.split(alloc_start,1)[1]
    begin=block.index("    Common::ScratchBuffer<u8> local_unswizzle_data_buffer;")
    end=block.index("    Tegra::Memory::GpuGuestMemory<u8",begin)
    return block[begin:end]
assert native_allocation(texture_selected)==native_allocation(shadow)
assert "resize_destructive(image.unswizzled_size_bytes);" in native_allocation(texture_selected)
assert not (ROOT/"encore-active/eden-core/overrides/core/deferred_cached_updates.h").exists()
raster=R.read_text()
device=D.read_text()
texture=T.read_text()
assert "DeferredCachedUpdates" not in raster+device
# The DMA fix may invalidate only guest-current destination images.
dma=raster.split("bool AccelerateDMA::BufferCopy(",1)[1].split(
    "\ntemplate <bool IS_IMAGE_UPLOAD>",1)[0]
assert "std::scoped_lock lock{buffer_cache.mutex, texture_cache.mutex};" in dma
assert dma.index("buffer_cache.DMACopy(") < dma.index("texture_cache.WriteGpuMemory(")
assert "if (!copied_gpu_bytes &&" in dma
assert "!texture_cache.IsGpuMemoryGpuModified(src_address, amount)" in dma
assert "!texture_cache.IsGpuMemoryGpuModified(dest_address, amount)" in dma
assert "bool may_claim_buffer = false;" in raster
# The GMMU may return mapped fragments while omitting holes. Re-uploading
# those spans must fail closed when any byte remains untranslatable.
assert "piece_addr - gpu_addr != covered" in texture
assert "piece_size > size - covered" in texture
assert "return covered != size || size == 0;" in texture
assert "ImagePages::Mark(image.cpu_addr, image.guest_size_bytes);" in texture
assert "ImagePages::Mark(cpu_addr, size);" in texture
start=raster.index("bool RasterizerVulkan::OnCPUWrite(")
end=raster.index("\nvoid RasterizerVulkan::OnCacheInvalidations(",start)
method=raster[start:end]
assert "static thread_local" not in method
assert method.index("GuestCacheLock(buffer_cache.mutex, false)") < method.index("buffer_cache.OnCPUWrite(addr, size)")
assert method.index("buffer_cache.OnCPUWrite(addr, size)") < method.index("texture_cache.WriteMemory(addr, size)")
assert "if (buffer_cache.OnCPUWrite(addr, size)) {\n            return true;\n        }" in method
assert "ImagePages::NeverHeld" in method
assert "device_inter->InvalidateRegion(address, size);" in device
assert "WaitForPublished" not in device
assert "void DeviceMemoryManager<Traits>::UpdatePagesCachedBatch" in device
assert "TryAdd(" not in device

fixture=r"""
#include <cassert>
#include <atomic>
#include <cstdint>
#include <mutex>
using DAddr=std::uint64_t; using u64=std::uint64_t;
#define DEBUG_ASSERT(x) assert(x)
struct FakeMutex {
    bool held=false;
    void lock(){assert(!held);held=true;}
    void unlock(){assert(held);held=false;}
};
namespace Eden {
namespace Performance {
enum class CachePhase {GuestWriteBufferWait,GuestWriteBufferWork,
    GuestWriteTextureWait,GuestWriteTextureWork,GuestWriteShaderInvalidate};
struct BatchedGuestWriteTimer { ~BatchedGuestWriteTimer() {} };
bool ShouldSampleGuestWrite(){return false;}
struct CachePhaseTimer {
    CachePhaseTimer(CachePhase,bool){}
    void Stop(){}
};
void GuestCacheLock(FakeMutex& m,bool){m.lock();}
struct CpuAhead { bool load(std::memory_order) const{return false;} };
CpuAhead cpu_write_ahead;
}
namespace BufferPages {
bool skip=false;
bool NeverRegistered(DAddr,u64){return skip;}
}
namespace ImagePages {
bool skip=false;
bool NeverHeld(DAddr,u64){return skip;}
}
namespace BufferWriteAhead {
std::atomic<int> calls{};
template<class A,class B>
bool MarkFollowingPages(std::uint64_t,std::uint64_t,bool,A,B) {
    ++calls; return false;
}
}
}
struct BufferCache {
    FakeMutex mutex;
    int writes{};
    bool gpu_modified{};
    bool OnCPUWrite(DAddr,u64){assert(mutex.held);++writes;return gpu_modified;}
    bool IsRegionRegistered(DAddr,u64){assert(mutex.held);return true;}
};
struct TextureCache {
    FakeMutex mutex;
    int writes{};
    void WriteMemory(DAddr,u64){assert(mutex.held);++writes;}
};
struct PipelineCache {
    int invalidations{};
    void InvalidateRegion(DAddr,u64){++invalidations;}
};
struct RasterizerVulkan {
    BufferCache buffer_cache;
    TextureCache texture_cache;
    PipelineCache pipeline_cache;
    bool OnCPUWrite(DAddr,u64);
};
""" + method + r"""
int main(){
    RasterizerVulkan r;
    assert(!r.OnCPUWrite(0x4000,64));
    assert(r.buffer_cache.writes==1 && r.texture_cache.writes==1);
    assert(r.pipeline_cache.invalidations==1);
    assert(!r.buffer_cache.mutex.held && !r.texture_cache.mutex.held);
    r.buffer_cache.gpu_modified=true;
    assert(r.OnCPUWrite(0x4000,64));
    assert(r.buffer_cache.writes==2 && r.texture_cache.writes==1);
    assert(r.pipeline_cache.invalidations==1);
    r.buffer_cache.gpu_modified=false;
    Eden::BufferPages::skip=true;
    assert(!r.OnCPUWrite(0x5000,64));
    assert(r.buffer_cache.writes==2 && r.texture_cache.writes==2);
    assert(r.pipeline_cache.invalidations==2);
    Eden::ImagePages::skip=true;
    assert(!r.OnCPUWrite(0x6000,64));
    assert(r.texture_cache.writes==2 && r.pipeline_cache.invalidations==3);
    Eden::BufferPages::skip=false;
    assert(!r.OnCPUWrite(0x7000,64));
    assert(r.buffer_cache.writes==3 && r.texture_cache.writes==2);
    assert(r.pipeline_cache.invalidations==4);
    // First ordinary unmodified write and last registered write only.
    // GPU-modified early return and BufferPages::NeverRegistered shortcuts
    // correctly do not enter the write-ahead helper.
    assert(Eden::BufferWriteAhead::calls==2);
}
"""
compilers=list(dict.fromkeys(filter(None, (os.environ.get("CXX"),shutil.which("clang++"),shutil.which("g++")))))
assert compilers, "host C++ compiler required"
with tempfile.TemporaryDirectory(prefix="fc27-sync-page-") as temp:
    code=Path(temp)/"test.cpp"
    code.write_text(fixture)
    for compiler in compilers:
        for opt in ("-O0","-O2"):
            binary=Path(temp)/(Path(compiler).name+opt)
            subprocess.run([compiler,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                            "-fsanitize=undefined","-fno-sanitize-recover=all",
                            str(code),"-o",str(binary)],check=True,timeout=45)
            subprocess.run([str(binary)],check=True,timeout=10)
print("FC27_SYNCHRONOUS_PAGE_PROTECTION_HOST_PASS source=1 gpu_modified=1 texture_invalidate=1 ps5=unverified")
