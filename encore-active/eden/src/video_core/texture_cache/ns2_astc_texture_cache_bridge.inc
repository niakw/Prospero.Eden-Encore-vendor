// SPDX-License-Identifier: GPL-3.0-or-later
// N60 research-only generic ASTC layer refresh methods. Included exactly once
// by the maintained TextureCache template owner with ASTC_CACHE_BRIDGE enabled.
// This file is intentionally not a separately compiled translation unit.
template <class P>
bool TextureCache<P>::Ns2AstcEligible(const ImageBase& image) const noexcept {
    if constexpr (requires { &Runtime::AccelerateImageUploadLayers; }) {
        constexpr ImageFlagBits unsafe = ImageFlagBits::Sparse |
            ImageFlagBits::Remapped | ImageFlagBits::Alias |
            ImageFlagBits::GpuModified | ImageFlagBits::Converted |
            ImageFlagBits::Rescaled | ImageFlagBits::AsynchronousDecode |
            ImageFlagBits::IsDecoding;
        return True(image.flags & ImageFlagBits::AcceleratedUpload) &&
            IsPixelFormatASTC(image.info.format) && False(image.flags & unsafe) &&
            image.info.type == ImageType::e2D &&
            image.info.resources.levels > 0 && image.info.resources.levels <= 16 &&
            image.info.resources.layers > 1 && image.info.resources.layers <= 4096 &&
            image.info.layer_stride > 0 && image.info.layer_stride <= (u64{64} << 20) &&
            image.guest_size_bytes >= (u64{64} << 20) && image.cpu_addr != 0;
    }
    return false;
}

template <class P>
bool TextureCache<P>::Ns2UploadDirtyLayers(Image& image) {
    if constexpr (requires { &Runtime::AccelerateImageUploadLayers; }) {
        if (!image.ns2_astc_dirty || !image.ns2_astc_dirty->Valid())
            return false;
        const auto swizzles = FullUploadSwizzles(image.info);
        // N57 has no pinned host_level_offset and rejects trimmed mip chains.
        if (swizzles.empty() || swizzles.front().level != 0 ||
            swizzles.front().buffer_offset != 0)
            return false;

        std::optional<decltype(runtime.UploadStagingBuffer(size_t{}))> staging;
        const auto result = Eden::NS2::Eglantine::UploadAstcDirtyLayerBatches(
            *image.ns2_astc_dirty, image.gpu_addr,
            [&](u32, u64 bytes) {
                staging.emplace(runtime.UploadStagingBuffer(static_cast<size_t>(bytes)));
                return staging && staging->mapped_span.size_bytes() >= bytes;
            },
            [&](u64 guest, u64 offset, u64 count) {
                if (!staging || offset > staging->mapped_span.size_bytes() ||
                    count > staging->mapped_span.size_bytes() - offset)
                    return false;
                gpu_memory->ReadBlock(guest, staging->mapped_span.data() + offset, count,
                                      VideoCommon::CacheType::NoTextureCache);
                return true;
            },
            [&](u32, std::span<const u32> layers) {
                if (!staging)
                    return false;
                runtime.AccelerateImageUploadLayers(
                    image, *staging, FixSmallVectorADL(swizzles), layers);
                // Flush submits Vulkan; it does not necessarily retire commands.
                // Do not free or reuse staging before the GPU completion fence.
                runtime.Finish();
                staging.reset();
                return true;
            });
        return result.status == Eden::NS2::Eglantine::AstcTransferStatus::Completed ||
               result.status == Eden::NS2::Eglantine::AstcTransferStatus::NoChanges;
    }
    return false;
}
