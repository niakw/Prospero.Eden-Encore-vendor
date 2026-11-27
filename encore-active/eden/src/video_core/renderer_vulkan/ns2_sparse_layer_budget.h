// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N63: fail-closed allocation budget and mip-tail plan for sparse 2D image arrays.
// Host-side policy only; the N64 Vulkan binder will consume this type.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace Eden::NS2::Eglantine {
struct SparseImageSpec {
    std::uint32_t width{},height{},levels{},layers{};
    std::uint32_t tile_width{},tile_height{};
    std::uint32_t tail_first_lod{};
    std::uint64_t tile_alignment{},tail_bytes{};
    std::uint64_t max_bound_bytes{};
    bool single_mip_tail{};
};
enum class SparsePlanStatus {
    Invalid, Empty, AlreadyBound, Ready, BudgetExceeded, Poisoned
};
struct SparseLayerBindPlan {
    SparsePlanStatus status{SparsePlanStatus::Invalid};
    std::uint64_t generation{};
    std::uint64_t bytes_new{};
    std::uint64_t layer_bytes{};
    std::uint64_t shared_tail_bytes{};
    bool bind_shared_tail{};
    std::vector<std::uint32_t> new_layers;
};
class SparseLayerBudget final {
    static constexpr auto Max = (std::numeric_limits<std::uint64_t>::max)();
    static bool Add(std::uint64_t a,std::uint64_t b,std::uint64_t& out) noexcept {
        if (a > Max - b) return false;
        out = a + b;
        return true;
    }
    static bool Mul(std::uint64_t a,std::uint64_t b,std::uint64_t& out) noexcept {
        if (b && a > Max / b) return false;
        out = a * b;
        return true;
    }
    static bool Aligned(std::uint64_t a,std::uint64_t alignment,std::uint64_t& out) noexcept {
        if(alignment==0 || (alignment & (alignment-1))!=0 ||
           a>Max-(alignment-1)) return false;
        out=(a+alignment-1)&~(alignment-1);return true;
    }
public:
    static constexpr std::uint64_t CapForDeviceMemory(std::uint64_t bytes) noexcept {
        return bytes<(std::uint64_t{7}<<30)
            ? (std::uint64_t{384}<<20) : (std::uint64_t{768}<<20);
    }
    explicit SparseLayerBudget(const SparseImageSpec& spec)
        : spec_{spec} {
        if (spec.layers<=1 || spec.layers>4096 || spec.levels==0 ||
            spec.levels>16 || !spec.width || !spec.height ||
            !spec.tile_width || !spec.tile_height ||
            spec.tail_first_lod>spec.levels ||
            !spec.max_bound_bytes || !spec.tile_alignment ||
            (spec.tile_alignment&(spec.tile_alignment-1))!=0 ||
            spec.max_bound_bytes>CapForDeviceMemory(std::uint64_t{7}<<30))
            return;
        const bool has_tail=spec.tail_first_lod<spec.levels;
        if (!has_tail && spec.tail_bytes!=0) return;
        if (has_tail && spec.tail_bytes==0) return;
        std::uint64_t bytes=0;
        for(std::uint32_t level=0;level<spec.tail_first_lod;++level){
            const auto w=std::max<std::uint32_t>(1,spec.width>>level);
            const auto h=std::max<std::uint32_t>(1,spec.height>>level);
            const auto nx=(static_cast<std::uint64_t>(w)+spec.tile_width-1)/spec.tile_width;
            const auto ny=(static_cast<std::uint64_t>(h)+spec.tile_height-1)/spec.tile_height;
            std::uint64_t tiles{},level_bytes{},total{};
            if(!Mul(nx,ny,tiles)||!Mul(tiles,spec.tile_alignment,level_bytes)||
               !Add(bytes,level_bytes,total))return;
            bytes=total;
        }
        if(has_tail){
            std::uint64_t aligned{};
            if(!Aligned(spec.tail_bytes,spec.tile_alignment,aligned))return;
            if(spec.single_mip_tail)shared_tail_bytes_=aligned;
            else if(!Add(bytes,aligned,bytes))return;
        }
        layer_bytes_=bytes;
        bound_.assign(spec.layers,false);
        valid_=true;
    }
    [[nodiscard]] bool Valid() const noexcept{return valid_;}
    [[nodiscard]] std::uint64_t BoundBytes() const noexcept{return bound_bytes_;}
    [[nodiscard]] bool Poisoned() const noexcept{return poisoned_;}
    [[nodiscard]] bool IsBound(std::uint32_t layer) const noexcept{
        return layer<bound_.size()&&bound_[layer];
    }
    [[nodiscard]] SparseLayerBindPlan Plan(std::span<const std::uint32_t> request) const {
        SparseLayerBindPlan out{};
        if(!valid_)return out;
        if(poisoned_){out.status=SparsePlanStatus::Poisoned;return out;}
        out.generation=generation_;
        out.layer_bytes=layer_bytes_;
        out.shared_tail_bytes=shared_tail_bytes_;
        if(request.empty()){out.status=SparsePlanStatus::Empty;return out;}
        std::vector<std::uint32_t> unique(request.begin(),request.end());
        std::sort(unique.begin(),unique.end());
        if(std::adjacent_find(unique.begin(),unique.end())!=unique.end() ||
           unique.back()>=spec_.layers)return out;
        for(std::uint32_t layer:unique)
            if(!bound_[layer])out.new_layers.push_back(layer);
        if(out.new_layers.empty()){
            out.status=SparsePlanStatus::AlreadyBound;return out;
        }
        std::uint64_t new_bytes{};
        if(!Mul(layer_bytes_,out.new_layers.size(),new_bytes))return out;
        out.bind_shared_tail=shared_tail_bytes_>0&&!shared_tail_bound_;
        if(out.bind_shared_tail&&
           !Add(new_bytes,shared_tail_bytes_,new_bytes))return out;
        out.bytes_new=new_bytes;
        if(new_bytes>spec_.max_bound_bytes ||
           bound_bytes_>spec_.max_bound_bytes-new_bytes){
            out.status=SparsePlanStatus::BudgetExceeded;return out;
        }
        out.status=SparsePlanStatus::Ready;
        return out;
    }
    // Commit only after vkQueueBindSparse and the associated fence succeed.
    [[nodiscard]] bool Commit(const SparseLayerBindPlan& plan) noexcept {
        if(!valid_||poisoned_||plan.status!=SparsePlanStatus::Ready||
           plan.generation!=generation_)return false;
        const auto check=Plan(plan.new_layers);
        if(check.status!=SparsePlanStatus::Ready ||
           check.bytes_new!=plan.bytes_new ||
           check.new_layers!=plan.new_layers ||
           check.bind_shared_tail!=plan.bind_shared_tail)return false;
        for(const auto layer:plan.new_layers)bound_[layer]=true;
        bound_bytes_+=plan.bytes_new;
        if(plan.bind_shared_tail)shared_tail_bound_=true;
        ++generation_;
        return true;
    }
    // Sparse bind failure may leave partially submitted GPU commands.
    // Do not re-use that image: caller must retire fence and recreate it.
    void Poison() noexcept{poisoned_=true;++generation_;}
    // Only call after the Vulkan image and all submitted GPU work retire.
    void ResetAfterGpuRetire() noexcept {
        std::fill(bound_.begin(),bound_.end(),false);
        bound_bytes_=0;shared_tail_bound_=false;poisoned_=false;++generation_;
    }
private:
    SparseImageSpec spec_{};
    std::vector<bool> bound_;
    std::uint64_t layer_bytes_{},shared_tail_bytes_{},bound_bytes_{},generation_{};
    bool valid_{},poisoned_{},shared_tail_bound_{};
};

// N68: turn the N63 per-layer budget into concrete, bounded tile coordinates
// and opaque mip-tail resource offsets. The native cache owner must allocate
// every returned region via N62 and submit it via N66/N67 before budget.Commit.
// This does not create Vulkan images or take ownership of GPU memory.
struct SparseTileRegion {
    std::uint32_t layer{}, level{}, x{}, y{}, width{}, height{};
    std::uint64_t bytes{};
};
struct SparseMipTailRegion {
    std::uint32_t layer{};
    bool shared{};
    std::uint64_t resource_offset{}, bytes{};
};
struct SparsePhysicalBindPlan {
    std::vector<SparseTileRegion> tiles;
    std::vector<SparseMipTailRegion> tails;
    std::uint64_t backing_bytes{};
    bool valid{};
};
[[nodiscard]] inline SparsePhysicalBindPlan MakeSparsePhysicalBindPlan(
    const SparseImageSpec& spec, const SparseLayerBindPlan& logical,
    std::uint64_t mip_tail_offset, std::uint64_t mip_tail_stride,
    std::uint64_t virtual_image_bytes) {
    SparsePhysicalBindPlan out{};
    const SparseLayerBudget validated(spec);
    if (!validated.Valid() || logical.status != SparsePlanStatus::Ready ||
        logical.new_layers.empty() || !virtual_image_bytes ||
        logical.new_layers.size() > spec.layers ||
        !std::is_sorted(logical.new_layers.begin(), logical.new_layers.end()) ||
        std::adjacent_find(logical.new_layers.begin(), logical.new_layers.end()) !=
            logical.new_layers.end() ||
        logical.new_layers.back() >= spec.layers ||
        logical.bytes_new > spec.max_bound_bytes) return out;
    const auto unit = spec.tile_alignment;
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::size_t max_tiles = 65536, max_tails = 4096;
    const bool has_tail = spec.tail_first_lod < spec.levels;
    if ((!has_tail && logical.bind_shared_tail) ||
        (has_tail && !spec.single_mip_tail && logical.bind_shared_tail)) return out;
    const std::uint64_t tail_span =
        has_tail ? (spec.tail_bytes > max - (unit - 1) ? 0 :
                    ((spec.tail_bytes + unit - 1) & ~(unit - 1))) : 0;
    if (has_tail && (!tail_span || mip_tail_offset % unit ||
        (!spec.single_mip_tail &&
         (!mip_tail_stride || mip_tail_stride % unit ||
          mip_tail_stride < tail_span)))) return out;
    if (logical.layer_bytes == 0 && !logical.bind_shared_tail) return out;
    // The computed geometry is not an alternative budget: require N63 agreement.
    const auto first = validated.Plan(std::span<const std::uint32_t>{
        logical.new_layers.data(), 1});
    if (first.status != SparsePlanStatus::Ready ||
        first.layer_bytes != logical.layer_bytes ||
        first.shared_tail_bytes != logical.shared_tail_bytes) return out;
    auto add_bytes = [&](std::uint64_t n) -> bool {
        if (out.backing_bytes > max - n) return false;
        out.backing_bytes += n;
        return out.backing_bytes <= spec.max_bound_bytes;
    };
    for (const auto layer : logical.new_layers) {
        for (std::uint32_t level = 0; level < spec.tail_first_lod; ++level) {
            const auto w = (std::max)(std::uint32_t{1}, spec.width >> level);
            const auto h = (std::max)(std::uint32_t{1}, spec.height >> level);
            const auto nx = (static_cast<std::uint64_t>(w) + spec.tile_width - 1) /
                            spec.tile_width;
            const auto ny = (static_cast<std::uint64_t>(h) + spec.tile_height - 1) /
                            spec.tile_height;
            if (nx > max_tiles || ny > max_tiles ||
                nx > (max_tiles - out.tiles.size()) / ny) return {};
            for (std::uint64_t y = 0; y < ny; ++y) {
                for (std::uint64_t x = 0; x < nx; ++x) {
                    const auto tx = x * spec.tile_width, ty = y * spec.tile_height;
                    if (tx > static_cast<std::uint64_t>(
                            (std::numeric_limits<std::int32_t>::max)()) ||
                        ty > static_cast<std::uint64_t>(
                            (std::numeric_limits<std::int32_t>::max)()) ||
                        !add_bytes(unit)) return {};
                    out.tiles.push_back(SparseTileRegion{
                        layer, level, static_cast<std::uint32_t>(tx),
                        static_cast<std::uint32_t>(ty),
                        static_cast<std::uint32_t>(
                            (std::min)(static_cast<std::uint64_t>(w)-tx,
                                       static_cast<std::uint64_t>(spec.tile_width))),
                        static_cast<std::uint32_t>(
                            (std::min)(static_cast<std::uint64_t>(h)-ty,
                                       static_cast<std::uint64_t>(spec.tile_height))), unit});
                }
            }
        }
        if (has_tail && !spec.single_mip_tail) {
            if (out.tails.size() >= max_tails ||
                layer > (max - mip_tail_offset) / mip_tail_stride) return {};
            const auto offset = mip_tail_offset +
                                static_cast<std::uint64_t>(layer) * mip_tail_stride;
            if (offset > virtual_image_bytes ||
                tail_span > virtual_image_bytes - offset ||
                !add_bytes(tail_span)) return {};
            out.tails.push_back(SparseMipTailRegion{layer, false, offset, tail_span});
        }
    }
    if (has_tail && spec.single_mip_tail && logical.bind_shared_tail) {
        if (out.tails.size() >= max_tails ||
            mip_tail_offset > virtual_image_bytes ||
            tail_span > virtual_image_bytes - mip_tail_offset ||
            !add_bytes(tail_span)) return {};
        out.tails.push_back(SparseMipTailRegion{0, true, mip_tail_offset, tail_span});
    }
    if ((out.tiles.empty() && out.tails.empty()) ||
        out.backing_bytes != logical.bytes_new) return {};
    out.valid = true;
    return out;
}
} // namespace Eden::NS2::Eglantine
