// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Eglantine ca524c9: do not create a 3D Vulkan image view from a 2D
// image, or vice versa. Generic so host tests use no Vulkan runtime.
namespace Eden::NS2::Vulkan {
template <class ViewType>
[[nodiscard]] constexpr bool CrossDimensionView(
    ViewType actual, ViewType shader, ViewType three_d) noexcept {
    return (actual == three_d) != (shader == three_d);
}
} // namespace Eden::NS2::Vulkan
