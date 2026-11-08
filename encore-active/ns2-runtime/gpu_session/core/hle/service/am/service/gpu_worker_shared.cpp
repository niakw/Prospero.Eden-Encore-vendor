// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <limits>
#include "core/hle/service/am/service/gpu_worker_shared.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"

namespace Service::AM {
GpuWorkerSharedState::GpuWorkerSharedState(Core::System& system,
                                           std::shared_ptr<Nvidia::Module> module_,
                                           Kernel::KProcess* process)
    : event_context{system, "NS2GpuNotification"}, module{std::move(module_)},
      native_process{process} {
    native_registered = Nvidia::Devices::NativeGpuProcessRegistry::Instance().Register(process);
}

std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
GpuWorkerSharedState::FindOwnedAddressSpace(u32 id) const {
    if (!id || id > static_cast<u32>(std::numeric_limits<std::int32_t>::max()) ||
        !address_spaces.Owns(id) ||
        module->VerifyFD(static_cast<Nvidia::DeviceFD>(id)) != Nvidia::NvResult::Success)
        return {};
    const auto it = opened_gpu_as.find(id);
    if (it == opened_gpu_as.end()) return {};
    const auto owned = it->second.lock();
    // Query ONLY the untyped nvdevice base to check object identity; never
    // static_cast a recycled descriptor before confirming the live type.
    const auto live = module->GetDevice<Nvidia::Devices::nvdevice>(
        static_cast<Nvidia::DeviceFD>(id));
    return owned && live && owned.get() == live.get() &&
                   owned->OwnsNativeProcess(native_process) ? owned : nullptr;
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
        // The guest can close an NVDRV FD before its worker exits. If its
        // integer was reused, closing it here would retire another device.
        if (fd && FindOwnedAddressSpace(*fd))
            module->Close(static_cast<Nvidia::DeviceFD>(*fd));
    }
    if (nv_session)
        module->GetContainer().CloseSession(Nvidia::NvCore::SessionId{*nv_session});
}
} // namespace Service::AM
