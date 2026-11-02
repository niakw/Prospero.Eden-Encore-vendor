// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Shared Switch 1 Maxwell / Switch 2 Ampere Compute storage descriptor
// walker. Shader reflection owns the descriptor order; QMD/CBUF layouts
// are guest-specific and must never be silently reinterpreted here.
#include <cstdint>
#include <limits>

namespace Vulkan::ComputeStorageLayout {
constexpr std::uint32_t kDescriptorBytes = 16;

// Expand SPIR-V storage-buffer arrays in layout order, without allocations.
// The callback receives the flattened binding, source CBUF, byte offset and
// access direction. Reject invalid/overflowing reflection *before* binding.
// All callers must preflight their own memory mappings and device limits.
template <typename Descriptors, typename Callback>
[[nodiscard]] bool ForEach(const Descriptors& descriptors,
                           std::uint32_t max_bindings,
                           Callback&& callback) {
    std::uint32_t flattened{};
    for (const auto& descriptor : descriptors) {
        const std::uint32_t count = descriptor.count;
        if (count == 0 || count > max_bindings - flattened ||
            (descriptor.cbuf_offset & 3U) != 0 ||
            descriptor.cbuf_offset >
                std::numeric_limits<std::uint32_t>::max() -
                    kDescriptorBytes * (count - 1U))
            return false;
        for (std::uint32_t element = 0; element < count; ++element) {
            const std::uint32_t offset =
                descriptor.cbuf_offset + element * kDescriptorBytes;
            if (!callback(flattened++, descriptor.cbuf_index, offset,
                          descriptor.is_written))
                return false;
        }
    }
    return true;
}
} // namespace Vulkan::ComputeStorageLayout
