    if constexpr (std::is_same_v<Runtime, Vulkan::TextureCacheRuntime>) {
        // Keep independent copies in one submission, then publish guest writes
        // in modification order. A staging allocation stays busy until its tick.
        using Map = decltype(runtime.DownloadStagingBuffer(size_t{}));
        // Up to 40 small independent readbacks can now share one GPU
        // completion fence. Keep their total staging bytes capped at 32 MiB
        // and publish guest writes in their original modification order.
        // Pinned Vulkan StagingBufferRef is a non-owning value handle;
        // the staging pool owns the backing memory and its tick lifetime.
        constexpr size_t readback_batch_limit = 40;
        boost::container::small_vector<std::pair<ImageId, Map>, readback_batch_limit> pending;
        constexpr size_t batch_limit = 32_MiB;
        size_t batch_bytes = 0;
        const auto finish_batch = [&] {
            if (pending.empty()) return;
            runtime.Finish();
            for (auto& [image_id, map] : pending) {
                Image& image = slot_images[image_id];
                const auto copies = FixSmallVectorADL(FullDownloadCopies(image.info));
                SwizzleImage(*gpu_memory, image.gpu_addr, image.info, copies, map.mapped_span,
                             swizzle_data_buffer);
            }
            pending.clear();
            batch_bytes = 0;
        };
        for (const ImageId image_id : images) {
            Image& image = slot_images[image_id];
            const size_t bytes = image.unswizzled_size_bytes;
            if (pending.size() == readback_batch_limit || bytes > batch_limit - batch_bytes)
                finish_batch();
            auto map = runtime.DownloadStagingBuffer(bytes);
            const auto copies = FixSmallVectorADL(FullDownloadCopies(image.info));
            image.DownloadMemory(map, copies);
            pending.emplace_back(image_id, std::move(map));
            // Oversized images retain the original one-image allocation/wait.
            if (bytes >= batch_limit) {
                finish_batch();
            } else {
                batch_bytes += bytes;
            }
        }
        finish_batch();
        return;
    }
