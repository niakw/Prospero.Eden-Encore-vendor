// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N45: validate the exact byte count before KProcess::LoadFromMetadata
// converts it to signed 32-bit 4-KiB page counts. Experimental NS2 only.
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Eden::NS2::ExeFS {
inline constexpr std::size_t KProcessCodePageSize = 0x1000;
enum class KProcessCodeStatus : std::uint8_t {
    Ready,
    EmptyModules,
    SizeOverflow,
    UnalignedPages,
    PageCountOverflow,
};
struct KProcessCodeAdmission {
    KProcessCodeStatus status;
    std::size_t total_bytes;
    [[nodiscard]] constexpr bool Ready() const noexcept {
        return status == KProcessCodeStatus::Ready;
    }
};
[[nodiscard]] constexpr KProcessCodeAdmission AdmitKProcessCode(
    std::size_t module_bytes, std::size_t patch_bytes) noexcept {
    if (module_bytes == 0) {
        return {KProcessCodeStatus::EmptyModules, 0};
    }
    if (patch_bytes > std::numeric_limits<std::size_t>::max() - module_bytes) {
        return {KProcessCodeStatus::SizeOverflow, 0};
    }
    const std::size_t total = module_bytes + patch_bytes;
    if (total % KProcessCodePageSize != 0) {
        return {KProcessCodeStatus::UnalignedPages, total};
    }
    if (total / KProcessCodePageSize >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return {KProcessCodeStatus::PageCountOverflow, total};
    }
    return {KProcessCodeStatus::Ready, total};
}
} // namespace Eden::NS2::ExeFS
