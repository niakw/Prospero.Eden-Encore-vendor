// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared across GPU sessions and independently opened worker handles.
// NVDRV address-space FDs must not be closed while a worker still holds
// a kernel client session, even after IGpuSession itself is destroyed.
#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>

#include "core/hle/service/am/service/gpu_session_protocol.h"
#include "core/hle/service/kernel_helpers.h"

namespace Kernel { class KEvent; class KProcess; }
namespace Service::Nvidia { class Module; }
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
    KernelHelpers::ServiceContext event_context;
    std::array<Kernel::KEvent*, GpuSessionProtocol::MaxNotificationEvents> events{};
    std::shared_ptr<Nvidia::Module> module;
    std::optional<std::size_t> nv_session;
    Kernel::KProcess* native_process{};
    bool native_registered{};
};
} // namespace Service::AM
