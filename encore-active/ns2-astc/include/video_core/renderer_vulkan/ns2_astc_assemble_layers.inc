// SPDX-License-Identifier: GPL-3.0-or-later
// Maintained experimental N57 per-layer ASTC GPU decode owner.
// Included only by the versioned PS5 Vulkan compute source, never built alone.
// Partial uploads require N60 to hold staging until runtime.Finish retires GPU.
void ASTCDecoderPass::AssembleLayers(
    Image& image, const StagingBufferRef& map,
    std::span<const VideoCommon::SwizzleParameters> swizzles,
    std::span<const u32> layers) {
    using namespace VideoCommon::Accelerated;
    // Partial decoding is only valid after a successful full initialization.
    // No speculative UNDEFINED -> GENERAL transition may discard other layers.
    if (!image.IsInitializedForLayerUpload() ||
        image.info.type != VideoCommon::ImageType::e2D ||
        image.info.resources.layers <= 1 || image.info.resources.levels <= 0) {
        throw std::runtime_error("NS2 ASTC partial decode requires initialized 2D array");
    }
    const u64 stride = image.info.layer_stride;
    std::vector<Eden::NS2::Eglantine::AstcMipSlice> mips;
    mips.reserve(swizzles.size());
    for (const auto& swizzle : swizzles) {
        if (swizzle.level < 0) {
            throw std::runtime_error("NS2 ASTC negative mip level");
        }
        mips.push_back({swizzle.buffer_offset, static_cast<u32>(swizzle.level)});
    }
    if (!Eden::NS2::Eglantine::PreflightAstcLayerDecode(
            static_cast<u32>(image.info.resources.layers),
            static_cast<u32>(image.info.resources.levels), stride,
            map.mapped_span.size_bytes(), layers, mips)) {
        throw std::runtime_error("NS2 ASTC invalid partial staging or layer selection");
    }
    const std::array<u32, 2> block_dims{
        VideoCore::Surface::DefaultBlockWidth(image.info.format),
        VideoCore::Surface::DefaultBlockHeight(image.info.format),
    };
    scheduler.RequestOutsideRenderPassOperationContext();
    const VkPipeline vk_pipeline = *pipeline;
    const VkImageAspectFlags aspect_mask = image.AspectMask();
    const VkImage vk_image = image.Handle();
    scheduler.Record([vk_pipeline, vk_image, aspect_mask](vk::CommandBuffer cmdbuf) {
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk_image,
            .subresourceRange{
                .aspectMask = aspect_mask,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        };
        cmdbuf.PipelineBarrier(vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, barrier);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, vk_pipeline);
    });
    for (size_t index = 0; index < layers.size(); ++index) {
        for (const auto& swizzle : swizzles) {
            const VkDeviceSize input_offset =
                map.offset + index * stride + swizzle.buffer_offset;
            compute_pass_descriptor_queue.Acquire(scheduler, 2);
            compute_pass_descriptor_queue.AddBuffer(
                map.buffer, input_offset, stride - swizzle.buffer_offset);
            compute_pass_descriptor_queue.AddImage(
                image.StorageImageLayerView(swizzle.level, layers[index]));
            const void* const descriptor_data{compute_pass_descriptor_queue.UpdateData()};
            const auto params = MakeBlockLinearSwizzle2DParams(swizzle, image.info);
            ASSERT(params.origin == (std::array<u32, 3>{0, 0, 0}));
            ASSERT(params.destination == (std::array<s32, 3>{0, 0, 0}));
            ASSERT(params.bytes_per_block_log2 == 4);
            const u32 dx = Common::DivCeil(swizzle.num_tiles.width, 8U);
            const u32 dy = Common::DivCeil(swizzle.num_tiles.height, 8U);
            scheduler.Record([this, dx, dy, block_dims, params, descriptor_data](
                                 vk::CommandBuffer cmdbuf) {
                const AstcPushConstants uniforms{
                    .blocks_dims = block_dims,
                    .layer_stride = params.layer_stride,
                    .block_size = params.block_size,
                    .x_shift = params.x_shift,
                    .block_height = params.block_height,
                    .block_height_mask = params.block_height_mask,
                };
                const VkDescriptorSet set = descriptor_allocator.Commit();
                device.GetLogical().UpdateDescriptorSet(set, *descriptor_template, descriptor_data);
                cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, *layout, 0, set, {});
                cmdbuf.PushConstants(*layout, VK_SHADER_STAGE_COMPUTE_BIT, uniforms);
                cmdbuf.Dispatch(dx, dy, 1);
            });
        }
    }
    scheduler.Record([vk_image, aspect_mask](vk::CommandBuffer cmdbuf) {
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk_image,
            .subresourceRange{
                .aspectMask = aspect_mask,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        };
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               vk::PIPELINE_STAGE_GRAPHICS_COMPUTE, 0, barrier);
    });
    { auto timer = Eden::Performance::VulkanTimer(12); scheduler.Flush(); }
}

