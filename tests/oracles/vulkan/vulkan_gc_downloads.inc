    // Images the GPU wrote are copied back to guest memory before they go, behind a wait for the
    // GPU. Eden evicts them from its "expected" mark on; here they stay, as they do below that
    // mark, until memory is really short (performance.h, KeepDirtyTextures).
    const auto DirtyEvictions = [&] {
        return high_priority_mode &&
               (total_used_memory >= critical_memory || !::Eden::Performance::KeepDirtyTextures());
    };
    using DownloadMap = decltype(runtime.DownloadStagingBuffer(size_t{}));
    // Aggressive GC can process 40 images per pass. Keep those readbacks in
    // ONE scheduled batch instead of falling back to a Finish per image after
    // the former 16-descriptor cap. The separate 32-MiB BYTE cap is unchanged.
    constexpr size_t gc_prefetch_limit = 40;
    boost::container::small_vector<std::pair<ImageId, DownloadMap>, gc_prefetch_limit> gc_downloads;
    const auto ReleaseDownloads = [&] {
        if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {
            for (auto& [id, map] : gc_downloads)
                runtime.FreeDeferredStagingBuffer(map);
            gc_downloads.clear();
        }
    };
    SCOPE_EXIT { ReleaseDownloads(); };
    const auto PrefetchDownloads = [&] {
        if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {
            ReleaseDownloads();
            // No texture can be old enough before this title's first
            // age threshold. Do not even consult the PS5 direct-memory
            // pressure callback if the LRU cannot select any image.
            if (frame_tick < ticks_to_destroy) return;
            // Only prefetch GPU-written textures when actual Cleanup could
            // evict them. Clean-only cleanup still runs independently.
            // One pressure decision for this batch, independent of LRU scanning.
            // R442: the R439 collector skips non-evictable LRU candidates without
            // burning the deletion budget; prefetch must use the SAME rules.
            const bool prefetch_dirty = DirtyEvictions();
            if (!prefetch_dirty) return;
            size_t remaining = num_iterations;
            size_t scanned = 0;
            const bool count_visits = ::Eden::Performance::gc_count_visits.load(std::memory_order_relaxed);
            size_t bytes = 0;
            u64 usage = total_used_memory;
            // A stable target for THIS speculative pass. The actual Cleanup
            // still makes authoritative eviction decisions using current
            // GPU pressure; this staging forecast cannot free any texture.
            // Avoid one kernel-backed pressure check per LRU candidate.
            const u64 projected_stop = (aggressive_mode || ::Eden::Performance::KeepDirtyTextures())
                                           ? critical_memory : expected_memory;
            // Model both clean and dirty future evictions, but only submit
            // readbacks that Cleanup could actually need. This estimate
            // never changes a live GPU/guest texture or its synchronization.
            const auto NoteProjectedEviction = [&](const Image& image) {
                usage = ::Eden::VulkanMemory::AfterProjectedEviction(
                    usage, ReclaimedBytes(image));
                return usage < projected_stop;
            };
            // Only look ahead through eligible evictions. Leave actual
            // deletion, alias updates and guest writes to Cleanup.
            lru_cache.ForEachItemBelow(frame_tick - ticks_to_destroy, [&](ImageId id) {
                if (remaining == 0 || scanned >= max_scanned) return true;
                ++scanned;
                if (count_visits) --remaining;
                auto& image = slot_images[id];
                if (True(image.flags & ImageFlagBits::IsDecoding)) return false;
                const bool dirty = IsDownloadable(image) && False(image.flags & ImageFlagBits::BadOverlap);
                // The prefetch pass was admitted from one stable pressure
                // snapshot above. Per-image reprobing is unnecessary and can
                // stall the GPU thread while walking up to 40 LRU entries.
                if (!aggressive_mode && True(image.flags & ImageFlagBits::CostlyLoad))
                    return false;
                // Only real collector eviction candidates spend the allowance;
                // the gc_scan=upstream rollback intentionally counts ALL visits.
                if (!count_visits) --remaining;
                // A clean or unsupported image is still visited by the
                // real Cleanup pass, but cannot use this staged fast path.
                // Skip it, do NOT stop scanning older-to-newer LRU entries:
                // the following dirty images can still share one Finish().
                // Scaled textures use the SAME Image::DownloadMemory path as
                // Cleanup: it scales down before the copy to guest layout and
                // restores the original image. Do not force one GPU Finish per
                // scaled image. Aliases, overlaps and MSAA remain excluded.
                // Keep our tested 40-image / 32-MiB shared-memory budget:
                // oversized images still use Cleanup's individual download,
                // without preventing later independent small images from
                // sharing the same GPU completion fence.
                if (!dirty || !image.aliased_images.empty() || !image.overlapping_images.empty() ||
                    image.info.num_samples != 1 ||
                    image.unswizzled_size_bytes > 32_MiB)
                    return NoteProjectedEviction(image);
                // Full descriptor/byte capacity is a true batch boundary.
                if (gc_downloads.size() == gc_prefetch_limit ||
                    image.unswizzled_size_bytes > 32_MiB - bytes) return true;
                auto map = runtime.DownloadStagingBuffer(image.unswizzled_size_bytes, true);
                // The pinned StagingBufferRef is a POD handle, not a RAII
                // owner. The pool tracks deferred lifetime until FreeDeferred.
                gc_downloads.emplace_back(id, map);
                const auto copies = FixSmallVectorADL(FullDownloadCopies(image.info));
                image.DownloadMemory(map, copies);
                bytes += image.unswizzled_size_bytes;
                // Same projected accounting for the staged dirty image,
                // including saturation when tracked and driver usage differ.
                return NoteProjectedEviction(image);
            });
            if (!gc_downloads.empty()) runtime.Finish();
        }
    };
    const auto UsePrefetched = [&](ImageId id, Image& image) {
        if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {
            const auto it = std::ranges::find_if(gc_downloads, [id](const auto& entry) {
                return entry.first == id;
            });
            if (it != gc_downloads.end()) {
                const auto copies = FixSmallVectorADL(FullDownloadCopies(image.info));
                SwizzleImage(*gpu_memory, image.gpu_addr, image.info, copies,
                             it->second.mapped_span, swizzle_data_buffer);
                runtime.FreeDeferredStagingBuffer(it->second);
                gc_downloads.erase(it);
                return true;
            }
        }
        return false;
    };
