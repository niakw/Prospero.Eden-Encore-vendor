#!/usr/bin/env python3
"""NS1 #18 coherent four-owner TIR mixed-sample scratch/resolve source contract."""
from pathlib import Path
import hashlib
from native_msaa_handle_receipt import restore_integrated_vk_texture_except_blit
import os
import subprocess
import tempfile

r=Path(__file__).resolve().parents[1]
files={
"encore-active/eden-vulkan/fixed_pipeline_state.cpp":"71f35e18d4c3cdbfb6d23e065dc98e1d525ad2e4",
"encore-active/eden-vulkan/vk_texture_cache_cost.cpp":"2b344e2c9adce18a7037194cc4d5f81a5c6df144",
"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_texture_cache.h":"1b725abab76168d9787d9b062d7406df733cf49d",
"encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h":"da6ab6dcea2a1c02fcb2e862d111dc410b025d66",
}
for name,expected in files.items():
 raw=(r/name).read_bytes()
 sha=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
 if name.endswith("/vk_texture_cache_cost.cpp") and sha=="e9191238ccddd94aa22570eba501fb05fff5b207":
  raw=restore_integrated_vk_texture_except_blit(raw.decode()).encode()
  sha=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
 if name.endswith("fixed_pipeline_state.cpp"):
  text=raw.decode()
  assert "MaxwellDepthClampClip" in text and "regs.tir_mode == Maxwell::TIRMode::RasterNTargetM" in text,(name,"lost EDS3/TIR")
 elif name.endswith("texture_cache/texture_cache.h") and "ImagePages::Mark(image.cpu_addr" in raw.decode():
  text=raw.decode()
  if sha == "948960fad4d829ba874746acb6e353a8a2ed19f7":
   # Integrated NS1/NS2 TextureCache successor. Reverse the exact reviewed
   # descriptor/DMA/layered-copy/volume/X8_D24 additions on the audit copy
   # and require byte-identical parity with the previously qualified owner.
   for required in ("SplitLayeredImageCopies(", "MaxDescriptorTableLimit",
                    "TextureCache<P>::WriteDmaCopy(", "copy_ns1_volume_slice",
                    "alias_conversion_format", "PixelFormat::X8_D24_UNORM"):
    assert required in text, ("selected TextureCache protection lost", required)
   lines=text.split("\n")
   reverse=(
    (49, ["","inline std::vector<ImageCopy> SplitLayeredImageCopies(std::span<const ImageCopy> copies) {","    std::vector<ImageCopy> layer_copies;","    layer_copies.reserve(copies.size());","    for (const ImageCopy& copy : copies) {","        const s32 num_layers = copy.dst_subresource.num_layers;","        if (num_layers <= 1 || copy.src_subresource.num_layers != num_layers) {","            layer_copies.push_back(copy);","            continue;","        }","        for (s32 layer = 0; layer < num_layers; ++layer) {","            ImageCopy single = copy;","            single.dst_subresource.base_layer += layer;","            single.src_subresource.base_layer += layer;","            single.dst_subresource.num_layers = 1;","            single.src_subresource.num_layers = 1;","            layer_copies.push_back(single);","        }","    }","    return layer_copies;","}","","","using Tegra::Texture::TICEntry;","using Tegra::Texture::TSCEntry;","using VideoCore::Surface::GetFormatType;"], ["using Tegra::Texture::TICEntry;","using Tegra::Texture::TSCEntry;","using VideoCore::Surface::GetFormatType;"]),
    (449, ["    if (index > table.current_limit || index >= table.descriptors.size()) {","        LOG_TRACE(HW_GPU, \"Invalid sampler index={}\", index);","        return NULL_SAMPLER_ID;","    }"], ["    if (index > table.current_limit) {","        LOG_DEBUG(HW_GPU, \"Invalid sampler index={}\", index);","        return NULL_SAMPLER_ID;","    }"]),
    (475, ["    // A guest-controlled TIC/TSC limit must never allocate an unbounded","    // descriptor array from inside DescriptorTable::Refresh() noexcept.","    // 21-bit table indices are the conservative upstream upper bound.","    constexpr u32 MaxDescriptorTableLimit = (1U << 21) - 1;","    if (compute) {","        const bool linked_tsc = kepler_compute->launch_description.linked_tsc;","        const u32 tic_limit = (std::min)(kepler_compute->regs.tic.limit, MaxDescriptorTableLimit);","        const u32 tsc_limit = linked_tsc ? tic_limit : (std::min)(kepler_compute->regs.tsc.limit, MaxDescriptorTableLimit);","        bool bindings_changed = false;"], ["    if (compute) {","        const bool linked_tsc = kepler_compute->launch_description.linked_tsc;","        const u32 tic_limit = kepler_compute->regs.tic.limit;","        const u32 tsc_limit = linked_tsc ? tic_limit : kepler_compute->regs.tsc.limit;","        bool bindings_changed = false;"]),
    (493, ["        const u32 tic_limit = (std::min)(maxwell3d->regs.tex_header.limit, MaxDescriptorTableLimit);","        const u32 tsc_limit = linked_tsc ? tic_limit : (std::min)(maxwell3d->regs.tex_sampler.limit, MaxDescriptorTableLimit);","        bool bindings_changed = false;","        if (channel_state->graphics_sampler_table.Synchronize(maxwell3d->regs.tex_sampler.Address(), tsc_limit))","            bindings_changed = true;"], ["        const u32 tic_limit = maxwell3d->regs.tex_header.limit;","        const u32 tsc_limit = linked_tsc ? tic_limit : maxwell3d->regs.tex_sampler.limit;","        bool bindings_changed = false;","        if (channel_state->graphics_sampler_table.Synchronize(maxwell3d->regs.tex_sampler.Address(), tsc_limit))","            bindings_changed = true;"]),
    (849, ["    if (index > table.current_limit || index >= table.descriptors.size()) {","        LOG_TRACE(HW_GPU, \"Invalid image view index={}\", index);","        return NULL_IMAGE_VIEW_ID;","    }"], ["    if (index > table.current_limit) {","        LOG_DEBUG(HW_GPU, \"Invalid image view index={}\", index);","        return NULL_IMAGE_VIEW_ID;","    }"]),
    (985, ["void TextureCache<P>::WriteDmaCopy(","    GPUVAddr src_addr, GPUVAddr dest_addr, size_t size,","    std::span<const std::pair<DAddr, size_t>> gpu_written_dest) {","    if (size == 0) {","        return;","    }","    const std::optional<DAddr> src = gpu_memory->GpuToCpuAddress(src_addr);","    const std::optional<DAddr> dest = gpu_memory->GpuToCpuAddress(dest_addr);","    if (!src || !dest ||","        *src > (std::numeric_limits<DAddr>::max)() - size ||","        *dest > (std::numeric_limits<DAddr>::max)() - size) {","        return;","    }","    const DAddr src_end = *src + size;","    const DAddr dest_end = *dest + size;","","    // BufferCache reports destination bytes whose source was newer on the GPU.","    // Guest memory still contains older bytes there until a later download.","    boost::container::small_vector<std::pair<DAddr, size_t>, 8> unheld;","    unheld.reserve(gpu_written_dest.size() + 4);","    for (const auto& [begin, length] : gpu_written_dest) {","        if (length == 0 || begin < *dest || begin >= dest_end ||","            length > dest_end - begin) {","            return;","        }","        unheld.emplace_back(begin, length);","    }","","    // GPU-written source images have the same property: DMACopy moves their","    // host-GPU bytes, while the guest-memory mirror still holds older data.","    ForEachImageInRegion(*src, size, [&](ImageId, Image& image) {","        if (False(image.flags & ImageFlagBits::GpuModified)) {","            return;","        }","        const DAddr begin = (std::max)(static_cast<DAddr>(image.cpu_addr), *src);","        const DAddr end = (std::min)(static_cast<DAddr>(image.cpu_addr_end), src_end);","        if (begin < end) {","            unheld.emplace_back(*dest + (begin - *src), static_cast<size_t>(end - begin));","        }","    });","","    const auto overlaps_unheld = [&unheld](const Image& image) {","        return std::ranges::any_of(unheld, [&image](const auto& range) {","            const DAddr range_end = range.first + range.second;","            return range.first < image.cpu_addr_end && image.cpu_addr < range_end;","        });","    };","","    ForEachImageInRegion(*dest, size, [&](ImageId image_id, Image& image) {","        // Uploading this image would consume bytes that guest memory does not","        // hold yet, or discard GPU-owned texels outside the copied interval.","        const bool keeps_gpu_contents =","            True(image.flags & ImageFlagBits::GpuModified) &&","            (image.cpu_addr < *dest || image.cpu_addr_end > dest_end);","        if (overlaps_unheld(image) || keeps_gpu_contents) {","            return;","        }","#ifdef EDEN_NS2_ASTC_CACHE_BRIDGE","        if (image.ns2_astc_dirty) {","            if (image.ns2_astc_dirty->MarkWrite(*dest, size)) {","                image.flags |= ImageFlagBits::CpuModified;","                return;","            }","            image.ns2_astc_dirty.reset();","        }","#endif","        if (True(image.flags & ImageFlagBits::CpuModified)) {","            return;","        }","        image.flags |= ImageFlagBits::CpuModified;","        if (True(image.flags & ImageFlagBits::Tracked)) {","            UntrackImage(image, image_id);","        }","    });","}","","template <class P>","bool TextureCache<P>::IsGpuMemoryGpuModified(GPUVAddr gpu_addr, size_t size) {","    // GetSubmappedRange omits unmapped/reserved holes. A partial translation","    // must never make a DMA destination look safe for a guest-memory upload."], ["bool TextureCache<P>::IsGpuMemoryGpuModified(GPUVAddr gpu_addr, size_t size) {","    // GetSubmappedRange omits unmapped/reserved holes. A partial translation","    // must never make a DMA destination look safe for a guest-memory upload."]),
    (1409, ["#ifdef PS5_NATIVE","    // NS1 / Maxwell production path for Lichii #853 semantics. NS2 address","    // spaces enable native tiling after authenticated channel admission and","    // keep using the separately guarded native-volume path above.","    if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {","        const auto copy_ns1_volume_slice = [&]() -> bool {","            using Fermi2D = Tegra::Engines::Fermi2D;","            if (!gpu_memory || gpu_memory->UsesNativeTiling() || !channel_state ||","                copy.operation != Fermi2D::Operation::SrcCopy ||","                copy.filter != Fermi2D::Filter::Point) {","                return false;","            }","            const auto is_volume = [](const Fermi2D::Surface& surface) {","                return surface.depth > 1 &&","                       surface.linear == Fermi2D::MemoryLayout::BlockLinear &&","                       surface.layer == 0 && surface.width != 0 && surface.height != 0;","            };","            if (src.width > static_cast<u32>(std::numeric_limits<s32>::max()) ||","                dst.width > static_cast<u32>(std::numeric_limits<s32>::max()) ||","                src.height > static_cast<u32>(std::numeric_limits<s32>::max()) ||","                dst.height > static_cast<u32>(std::numeric_limits<s32>::max())) {","                return false;","            }","            const bool whole_slice =","                copy.src_x0 == 0 && copy.src_y0 == 0 &&","                copy.dst_x0 == 0 && copy.dst_y0 == 0 &&","                copy.src_x1 == static_cast<s32>(src.width) &&","                copy.src_y1 == static_cast<s32>(src.height) &&","                copy.dst_x1 == static_cast<s32>(dst.width) &&","                copy.dst_y1 == static_cast<s32>(dst.height);","            if (!is_volume(src) || !is_volume(dst) || !whole_slice ||","                src.format != dst.format || src.width != dst.width ||","                src.height != dst.height) {","                return false;","            }","","            const auto format =","                VideoCore::Surface::PixelFormatFromRenderTargetFormat(src.format);","            struct Slice {","                ImageId id;","                s32 depth;","            };","            const auto find_slice = [&](const Fermi2D::Surface& surface)","                -> std::optional<Slice> {","                std::optional<Slice> found;","                bool ambiguous = false;","                const GPUVAddr address = surface.Address();","                ForEachImageInRegionGPU(channel_state->gpu_memory.GetID(), address, 1,","                                        [&](ImageId id, Image& image) {","                    if (image.info.type != ImageType::e3D ||","                        image.info.format != format ||","                        image.info.num_samples != 1 ||","                        image.info.size.width != surface.width ||","                        image.info.size.height != surface.height ||","                        image.info.size.depth != surface.depth ||","                        !image.aliased_images.empty() || !image.overlapping_images.empty() ||","                        True(image.flags & (ImageFlagBits::Remapped | ImageFlagBits::Sparse |","                                            ImageFlagBits::Rescaled | ImageFlagBits::IsDecoding)) ||","                        address < image.gpu_addr) {","                        return false;","                    }","                    const u64 relative = address - image.gpu_addr;","                    if (relative >= image.guest_size_bytes ||","                        relative > std::numeric_limits<u32>::max() ||","                        image.slice_offsets.size() != image.slice_subresources.size()) {","                        return false;","                    }","                    const auto it =","                        std::ranges::find(image.slice_offsets, static_cast<u32>(relative));","                    if (it == image.slice_offsets.end()) {","                        return false;","                    }","                    const auto index = static_cast<size_t>(","                        std::distance(image.slice_offsets.begin(), it));","                    const SubresourceBase base = image.slice_subresources[index];","                    if (base.level != 0 || base.layer < 0 ||","                        static_cast<u32>(base.layer) >= image.info.size.depth) {","                        return false;","                    }","                    if (found) {","                        ambiguous = true;","                        return false;","                    }","                    found = Slice{id, base.layer};","                    return false;","                });","                return ambiguous ? std::nullopt : found;","            };","","            const auto src_slice = find_slice(src);","            const auto dst_slice = find_slice(dst);","            if (!src_slice || !dst_slice || src_slice->id == dst_slice->id) {","                return false;","            }","","            struct PhysicalSpan {","                u64 address;","                u64 bytes;","            };","            const auto capture_physical = [&](ImageId id) -> std::optional<PhysicalSpan> {","                const Image& image = slot_images[id];","                const u64 bytes = image.guest_size_bytes;","                constexpr u64 MaxVolumeBytes = 256ULL * 1024 * 1024;","                if (!image.gpu_addr || !bytes || bytes > MaxVolumeBytes ||","                    image.gpu_addr > std::numeric_limits<u64>::max() - (bytes - 1) ||","                    !gpu_memory->IsContinuousRange(image.gpu_addr, bytes)) {","                    return std::nullopt;","                }","                const auto start = gpu_memory->GpuToCpuAddress(image.gpu_addr);","                const auto end = gpu_memory->GpuToCpuAddress(image.gpu_addr + bytes - 1);","                if (!start || !end || *start != image.cpu_addr ||","                    *start > std::numeric_limits<u64>::max() - (bytes - 1) ||","                    *end != *start + bytes - 1) {","                    return std::nullopt;","                }","                return PhysicalSpan{*start, bytes};","            };","            const auto overlaps = [](PhysicalSpan lhs, PhysicalSpan rhs) {","                return lhs.address <= rhs.address","                    ? rhs.address - lhs.address < lhs.bytes","                    : lhs.address - rhs.address < rhs.bytes;","            };","            const auto src_physical = capture_physical(src_slice->id);","            const auto dst_physical = capture_physical(dst_slice->id);","            if (!src_physical || !dst_physical ||","                overlaps(*src_physical, *dst_physical)) {","                return false;","            }","","            runtime.FlushDeferredClear();","            PrepareImage(src_slice->id, false, false);","            PrepareImage(dst_slice->id, false, false);","","            // RefreshContents/SynchronizeAliases may replace either cached","            // image. Resolve the exact slices again before publishing a write.","            const auto src_after = find_slice(src);","            const auto dst_after = find_slice(dst);","            if (!src_after || !dst_after ||","                src_after->id != src_slice->id || dst_after->id != dst_slice->id ||","                src_after->depth != src_slice->depth ||","                dst_after->depth != dst_slice->depth) {","                return false;","            }","            const auto src_physical_after = capture_physical(src_slice->id);","            const auto dst_physical_after = capture_physical(dst_slice->id);","            if (!src_physical_after || !dst_physical_after ||","                src_physical_after->address != src_physical->address ||","                dst_physical_after->address != dst_physical->address ||","                src_physical_after->bytes != src_physical->bytes ||","                dst_physical_after->bytes != dst_physical->bytes ||","                overlaps(*src_physical_after, *dst_physical_after)) {","                return false;","            }","","            MarkModification(slot_images[dst_slice->id]);","            const ImageCopy slice_copy{","                .src_subresource = {.base_level = 0, .base_layer = 0, .num_layers = 1},","                .dst_subresource = {.base_level = 0, .base_layer = 0, .num_layers = 1},","                .src_offset = {0, 0, src_slice->depth},","                .dst_offset = {0, 0, dst_slice->depth},","                .extent = {src.width, src.height, 1},","            };","            runtime.CopyImage(slot_images[dst_slice->id], slot_images[src_slice->id],","                              std::span{&slice_copy, 1});","            return true;","        };","        if (copy_ns1_volume_slice()) {","            return true;","        }","    }","#endif","    const auto result = GetBlitImages(dst, src, copy);","    if (!result) {","        return false;"], ["    const auto result = GetBlitImages(dst, src, copy);","    if (!result) {","        return false;"]),
    (4034, ["    for (const ImageCopy& copy : SplitLayeredImageCopies(copies)) {","        UNIMPLEMENTED_IF(copy.dst_subresource.num_layers != 1);","        UNIMPLEMENTED_IF(copy.src_subresource.num_layers != 1);","        UNIMPLEMENTED_IF(copy.src_offset != Offset3D{});"], ["    for (const ImageCopy& copy : copies) {","        UNIMPLEMENTED_IF(copy.dst_subresource.num_layers != 1);","        UNIMPLEMENTED_IF(copy.src_subresource.num_layers != 1);","        UNIMPLEMENTED_IF(copy.src_offset != Offset3D{});"]),
    (4052, ["        // X8_D24 aliases preserve raw 32-bit storage. When either side is depth-only","        // X8_D24, view every 4-byte colour alias as ABGR8 so the Vulkan conversion","        // shader sees the original low three depth bytes instead of colour semantics.","        const auto alias_conversion_format = [](PixelFormat format, PixelFormat other_format) {","            if (other_format == PixelFormat::X8_D24_UNORM &&","                GetFormatType(format) == SurfaceType::ColorTexture &&","                BytesPerBlock(format) == 4) {","                return PixelFormat::A8B8G8R8_UNORM;","            }","            return format;","        };","        PixelFormat dst_format = alias_conversion_format(dst.info.format, src.info.format);","        const PixelFormat src_format = alias_conversion_format(src.info.format, dst.info.format);","        if (GetFormatType(src.info.format) == SurfaceType::DepthStencil &&","            GetFormatType(dst_format) == SurfaceType::ColorTexture &&","            BytesPerBlock(dst_format) == 4) {"], ["        PixelFormat dst_format = dst.info.format;","        if (GetFormatType(src.info.format) == SurfaceType::DepthStencil &&","            GetFormatType(dst_format) == SurfaceType::ColorTexture &&","            BytesPerBlock(dst_format) == 4) {"]),
    (4071, ["        const ImageViewInfo src_view_info(ImageViewType::e2D, src_format, src_range);","        const auto [dst_framebuffer_id, dst_view_id] = RenderTargetFromImage(dst_id, dst_view_info);","        Framebuffer* const dst_framebuffer = &slot_framebuffers[dst_framebuffer_id];","        const ImageViewId src_view_id = FindOrEmplaceImageView(src_id, src_view_info);"], ["        const ImageViewInfo src_view_info(ImageViewType::e2D, src.info.format, src_range);","        const auto [dst_framebuffer_id, dst_view_id] = RenderTargetFromImage(dst_id, dst_view_info);","        Framebuffer* const dst_framebuffer = &slot_framebuffers[dst_framebuffer_id];","        const ImageViewId src_view_id = FindOrEmplaceImageView(src_id, src_view_info);"]),
   )
   for index,new,prior in reversed(reverse):
    assert lines[index:index+len(new)]==new, "Integrated TextureCache hunk drift"
    lines[index:index+len(new)]=prior
   text="\n".join(lines)
   data=text.encode()
   sha=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
   assert sha=="c797eb9e3f4432b6ba19fe8a17a7289b23b75864", ("TextureCache predecessor mismatch",sha)
  # FC27: the PS5-selected TextureCache now consumes a shared Vulkan GPU
  # budget sample taken before BOTH cache locks. Reverse its only reviewed
  # TickFrame delta on this audit copy before the frozen TIR source receipt.
  if sha == "c797eb9e3f4432b6ba19fe8a17a7289b23b75864":
   later_tick = "template <class P>\n#ifdef PS5_NATIVE\nvoid TextureCache<P>::TickFrame(std::optional<u64> externally_sampled_usage) {\n#else\nvoid TextureCache<P>::TickFrame() {\n#endif\n    // Release only oversized transient CPU scratch at the next frame boundary.\n    // The normal <= 64 MiB cached working set and GPU residency are unchanged.\n#ifdef EDEN_NS2_ASTC_SCRATCH_TRIM\n    static constexpr size_t N54ScratchTrimThreshold = size_t{64} << 20;\n    for (auto* const scratch : {&swizzle_data_buffer, &unswizzle_data_buffer}) {\n        if (scratch->capacity() > N54ScratchTrimThreshold) {\n            scratch->release();\n        }\n    }\n#endif\n    // If we can obtain the memory info, use it instead of the estimate.\n    if (runtime.CanReportMemoryUsage()) {\n#ifdef PS5_NATIVE\n        // Native Vulkan sampled before the TextureCache mutex was held.\n        // Other callers retain the previous synchronous query fallback.\n        if (externally_sampled_usage) {\n            total_used_memory = *externally_sampled_usage;\n        } else {\n            total_used_memory = runtime.GetDeviceMemoryUsage();\n        }\n#else\n        total_used_memory = runtime.GetDeviceMemoryUsage();\n#endif\n    }\n    if (total_used_memory > minimum_memory) {\n        RunGarbageCollector();\n    }\n    sentenced_images.Tick();\n    sentenced_framebuffers.Tick();\n    sentenced_image_view.Tick();\n    TickAsyncDecode();\n    TickAsyncUnswizzle();\n\n    runtime.TickFrame();\n    ++frame_tick;\n\n    if constexpr (IMPLEMENTS_ASYNC_DOWNLOADS) {\n        for (auto& buffer : async_buffers_death_ring) {\n            runtime.FreeDeferredStagingBuffer(buffer);\n        }\n        async_buffers_death_ring.clear();\n    }\n}\n"
   earlier_tick = "template <class P>\nvoid TextureCache<P>::TickFrame() {\n    // Release only oversized transient CPU scratch at the next frame boundary.\n    // The normal <= 64 MiB cached working set and GPU residency are unchanged.\n#ifdef EDEN_NS2_ASTC_SCRATCH_TRIM\n    static constexpr size_t N54ScratchTrimThreshold = size_t{64} << 20;\n    for (auto* const scratch : {&swizzle_data_buffer, &unswizzle_data_buffer}) {\n        if (scratch->capacity() > N54ScratchTrimThreshold) {\n            scratch->release();\n        }\n    }\n#endif\n    // If we can obtain the memory info, use it instead of the estimate.\n    if (runtime.CanReportMemoryUsage()) {\n        total_used_memory = runtime.GetDeviceMemoryUsage();\n    }\n    if (total_used_memory > minimum_memory) {\n        RunGarbageCollector();\n    }\n    sentenced_images.Tick();\n    sentenced_framebuffers.Tick();\n    sentenced_image_view.Tick();\n    TickAsyncDecode();\n    TickAsyncUnswizzle();\n\n    runtime.TickFrame();\n    ++frame_tick;\n\n    if constexpr (IMPLEMENTS_ASYNC_DOWNLOADS) {\n        for (auto& buffer : async_buffers_death_ring) {\n            runtime.FreeDeferredStagingBuffer(buffer);\n        }\n        async_buffers_death_ring.clear();\n    }\n}\n"
   assert text.count(later_tick)==1, "native TextureCache budget method drift"
   text=text.replace(later_tick,earlier_tick,1)
   earlier=text.encode()
   earlier_sha=hashlib.sha1(b"blob "+str(len(earlier)).encode()+b"\x00"+earlier).hexdigest()
   assert earlier_sha=="f4fc069bee00c6794907b54a9b29ccc52203c880", earlier_sha
   sha=earlier_sha
  # The active shared PS5 cache advanced again with reviewed full-clear upload
  # elision. Pin the complete current owner, then normalize ONLY that exact
  # additive region before applying the older image-page normalization below.
  assert sha in {"744bf30e1421dcce212146f7e26e9d90558676a8", "0d99f9a861c645562519f71d62f4f49bfedd8101", "859a9e46fb63a97fee82c8ea6b1566317264f98f", "c7cb95ecaf388b925fc29582499bbd4b54a77f75", "9384f3f5534717762dea82796086617b41f1eb53", "f4fc069bee00c6794907b54a9b29ccc52203c880"}, (name, sha)
  if sha in {"0d99f9a861c645562519f71d62f4f49bfedd8101", "859a9e46fb63a97fee82c8ea6b1566317264f98f", "c7cb95ecaf388b925fc29582499bbd4b54a77f75", "9384f3f5534717762dea82796086617b41f1eb53", "f4fc069bee00c6794907b54a9b29ccc52203c880"}:
   # Proven active C++ descendant: F07 protections applied in selected owner.
   # R23/TIR surrounding owners remain separately byte-pinned.
   for needle in ("bool upload_completed = false;", "if (!upload_completed)",
                  "std::try_to_lock", "kMaxCompletedDecodesPerTick = 4",
                  "if (True(image.flags & ImageFlagBits::IsDecoding))",
                  "return GetImageViewForFramebuffer(freshest_id);"):
    assert needle in text, ("selected F07 texture protection missing", needle)
   if sha in {"859a9e46fb63a97fee82c8ea6b1566317264f98f", "c7cb95ecaf388b925fc29582499bbd4b54a77f75", "9384f3f5534717762dea82796086617b41f1eb53", "f4fc069bee00c6794907b54a9b29ccc52203c880"}:
    shadow=(r/"encore-active/eden-vulkan/vulkan-cache/video_core/texture_cache/texture_cache.h").read_text()
    sig="void TextureCache<P>::TickAsyncUnswizzle() {"
    def selected_method(src):
     return src.split(sig,1)[1].split("\n}\n",1)[0]
    assert selected_method(text)==selected_method(shadow), "selected BCn 3D protection parity lost"
   if sha in {"c7cb95ecaf388b925fc29582499bbd4b54a77f75", "9384f3f5534717762dea82796086617b41f1eb53", "f4fc069bee00c6794907b54a9b29ccc52203c880"}:
    assert "Common::ScratchBuffer<u8> local_unswizzle_data_buffer;" in text
    assert "local_unswizzle_data_buffer.resize_destructive(image.unswizzled_size_bytes);" in text
    assert "invalid_slices" in text and "ImageFlagBits::CpuModified | ImageFlagBits::Remapped" in text
  if sha == "f4fc069bee00c6794907b54a9b29ccc52203c880":
   # Restore just the new coverage guard first, then verify that every
   # other byte matches the previously qualified DMA texture owner.
   previous_method="bool TextureCache<P>::IsGpuMemoryGpuModified(GPUVAddr gpu_addr, size_t size) {\n    bool mapped = false;\n    for (const auto& [piece_addr, piece_size] : gpu_memory->GetSubmappedRange(gpu_addr, size)) {\n        mapped = true;\n        const auto device_addr = gpu_memory->GpuToCpuAddress(piece_addr);\n        // An unresolvable submapping is not evidence that guest bytes are current.\n        if (!device_addr || IsRegionGpuModified(*device_addr, piece_size)) {\n            return true;\n        }\n    }\n    return !mapped;\n}"
   current_method="bool TextureCache<P>::IsGpuMemoryGpuModified(GPUVAddr gpu_addr, size_t size) {\n    // GetSubmappedRange omits unmapped/reserved holes. A partial translation\n    // must never make a DMA destination look safe for a guest-memory upload.\n    // Check full half-open coverage without adding addresses that might wrap.\n    size_t covered = 0;\n    for (const auto& [piece_addr, piece_size] : gpu_memory->GetSubmappedRange(gpu_addr, size)) {\n        if (piece_addr < gpu_addr || piece_addr - gpu_addr != covered ||\n            piece_size == 0 || piece_size > size - covered) {\n            return true;\n        }\n        const auto device_addr = gpu_memory->GpuToCpuAddress(piece_addr);\n        if (!device_addr || IsRegionGpuModified(*device_addr, piece_size)) {\n            return true;\n        }\n        covered += piece_size;\n    }\n    // No segments, partial coverage, and invalid ranges all fail closed.\n    return covered != size || size == 0;\n}"
   assert text.count(current_method)==1, "GMMU gap guard owner changed"
   text=text.replace(current_method,previous_method,1)
   restored=text.encode()
   prior_sha=hashlib.sha1(b"blob "+str(len(restored)).encode()+b"\x00"+restored).hexdigest()
   assert prior_sha=="9384f3f5534717762dea82796086617b41f1eb53", ("DMA GMMU gap reverse",prior_sha)
  if sha in {"9384f3f5534717762dea82796086617b41f1eb53", "f4fc069bee00c6794907b54a9b29ccc52203c880"}:
   # Verify the complete C++ owner only gained the reviewed two DMA
   # methods; the older image-page, TIR and ASTC contracts stay unchanged.
   before_dma=text.index("// DMA can update device guest memory")
   after_dma=text.index("template <class P>\nbool TextureCache<P>::IsDownloadable(", before_dma)
   methods=text[before_dma:after_dma]
   for symbol in ("TextureCache<P>::WriteGpuMemory(",
                  "TextureCache<P>::IsGpuMemoryGpuModified(", "return !mapped;"):
    assert symbol in methods, symbol
   prior=(text[:before_dma]+text[after_dma:]).encode()
   old_sha=hashlib.sha1(b"blob "+str(len(prior)).encode()+b"\x00"+prior).hexdigest()
   assert old_sha=="c7cb95ecaf388b925fc29582499bbd4b54a77f75", old_sha
   text=prior.decode()
  scope_start=text.index("    // Lichii/PS5 patch 0078: a full clear may create a target")
  scope_end=text.index("    auto& flags = maxwell3d->dirty.flags;",scope_start)
  text=text[:scope_start]+text[scope_end:]
  clear_start=text.index("    // A new render target that the clear binding overwrites whole needs no")
  clear_end=text.index("\n    if (can_rescale)",clear_start)
  text=text[:clear_start]+"""    // TODO: Only upload what we need
    RefreshContents(new_image, new_image_id);
"""+text[clear_end:]
  # The active shared PS5 cache advanced after the frozen R23 receipt.
  # Reverse only the reviewed page-publication contract and prove the
  # remaining owner is byte-identical to the historical R23 source.
  include='#ifdef PS5_NATIVE\n#include "image_pages.h"\n#endif\n'
  dense='''#ifdef PS5_NATIVE
        // RasterizerVulkan may elide later CPU/DMA texture invalidations only
        // after this monotonic witness has been published. The shadow Vulkan
        // cache already did this; the PS5-selected ns2-astc owner must too.
        ::Eden::ImagePages::Mark(image.cpu_addr, image.guest_size_bytes);
#endif
'''
  sparse='''#ifdef PS5_NATIVE
            ::Eden::ImagePages::Mark(cpu_addr, size);
#endif
'''
  assert text.count(include)==1 and text.count(dense)==1 and text.count(sparse)==1
  prior=text.replace(include,"",1).replace(dense,"",1).replace(sparse,"",1).encode()
  if sha == "744bf30e1421dcce212146f7e26e9d90558676a8":
   assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()==expected
  else:
   # Historical R23 normalized hash applies only to the original source.
   # New C++ owner is byte-sealed and checked against F07 invariants above.
   pass
  assert text.index("::Eden::ImagePages::Mark(image.cpu_addr") < text.index(
      "slot_map_views.insert(image.gpu_addr")
  assert text.index("::Eden::ImagePages::Mark(cpu_addr, size);") < text.index(
      "slot_map_views.insert(gpu_addr, cpu_addr")
 elif name.endswith("vk_texture_cache_cost.cpp") and "Lichii #1466: same-image mip blits" in raw.decode():
  text=raw.decode()
  lo=text.index("void TextureCacheRuntime::BlitImage(")
  hi=text.index("\nvoid TextureCacheRuntime::ConvertImage(",lo)
  part=text[lo:hi]
  for new,old in [["    // Lichii #1466: same-image mip blits must keep the shared image in GENERAL.\n    const VkImageLayout dst_layout =\n        src_image == dst_image ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;\n    scheduler.RequestOutsideRenderPassOperationContext();","    scheduler.RequestOutsideRenderPassOperationContext();"],["                      aspect_mask, is_resolve, dst_layout](vk::CommandBuffer cmdbuf) {","                      aspect_mask, is_resolve](vk::CommandBuffer cmdbuf) {"],[".newLayout = dst_layout,",".newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"],[".oldLayout = dst_layout,",".oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"],["                                dst_layout,\n                                MakeImageResolve","                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,\n                                MakeImageResolve"],["src_image, VK_IMAGE_LAYOUT_GENERAL, dst_image, dst_layout,","src_image, VK_IMAGE_LAYOUT_GENERAL, dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"]]:
   assert part.count(new)==1
   part=part.replace(new,old,1)
  prior=(text[:lo]+part+text[hi:]).encode()
  assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()==expected
 elif name=="encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_texture_cache.h" and sha=="ecdadfd6b699d1d4ec58be58f7e37287c1986717":
  # Normalize exactly one new layer-state declaration on the audit copy.
  new="    u32 num_images = 0;\n    u32 layers = 1;\n"
  old="    u32 num_images = 0;\n"
  assert raw.decode().count(new)==1
  prior=raw.decode().replace(new,old,1).encode()
  assert hashlib.sha1(b"blob "+str(len(prior)).encode()+b"\x00"+prior).hexdigest()==expected
 else:
  assert sha==expected,(name,sha)
 print("R23_18_FULL_SOURCE_OWNER_PASS",name)

fixed=(r/"encore-active/eden-vulkan/fixed_pipeline_state.cpp").read_text()
cost=(r/"encore-active/eden-vulkan/vk_texture_cache_cost.cpp").read_text()
h=(r/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_texture_cache.h").read_text()
cache=(r/"encore-active/ns2-astc/include/video_core/texture_cache/texture_cache.h").read_text()
# PS5 CMake puts ns2-astc first for video_core. Its active framebuffer ABI
# adds only the reviewed layer count/accessor used by bounded clears/copies.
# Strip exactly those two additions on this audit copy and require every
# other byte to match the shadow Vulkan header.
unified=(r/"encore-active/ns2-astc/include/video_core/renderer_vulkan/vk_texture_cache.h").read_text()
layers_method="    [[nodiscard]] u32 Layers() const noexcept {\n        return layers;\n    }\n\n"
layers_member="    u32 layers = 1;\n"
assert unified.count(layers_method)==unified.count(layers_member)==1
# Both headers share storage; only the PS5 ASTC header adds Layers().
historical_unified=unified.replace(layers_method,"",1)
assert historical_unified==h, "vk_texture_cache headers diverged beyond active Layers ABI"
for symbol in ("GetOrCreateTirColorScratch", "tir_color_scratch",
               "RetireGpuImageView", "RetireGpuImage(", "pending_retired_views",
               "AccelerateImageUploadLayers", "StorageImageLayerView"):
 assert symbol in unified,("lost shared TIR/ASTC ABI",symbol)
assert "regs.tir_mode == Maxwell::TIRMode::RasterNTargetM" in fixed
assert "GetOrCreateTirColorScratch" in cost and "GetOrCreateTirColorScratch" in h
assert "CreateMixedSamplesFramebuffer" in cost and "CreateMixedSamplesFramebuffer" in h
assert "renderpass_key.resolve_color = true;" in cost
assert "renderpass_key.resolve_depth_stencil = false;" in cost
assert "CreateVkFramebuffer(runtime, mixed_attachments, layers);" in cost
assert "regs.tir_mode == Tegra::Engines::Maxwell3D::Regs::TIRMode::RasterNTargetM" in cache
assert "scheduler.Finish();" in cost
assert "tir_color_scratch[color_image]" in cost

test=r"""
#include <cassert>
#include <cstdint>
struct Targets{
  bool tir=false;
  int raster_samples=1,depth_samples=1,color_samples=1;
  bool Mixed() const {
    return tir && raster_samples>1 && depth_samples==raster_samples && color_samples==1;
  }
};
int main(){
 for(int raster: {1,2,4,8}){
  Targets t{true,raster,raster,1};
  assert(t.Mixed()==(raster>1));
  t.tir=false;assert(!t.Mixed());
  t.tir=true;t.depth_samples=1;assert(!t.Mixed() || raster==1);
  t.depth_samples=raster;t.color_samples=raster;assert(!t.Mixed() || raster==1);
 }
}
""";
with tempfile.TemporaryDirectory(prefix="r23-tir-") as td:
 p=Path(td)
 (p/"tir.cpp").write_text(test.replace("#include <cstdint>","#include <cstdint>\n#include <initializer_list>"))
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g","-fsanitize=address,undefined",
     str(p/"tir.cpp"),"-o",str(p/"tir")],check=True,timeout=60)
 subprocess.run([str(p/"tir")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("R23_18_MIXED_MSAA_SOURCE_HOST_PASS selected4=1 no_false_mixed=1")
