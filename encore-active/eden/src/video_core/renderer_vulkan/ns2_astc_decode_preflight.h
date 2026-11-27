// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N57: fail-closed preflight before using partially staged Vulkan ASTC layers.
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace Eden::NS2::Eglantine {
struct AstcMipSlice {
    std::uint64_t buffer_offset{};
    std::uint32_t level{};
};
[[nodiscard]] inline bool PreflightAstcLayerDecode(
    std::uint32_t image_layers, std::uint32_t host_levels,
    std::uint64_t layer_stride, std::uint64_t staged_bytes,
    std::span<const std::uint32_t> layers,
    std::span<const AstcMipSlice> mips) {
    // The pinned PS5 owner does not expose host_level_offset. Reject
    // skipped-mip staging until a full trimmed-mip owner exists.
    if (mips.empty() || mips.front().level != 0 || mips.front().buffer_offset != 0 ||
        image_layers <= 1 || image_layers > 4096 || host_levels == 0 ||
        host_levels > 16 || layers.empty() ||
        layer_stride == 0 || layer_stride > (std::numeric_limits<std::uint32_t>::max)() ||
        layers.size() > image_layers || layer_stride > staged_bytes / layers.size())
        return false;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (layers[i] >= image_layers) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (layers[i] == layers[j]) return false;
    }
    for (std::size_t i = 0; i < mips.size(); ++i) {
        if (mips[i].level >= host_levels ||
            mips[i].buffer_offset >= layer_stride)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (mips[i].level == mips[j].level) return false;
    }
    return true;
}
} // namespace Eden::NS2::Eglantine
