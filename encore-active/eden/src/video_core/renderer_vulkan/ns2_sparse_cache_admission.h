// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N73: admission of *fully resident* 2D array textures into the experimental
// native Vulkan sparse cache path. No partial sampling and no game exceptions.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>
#include "video_core/renderer_vulkan/ns2_sparse_layer_budget.h"
#include "video_core/renderer_vulkan/vk_shared_gpu_budget.h"

namespace Eden::NS2::Cache {
using Eden::NS2::Eglantine::SparseImageSpec;
using Eden::NS2::Eglantine::SparseLayerBudget;
using Eden::NS2::Eglantine::SparsePlanStatus;
using Eden::NS2::Eglantine::MakeSparsePhysicalBindPlan;

enum class SparseCacheReject {
    None, Disabled, UnsupportedImage, UnsupportedMemory, UnsupportedAspect,
    UnsupportedTiling, InvalidBudget, IncompleteResidency
};

struct SparseCacheAdmission {
    SparseCacheReject rejection = SparseCacheReject::UnsupportedImage;
    SparseImageSpec spec{};
    std::vector<std::uint32_t> complete_layers{};
    std::uint64_t resident_bytes{};
    [[nodiscard]] bool Accepted() const noexcept {
        return rejection == SparseCacheReject::None;
    }
};

[[nodiscard]] inline SparseCacheAdmission AdmitSparseCacheImage(
    const VkImageCreateInfo& ci, const VkMemoryRequirements& memory,
    std::span<const VkSparseImageMemoryRequirements> requirements,
    std::uint64_t device_local_bytes, bool sparse_queue_supported,
    std::uint64_t shared_pool_used = 0,
    bool reliable_shared_pool_usage = false) {
    SparseCacheAdmission result{};
    if (!sparse_queue_supported) {
        result.rejection = SparseCacheReject::Disabled;
        return result;
    }
    if (ci.imageType != VK_IMAGE_TYPE_2D || ci.extent.depth != 1 ||
        ci.extent.width == 0 || ci.extent.height == 0 ||
        ci.arrayLayers < 2 || ci.arrayLayers > 4096 ||
        ci.mipLevels == 0 || ci.mipLevels > 16 ||
        ci.samples != VK_SAMPLE_COUNT_1_BIT ||
        ci.tiling != VK_IMAGE_TILING_OPTIMAL ||
        (ci.flags & (VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT |
                     VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                     VK_IMAGE_CREATE_EXTENDED_USAGE_BIT |
                     VK_IMAGE_CREATE_ALIAS_BIT)) != 0 ||
        (ci.usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 ||
        (ci.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0 ||
        (ci.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_STORAGE_BIT)) != 0) {
        result.rejection = SparseCacheReject::UnsupportedImage;
        return result;
    }
    if (!memory.size || !memory.memoryTypeBits || !memory.alignment ||
        (memory.alignment & (memory.alignment - 1)) != 0) {
        result.rejection = SparseCacheReject::UnsupportedMemory;
        return result;
    }
    if (requirements.size() != 1 ||
        requirements.front().formatProperties.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) {
        result.rejection = SparseCacheReject::UnsupportedAspect;
        return result;
    }
    const auto& sparse = requirements.front();
    const auto flags = sparse.formatProperties.flags;
    if ((flags & (VK_SPARSE_IMAGE_FORMAT_NONSTANDARD_BLOCK_SIZE_BIT |
                  VK_SPARSE_IMAGE_FORMAT_ALIGNED_MIP_SIZE_BIT)) != 0 ||
        sparse.formatProperties.imageGranularity.width == 0 ||
        sparse.formatProperties.imageGranularity.height == 0 ||
        sparse.formatProperties.imageGranularity.depth != 1 ||
        sparse.imageMipTailFirstLod > ci.mipLevels) {
        result.rejection = SparseCacheReject::UnsupportedTiling;
        return result;
    }
    const SparseImageSpec spec{
        .width = ci.extent.width,
        .height = ci.extent.height,
        .levels = ci.mipLevels,
        .layers = ci.arrayLayers,
        .tile_width = sparse.formatProperties.imageGranularity.width,
        .tile_height = sparse.formatProperties.imageGranularity.height,
        .tail_first_lod = sparse.imageMipTailFirstLod,
        .tile_alignment = memory.alignment,
        .tail_bytes = sparse.imageMipTailFirstLod < ci.mipLevels ?
                      sparse.imageMipTailSize : 0,
        .max_bound_bytes = Eden::Vulkan::MemoryPolicy::SparseBackingBudget(
            device_local_bytes, shared_pool_used, reliable_shared_pool_usage,
            SparseLayerBudget::CapForDeviceMemory(device_local_bytes)).max_new_bytes,
        .single_mip_tail = bool(flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT),
    };
    const SparseLayerBudget budget(spec);
    if (!budget.Valid()) {
        result.rejection = SparseCacheReject::InvalidBudget;
        return result;
    }
    // A sampled cache image must NEVER expose unbound layers/mips. This
    // preflight must admit an all-layers plan, not the N63 partial plan.
    std::vector<std::uint32_t> all(spec.layers);
    std::iota(all.begin(), all.end(), 0U);
    const auto plan = budget.Plan(all);
    if (plan.status != SparsePlanStatus::Ready ||
        plan.new_layers.size() != all.size()) {
        result.rejection = SparseCacheReject::IncompleteResidency;
        return result;
    }
    const auto physical = MakeSparsePhysicalBindPlan(
        spec, plan, sparse.imageMipTailOffset,
        sparse.imageMipTailStride, memory.size);
    if (!physical.valid || physical.backing_bytes != plan.bytes_new) {
        result.rejection = SparseCacheReject::IncompleteResidency;
        return result;
    }
    result.spec = spec;
    result.complete_layers = std::move(all);
    result.resident_bytes = plan.bytes_new;
    result.rejection = SparseCacheReject::None;
    return result;
}
} // namespace Eden::NS2::Cache
