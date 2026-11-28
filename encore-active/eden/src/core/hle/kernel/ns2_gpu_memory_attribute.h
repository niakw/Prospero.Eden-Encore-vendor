// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// NS2 SVC 0x80 range/attribute admission. After validation the kernel
// stores the bit in its real memory block manager; GPU mapping and command
// completion still require the separate GPU worker.
#include <cstdint>
namespace Eden::NS2::GpuAccess {
enum class Status { Success, InvalidAddress, InvalidSize, InvalidAttribute };
constexpr std::uint64_t GuestPageSize = 0x1000;
constexpr std::uint32_t GpuAccessibleMask = 0x20;
[[nodiscard]] constexpr Status Validate(std::uint64_t address, std::uint64_t size,
                                         std::uint32_t mask, std::uint32_t attr) noexcept {
    if ((address & (GuestPageSize - 1)) != 0) return Status::InvalidAddress;
    if (!size || (size & (GuestPageSize - 1)) != 0 || size > UINT64_MAX - address)
        return Status::InvalidSize;
    if ((mask | attr) & ~GpuAccessibleMask || (attr & ~mask))
        return Status::InvalidAttribute;
    return Status::Success;
}
} // namespace Eden::NS2::GpuAccess
