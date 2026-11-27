// SPDX-License-Identifier: MIT
// Research-only R09 GPU readback raster admission (no PS5 runtime toggle).
#pragma once
#include <cstdint>
namespace Vulkan::R09ReadbackPolicy {
[[nodiscard]] constexpr bool Supported(std::uint32_t width,
                                        std::uint32_t height) noexcept {
    return (width==1920u && height==1080u) ||
           (width==1280u && height==720u);
}
[[nodiscard]] constexpr std::uint64_t Rgba32fBytes(
    std::uint32_t width, std::uint32_t height) noexcept {
    return Supported(width,height) ?
           std::uint64_t(width)*height*4u*sizeof(float) : 0u;
}
static_assert(Rgba32fBytes(1920u,1080u)==33'177'600u);
static_assert(Rgba32fBytes(1280u,720u)==14'745'600u);
static_assert(Rgba32fBytes(3840u,2160u)==0u);
} // namespace Vulkan::R09ReadbackPolicy
