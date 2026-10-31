// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N69: research-only bridge to the native PS5 Vulkan queue and VMA allocator.
// Include from the native Vulkan translation unit, never a release path.
#include <atomic>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#include "video_core/renderer_vulkan/ns2_sparse_layer_budget.h"

namespace Eden::NS2::Eglantine {
// Owns the memory tokens for a single sparse image until a verified GPU-idle
// and image-destruction transition. No implicit destructor release: it would
// create a use-after-free if a Vulkan fence timed out or the GPU was lost.
template <typename Allocator>
class SparseBindOwner final {
public:
    using Allocation = typename Allocator::SparseMemoryAllocation;
    SparseBindOwner(Allocator& allocator, SparseLayerBudget& budget, SparseImageSpec spec)
        : allocator_{allocator}, budget_{budget}, spec_{spec} {}
    SparseBindOwner(const SparseBindOwner&) = delete;
    SparseBindOwner& operator=(const SparseBindOwner&) = delete;
    [[nodiscard]] bool ReadyForSampling() const noexcept {
        return ready_ && !budget_.Poisoned();
    }
    [[nodiscard]] bool HasLiveBacking() const noexcept { return !backing_.empty(); }
    // All memory bind offsets are within one VMA chunk. Caller first ensures
    // that its scheduler submitted earlier uses, and synchronizes consumers
    // using N67's wait/signal semaphores (or a completed fence).
    template <typename Queue, typename Fence>
    [[nodiscard]] VkResult Bind(
        VkImage image, const VkMemoryRequirements& image_memory,
        const std::vector<VkSparseImageMemoryRequirements>& sparse_requirements,
        const SparseLayerBindPlan& logical, const Queue& queue,
        const Fence& fence, VkSemaphore wait = VK_NULL_HANDLE,
        VkSemaphore signal = VK_NULL_HANDLE,
        std::uint64_t timeout_ns = 5'000'000'000ULL) {
        // A Vulkan image may expose separate color/metadata sparse aspects.
        // This owner currently binds only color; refuse multi-aspect images
        // instead of accidentally leaving required metadata memory unbound.
        if (sparse_requirements.size() != 1) return VK_ERROR_INITIALIZATION_FAILED;
        const auto& sparse = sparse_requirements.front();
        // Bind ownership is per VkImage: a second cache image cannot borrow
        // the first image's resident-layer budget and VMA allocations.
        if (image_ && image_ != image) return VK_ERROR_INITIALIZATION_FAILED;
        if (!image || !timeout_ns || budget_.Poisoned() || !budget_.Valid() ||
            spec_.layers <= 1 || spec_.levels == 0 ||
            !image_memory.size || !image_memory.memoryTypeBits ||
            !image_memory.alignment ||
            (image_memory.alignment & (image_memory.alignment - 1)) ||
            spec_.tile_alignment != image_memory.alignment ||
            !sparse.formatProperties.aspectMask ||
            sparse.formatProperties.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
            sparse.formatProperties.imageGranularity.width != spec_.tile_width ||
            sparse.formatProperties.imageGranularity.height != spec_.tile_height ||
            sparse.formatProperties.imageGranularity.depth != 1 ||
            sparse.imageMipTailFirstLod != spec_.tail_first_lod ||
            sparse.imageMipTailSize != spec_.tail_bytes ||
            (bool(sparse.formatProperties.flags &
                  VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) !=
             spec_.single_mip_tail) ||
            fence.GetStatus() != VK_NOT_READY) return VK_ERROR_INITIALIZATION_FAILED;
        // A duplicate submit would either cause an invalid page alias or a
        // mismatched logical budget generation; keep all outstanding tokens.
        const auto current = budget_.Plan(logical.new_layers);
        if (current.status != SparsePlanStatus::Ready ||
            current.generation != logical.generation ||
            current.new_layers != logical.new_layers ||
            current.bytes_new != logical.bytes_new ||
            current.bind_shared_tail != logical.bind_shared_tail) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        const auto physical = MakeSparsePhysicalBindPlan(
            spec_, logical, sparse.imageMipTailOffset,
            sparse.imageMipTailStride, image_memory.size);
        if (!physical.valid || physical.backing_bytes == 0 ||
            physical.backing_bytes > image_memory.size) return VK_ERROR_INITIALIZATION_FAILED;
        std::vector<VkSparseImageMemoryBind> tiles;
        std::vector<VkSparseMemoryBind> tails;
        tiles.reserve(physical.tiles.size());
        tails.reserve(physical.tails.size());
        // Do not allocate device memory until every host-side list is ready.
        backing_.reserve(backing_.size() + 1);
        const VkMemoryRequirements page_request{
            .size = physical.backing_bytes,
            .alignment = image_memory.alignment,
            .memoryTypeBits = image_memory.memoryTypeBits,
        };
        Allocation token{};
        if (!allocator_.AllocateSparseMemory(page_request, token)) {
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        if (!token.memory || !token.allocation ||
            token.offset % image_memory.alignment ||
            token.offset > (std::numeric_limits<VkDeviceSize>::max)() -
                               physical.backing_bytes) {
            allocator_.ReleaseSparseMemory(token);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        std::uint64_t cursor = 0;
        for (const auto& tile : physical.tiles) {
            tiles.push_back(VkSparseImageMemoryBind{
                .subresource = {VK_IMAGE_ASPECT_COLOR_BIT, tile.level, tile.layer},
                .offset = {static_cast<std::int32_t>(tile.x),
                           static_cast<std::int32_t>(tile.y), 0},
                .extent = {tile.width, tile.height, 1},
                .memory = token.memory,
                .memoryOffset = token.offset + cursor,
                .flags = 0,
            });
            cursor += tile.bytes;
        }
        for (const auto& tail : physical.tails) {
            tails.push_back(VkSparseMemoryBind{
                .resourceOffset = tail.resource_offset,
                .size = tail.bytes,
                .memory = token.memory,
                .memoryOffset = token.offset + cursor,
                .flags = 0,
            });
            cursor += tail.bytes;
        }
        if (cursor != physical.backing_bytes) {
            allocator_.ReleaseSparseMemory(token);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        // After this point, even an error or timeout may mean queued GPU
        // work. Retain VMA memory until explicit device-idle retirement.
        backing_.push_back(token);
        image_ = image;
        const VkResult result = queue.BindSparseImage(
            image, tiles, tails, *fence, wait, signal);
        if (result != VK_SUCCESS) {
            ready_ = false;
            budget_.Poison();
            return result;
        }
        const VkResult completion = fence.Wait(timeout_ns);
        if (completion != VK_SUCCESS) {
            ready_ = false;
            budget_.Poison();
            return completion;
        }
        if (!budget_.Commit(logical)) {
            ready_ = false;
            budget_.Poison();
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        ready_ = true;
        return VK_SUCCESS;
    }
    // Caller must provide a callback that first drops all VkImage views and
    // the VkImage itself. Do not free bound memory while an image still exists.
    // If device idle fails, deliberately retain backing and poison the image.
    template <typename Device, typename DestroyImage>
    [[nodiscard]] bool RetireAfterDeviceIdle(const Device& device,
                                             DestroyImage&& destroy_image) {
        if (device.WaitIdle() != VK_SUCCESS) {
            ready_ = false;
            budget_.Poison();
            return false;
        }
        std::forward<DestroyImage>(destroy_image)();
        for (auto& allocation : backing_) allocator_.ReleaseSparseMemory(allocation);
        backing_.clear();
        budget_.ResetAfterGpuRetire();
        image_ = VK_NULL_HANDLE;
        ready_ = false;
        return true;
    }
private:
    Allocator& allocator_;
    SparseLayerBudget& budget_;
    SparseImageSpec spec_;
    std::vector<Allocation> backing_;
    VkImage image_ = VK_NULL_HANDLE;
    bool ready_{};
};
// N74: the failed-idle case must quarantine *every* Vulkan handle, including
// VkFence and VkImageView. A local RAII VkImage destructor is otherwise unsafe
// after VkQueueBindSparse accepted work but the GPU never signalled completion.
// Quarantined objects intentionally remain allocated (driver-loss last resort).
// The sticky failure flag prevents repeated probes exhausting host memory.
template <typename Device, typename Owner, typename View, typename Image,
          typename Fence>
[[nodiscard]] bool RetireSparseCacheProbe(
    const Device& device, Owner& owner,
    std::unique_ptr<View>& view, std::unique_ptr<Image>& image,
    std::unique_ptr<Fence>& fence, std::atomic<bool>& gpu_unsafe) {
    const bool retired = owner.RetireAfterDeviceIdle(device, [&] {
        // Must destroy views before their VkImage, then the completion fence,
        // and only THEN may the N69 owner release its VMA page tokens.
        view.reset();
        image.reset();
        fence.reset();
    });
    if (!retired) {
        gpu_unsafe.store(true, std::memory_order_release);
        // Device idle failed. Do not invoke RAII destructors at scope exit:
        // Vulkan may still reference all three handles. Memory is likewise
        // retained by N69 (not returned to VMA).
        (void)view.release();
        (void)image.release();
        (void)fence.release();
    }
    return retired;
}

} // namespace Eden::NS2::Eglantine
