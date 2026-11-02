// Runs on the existing GPU worker, before the first guest Composite. Reuse
// its swapchain: this is the SAME selected game's last launcher composition,
// not a second Vulkan-specific loading animation or guest framebuffer.
void RendererVulkan::PresentLoading() {
    Frame* frame = present_manager.GetRenderFrame();
    scheduler.RequestOutsideRenderPassOperationContext();
    blit_swapchain.DrawToFrame(device, rasterizer, frame, {},
                              render_window.GetFramebufferLayout(), present_manager.SwapchainImageCount(),
                              present_manager.SwapchainImageViewFormat(), true);
#ifdef PS5_NATIVE
    // Do not consume the screenshot while the swapchain has zero extent.
    // A transient staging failure may retry at most three times; no guest
    // work or network request occurs on this launcher-render path.
    if (!loading_jacket_attempted &&
        frame->width > 0 && frame->height > 0 &&
        frame->width <= 4096 && frame->height <= 2160) {
        auto jacket = Eden::LaunchHandoff::Peek();
        const VkFormat format = present_manager.SwapchainImageViewFormat();
        const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM ||
                          format == VK_FORMAT_B8G8R8A8_SRGB;
        const bool rgba = format == VK_FORMAT_R8G8B8A8_UNORM ||
                          format == VK_FORMAT_R8G8B8A8_SRGB ||
                          format == VK_FORMAT_A8B8G8R8_UNORM_PACK32 ||
                          format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
        if (jacket && Eden::LaunchHandoff::Valid(*jacket) &&
            (bgra || rgba)) {
            ++loading_jacket_attempts;
            const std::size_t row_bytes =
                static_cast<std::size_t>(frame->width) * 4;
            const VkDeviceSize bytes =
                static_cast<VkDeviceSize>(row_bytes) * frame->height;
            try {
                const VkBufferCreateInfo buffer_info{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .size = bytes,
                    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                    .queueFamilyIndexCount = 0,
                    .pQueueFamilyIndices = nullptr,
                };
                auto buffer = memory_allocator.CreateBuffer(
                    buffer_info, MemoryUsage::Upload);
                auto mapped = buffer.Mapped();
                if (mapped.size() >= bytes) {
                    // Bounded one-shot resample from launcher size to the
                    // game's actual Vulkan output; no allocations in the
                    // guest-frame or subsequent loading-present hot path.
                    for (u32 y = 0; y < frame->height; ++y) {
                        const u32 source_y = static_cast<u32>(
                            static_cast<u64>(y) * jacket->height / frame->height);
                        for (u32 x = 0; x < frame->width; ++x) {
                            const u32 source_x = static_cast<u32>(
                                static_cast<u64>(x) * jacket->width / frame->width);
                            const std::size_t src =
                                (static_cast<std::size_t>(source_y) * jacket->width +
                                 source_x) * 4;
                            const std::size_t dst =
                                (static_cast<std::size_t>(y) * frame->width +
                                 x) * 4;
                            mapped[dst] = jacket->rgba[src + (bgra ? 2 : 0)];
                            mapped[dst + 1] = jacket->rgba[src + 1];
                            mapped[dst + 2] = jacket->rgba[src + (bgra ? 0 : 2)];
                            mapped[dst + 3] = jacket->rgba[src + 3];
                        }
                    }
                    buffer.Flush();
                    loading_jacket_width = frame->width;
                    loading_jacket_height = frame->height;
                    loading_jacket_buffer = std::move(buffer);
                    // Clear only AFTER the upload buffer is ready.
                    // Failed allocations preserve the title's last GL frame.
                    Eden::LaunchHandoff::Clear();
                    loading_jacket_attempted = true;
                    LOG_INFO(Render_Vulkan,
                             "EDEN_LOADER_JACKET_HANDOFF_VULKAN size={}x{} format={} ready=1",
                             loading_jacket_width, loading_jacket_height,
                             static_cast<u32>(format));
                }
            } catch (const std::exception& error) {
                LOG_WARNING(Render_Vulkan,
                            "EDEN_LOADER_JACKET_HANDOFF_VULKAN ready=0 reason={}",
                            error.what());
            }
            if (!loading_jacket_attempted && loading_jacket_attempts >= 3) {
                // A hard failure should release up to 34 MiB of screenshot
                // pixels, not retry allocation every 60 Hz indefinitely.
                Eden::LaunchHandoff::Clear();
                loading_jacket_attempted = true;
                LOG_WARNING(Render_Vulkan,
                            "EDEN_LOADER_JACKET_HANDOFF_VULKAN retries_exhausted=1");
            }
        } else if (!jacket || !Eden::LaunchHandoff::Valid(*jacket)) {
            loading_jacket_attempted = true;
        } else {
            // Format not supported by this byte-order conversion. Do not
            // submit a wrongly swizzled screenshot as if it were valid.
            Eden::LaunchHandoff::Clear();
            loading_jacket_attempted = true;
            LOG_WARNING(Render_Vulkan,
                        "EDEN_LOADER_JACKET_HANDOFF_VULKAN unsupported_format={}",
                        static_cast<u32>(format));
        }
    }
    if (loading_jacket_buffer &&
        loading_jacket_width == frame->width &&
        loading_jacket_height == frame->height) {
        // DrawToFrame has ended its color pass and left the frame GENERAL;
        // PresentManager later expects GENERAL as the transfer source.
        const VkImage target = *frame->image;
        const VkBuffer upload = *loading_jacket_buffer;
        const VkBufferImageCopy copy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {frame->width, frame->height, 1},
        };
        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record([target, upload, copy](vk::CommandBuffer cmdbuf) {
            const VkImageMemoryBarrier to_upload{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = target,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, to_upload);
            cmdbuf.CopyBufferToImage(upload, target,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy);
            auto to_present = to_upload;
            to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_present.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_present.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                   0, to_present);
        });
    }
#endif
    scheduler.Flush(*frame->render_ready);
    present_manager.Present(frame);
    ::Eden::FrameMetrics::OnLoadingQueued();
}
