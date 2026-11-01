// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include "core/hle/service/am/service/gpu_worker_shared.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"

namespace Service::AM {
GpuWorkerSharedState::GpuWorkerSharedState(Core::System& system,
                                           std::shared_ptr<Nvidia::Module> module_,
                                           Kernel::KProcess* process)
    : event_context{system, "NS2GpuNotification"}, module{std::move(module_)},
      native_process{process} {
    native_registered = Nvidia::Devices::NativeGpuProcessRegistry::Instance().Register(process);
}

GpuWorkerSharedState::~GpuWorkerSharedState() {
    if (native_registered)
        Nvidia::Devices::NativeGpuProcessRegistry::Instance().Unregister(native_process);
    // Worker clients can be moved to a different guest handle table. Kernel
    // events, live GPU address-space devices and NVDRV session therefore have
    // the lifetime of the last shared session/worker owner.
    for (auto* event : events) {
        if (event) event_context.CloseEvent(event);
    }
    for (const auto fd : address_spaces.OwnedIds()) {
        if (fd) module->Close(static_cast<Nvidia::DeviceFD>(*fd));
    }
    if (nv_session)
        module->GetContainer().CloseSession(Nvidia::NvCore::SessionId{*nv_session});
}
} // namespace Service::AM
