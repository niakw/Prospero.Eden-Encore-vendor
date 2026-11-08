// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared across GPU sessions and independently opened worker handles.
// NVDRV address-space FDs must not be closed while a worker still holds
// a kernel client session, even after IGpuSession itself is destroyed.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <optional>

#include "core/hle/service/am/service/gpu_session_protocol.h"
#include "core/hle/service/kernel_helpers.h"

namespace Kernel { class KEvent; class KProcess; }
namespace Service::Nvidia { class Module; namespace Devices { class nvhost_as_gpu; } }
namespace Service::AM {

struct GpuWorkerSharedState {
    explicit GpuWorkerSharedState(Core::System& system,
                                  std::shared_ptr<Nvidia::Module> module,
                                  Kernel::KProcess* process);
    ~GpuWorkerSharedState();
    GpuWorkerSharedState(const GpuWorkerSharedState&) = delete;
    GpuWorkerSharedState& operator=(const GpuWorkerSharedState&) = delete;

    // Lock when reading/modifying events, device slots or address-space FDs.
    std::mutex mutex;
    GpuSessionProtocol::DeviceSlots devices;
    GpuSessionProtocol::AddressSpaceSlots address_spaces;
    // Preserve the original nvhost-as-gpu OBJECT identity across all moved
    // GPU worker KSessions. A numeric NVDRV FD alone can be recycled and
    // GetDevice<nvhost_as_gpu> is a static_cast, not a type check.
    std::map<std::uint32_t, std::weak_ptr<Nvidia::Devices::nvhost_as_gpu>> opened_gpu_as;
    // Caller holds mutex except during final shared-state destruction.
    [[nodiscard]] std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
    FindOwnedAddressSpace(std::uint32_t id) const;
    KernelHelpers::ServiceContext event_context;
    std::array<Kernel::KEvent*, GpuSessionProtocol::MaxNotificationEvents> events{};
    std::shared_ptr<Nvidia::Module> module;
    std::optional<std::size_t> nv_session;
    Kernel::KProcess* native_process{};
    bool native_registered{};
};
} // namespace Service::AM
