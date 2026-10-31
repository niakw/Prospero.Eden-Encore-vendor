// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N55. Fail-closed staging planner for selectively refreshing large ASTC arrays.
// No GPU dispatch or Vulkan sparse residency is enabled by this header.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
namespace Eden::NS2::Eglantine {
enum class LayerPlanStatus { Invalid, FullUpload, NoChanges, Partial };
struct LayerCopy {
    std::uint32_t layer{};
    std::uint32_t batch{};
    std::uint64_t guest_offset{};
    std::uint64_t staging_offset{};
    std::uint64_t bytes{};
};
struct LayerPlan {
    LayerPlanStatus status{LayerPlanStatus::Invalid};
    std::uint64_t generation{};
    std::uint64_t staged_bytes{};
    std::vector<std::uint64_t> batch_bytes;
    std::vector<LayerCopy> copies;
};
class AstcDirtyLayerPlanner {
public:
    AstcDirtyLayerPlanner(std::uint64_t base_address, std::uint64_t image_bytes,
                          std::uint64_t layer_stride, std::uint32_t num_layers,
                          std::uint64_t first_kept_mip_offset, std::uint64_t staging_budget)
        : base_{base_address}, bytes_{image_bytes}, stride_{layer_stride},
          levels_offset_{first_kept_mip_offset}, budget_{staging_budget},
          layers_{num_layers} {
        const auto max = (std::numeric_limits<std::uint64_t>::max)();
        valid_ = layers_ > 1 && layers_ <= 4096 && stride_ != 0 &&
                 bytes_ != 0 && stride_ <= bytes_ / layers_ &&
                 levels_offset_ < stride_ && budget_ >= stride_ - levels_offset_ &&
                 base_ <= max - bytes_;
        // Untrusted layer counts must be rejected before allocating the bitset.
        if (valid_) bits_.assign((static_cast<std::size_t>(layers_) + 63) / 64, 0);
    }
    [[nodiscard]] bool Valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint64_t Generation() const noexcept { return generation_; }
    // Call ONLY after the initial full upload has completed successfully.
    [[nodiscard]] bool ResetAfterFullUpload() noexcept {
        if (!valid_) return false;
        std::fill(bits_.begin(), bits_.end(), 0);
        full_dirty_ = false;
        ++generation_;
        return true;
    }
    // On remap, unmap, image replacement, unsafe alias, or lost GPU sync.
    void Invalidate() noexcept {
        full_dirty_ = true;
        std::fill(bits_.begin(), bits_.end(), 0);
        ++generation_;
    }
    // Untrusted CPU write: clamp instead of wrapping write address or size.
    [[nodiscard]] bool MarkWrite(std::uint64_t address, std::uint64_t size) noexcept {
        if (!valid_) return false;
        if (size == 0 || full_dirty_) return true;
        const auto max = (std::numeric_limits<std::uint64_t>::max)();
        const auto write_end = size > max - address ? max : address + size;
        const auto image_end = base_ + bytes_;
        if (address >= image_end || write_end <= base_) return true;
        const auto begin = std::max(address, base_) - base_;
        const auto end = std::min(write_end, image_end) - base_;
        if (end <= begin) return true;
        const auto first = begin / stride_;
        const auto last = (end - 1) / stride_;
        if (first >= layers_) {
            // Tail/padding outside declared layers is not safely trackable.
            Invalidate();
            return false;
        }
        for (std::uint64_t layer = first; layer <= std::min<std::uint64_t>(last, layers_ - 1); ++layer)
            bits_[static_cast<std::size_t>(layer / 64)] |= std::uint64_t{1} << (layer % 64);
        if (last >= layers_) {
            Invalidate();
            return false;
        }
        ++generation_;
        return true;
    }
    [[nodiscard]] LayerPlan Snapshot() const {
        LayerPlan plan;
        plan.generation = generation_;
        if (!valid_) return plan;
        if (full_dirty_) {
            plan.status = LayerPlanStatus::FullUpload;
            return plan;
        }
        const auto per_layer = stride_ - levels_offset_;
        for (std::uint32_t layer = 0; layer < layers_; ++layer) {
            if ((bits_[layer / 64] & (std::uint64_t{1} << (layer % 64))) == 0)
                continue;
            if (plan.batch_bytes.empty() || plan.batch_bytes.back() > budget_ - per_layer)
                plan.batch_bytes.push_back(0);
            const std::uint32_t batch = static_cast<std::uint32_t>(plan.batch_bytes.size() - 1);
            const auto staged_offset = plan.batch_bytes.back();
            // stride <= image_bytes/layers checked in constructor.
            plan.copies.push_back(LayerCopy{layer, batch,
                static_cast<std::uint64_t>(layer) * stride_ + levels_offset_,
                staged_offset, per_layer});
            plan.batch_bytes.back() += per_layer;
            plan.staged_bytes += per_layer;  // bounded by image_bytes
        }
        if (plan.copies.empty()) {
            plan.status = LayerPlanStatus::NoChanges;
        } else if (plan.copies.size() == layers_) {
            // All layers dirty: let the existing full-image path decide.
            plan.status = LayerPlanStatus::FullUpload;
            plan.copies.clear();
            plan.batch_bytes.clear();
            plan.staged_bytes = 0;
        } else {
            plan.status = LayerPlanStatus::Partial;
        }
        return plan;
    }
    // Commit only after *all* GPU batches have been submitted successfully.
    // New writes invalidate a stale snapshot and remain pending.
    [[nodiscard]] bool Commit(const LayerPlan& plan) noexcept {
        if (!valid_ || full_dirty_ || plan.status != LayerPlanStatus::Partial ||
            plan.generation != generation_)
            return false;
        std::fill(bits_.begin(), bits_.end(), 0);
        ++generation_;
        return true;
    }
private:
    std::uint64_t base_{};
    std::uint64_t bytes_{};
    std::uint64_t stride_{};
    std::uint64_t levels_offset_{};
    std::uint64_t budget_{};
    std::uint32_t layers_{};
    std::vector<std::uint64_t> bits_;
    std::uint64_t generation_{};
    bool full_dirty_{true};
    bool valid_{false};
};
} // namespace Eden::NS2::Eglantine
