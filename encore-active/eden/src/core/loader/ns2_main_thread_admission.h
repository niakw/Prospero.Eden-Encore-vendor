// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N3y: validate the kernel's actual main-thread capability masks after
// KProcess::LoadFromMetadata. Never synthesize a missing permission.
#include <cstdint>
namespace Eden::NS2::ExeFS {
[[nodiscard]] constexpr bool MainThreadPermitted(
    std::uint64_t allowed_cores, std::uint64_t allowed_priorities,
    std::uint32_t selected_core, std::uint32_t selected_priority) noexcept {
    return selected_core < 64 && selected_priority < 64 &&
           ((allowed_cores >> selected_core) & 1U) != 0 &&
           ((allowed_priorities >> selected_priority) & 1U) != 0;
}
} // namespace Eden::NS2::ExeFS
