// SPDX-License-Identifier: MIT
// Checked FinalHead tiled FP8 tokens <-> padded raster ViT tokens.
// This is a buffer-layout contract, not a claim of DLSS model execution.
#pragma once
#include <cstdint>

namespace Eden::Graphics::NeuralRDNA2 {
struct VitBoundaryRepack {
    std::uint32_t width{}, height{}, channels{}, vit_tokens{}, direction{};
    std::uint32_t source_base_bytes{}, target_base_bytes{};
};
struct VitBoundaryStorage {
    std::uint64_t source_bytes{}, target_bytes{};
};

[[nodiscard]] constexpr std::uint32_t VitTiledTokenCount(
    const VitBoundaryRepack& p) noexcept {
    return ((p.width + 3u) / 4u) * ((p.height + 3u) / 4u) * 16u;
}
[[nodiscard]] constexpr std::uint32_t VitBoundaryGrid(
    const VitBoundaryRepack& p) noexcept {
    // The Vulkan shader works with 256 u32 words per workgroup. Each
    // FP8 token has exactly 1024 bytes (= 256 words).
    return p.direction == 0u ? p.vit_tokens : VitTiledTokenCount(p);
}
[[nodiscard]] constexpr bool CanVitBoundaryRepack(
    const VitBoundaryRepack& p, const VitBoundaryStorage& b) noexcept {
    if (!p.width || !p.height || p.width > 4096u || p.height > 4096u ||
        p.channels != 1024u || p.vit_tokens == 0u || p.vit_tokens > 4096u ||
        p.direction > 1u || (p.source_base_bytes & 3u) ||
        (p.target_base_bytes & 3u))
        return false;
    const auto pixels = static_cast<std::uint64_t>(p.width) * p.height;
    if (pixels > p.vit_tokens)
        return false;
    const auto tiles = static_cast<std::uint64_t>((p.width + 3u) / 4u) *
                       ((p.height + 3u) / 4u) * 16u;
    if (!tiles || tiles > 4096u)
        return false;
    const auto source_tokens = p.direction == 0u ? tiles : p.vit_tokens;
    const auto target_tokens = p.direction == 0u ? p.vit_tokens : tiles;
    const auto source_end = static_cast<std::uint64_t>(p.source_base_bytes) +
                            source_tokens * p.channels;
    const auto target_end = static_cast<std::uint64_t>(p.target_base_bytes) +
                            target_tokens * p.channels;
    return source_end <= b.source_bytes && target_end <= b.target_bytes;
}
} // namespace Eden::Graphics::NeuralRDNA2
