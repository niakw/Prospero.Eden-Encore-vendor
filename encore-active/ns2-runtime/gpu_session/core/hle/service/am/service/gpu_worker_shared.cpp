// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <cstdint>
#include <limits>
#include <new>
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
    // This ticket is mandatory for native channel ownership and Ampere
    // shader authorization. An unchecked false result lets GSV 497 report
    // a valid GPU session, while NVDRV later refuses every channel bind:
    // the guest can then wait forever without a first GPU submission.
    // GSV's OpenGpuSession catches bad_alloc and returns an error without
    // publishing a KClientSession. No process epoch exists on failure.
    native_registered = Nvidia::Devices::NativeGpuProcessRegistry::Instance().Register(process);
    if (!native_registered)
        throw std::bad_alloc{};
}

std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
GpuWorkerSharedState::FindOwnedAddressSpace(std::uint32_t id) const {
    if (!id || !address_spaces.Owns(id)) return {};
    const auto it = opened_gpu_as.find(id);
    if (it == opened_gpu_as.end()) return {};
    const auto owned = it->second.lock();
    return owned && owned->OwnsNativeProcess(native_process) ? owned : nullptr;
}

Nvidia::NvResult GpuWorkerSharedState::BindImportedAddressSpace(
    std::uint32_t id, const std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>& as) {
    if (!id || !as) return Nvidia::NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!address_spaces.Registered(id) || !as->OwnsNativeProcess(native_process))
        return Nvidia::NvResult::InvalidState;

    try {
        const auto [it, inserted] = opened_gpu_as.try_emplace(id, as);
        if (!inserted) {
            if (it->second.lock()) return Nvidia::NvResult::InvalidState;
            it->second = as;
        }
    } catch (const std::bad_alloc&) {
        return Nvidia::NvResult::InsufficientMemory;
    }

    Nvidia::NvResult replay = Nvidia::NvResult::Success;
    for (auto& identity : identity_gpu_mappings) {
        replay = as->MapNative4K(identity.handle, identity.address, 0, identity.size, 0);
        if (replay != Nvidia::NvResult::Success) break;
        if (!identity.AddSpace(id)) {
            (void)as->UnmapNative4K(identity.address, identity.size);
            replay = Nvidia::NvResult::InvalidState;
            break;
        }
    }
    // Publish the local NVDRV identity before releasing the GSV mutex.
    // Destroy must never observe opened_gpu_as with an uncommitted local ID.
    if (replay == Nvidia::NvResult::Success) {
        if (as->CommitNativeVmImport(id)) return replay;
        replay = Nvidia::NvResult::InvalidState;
    }

    for (auto& identity : identity_gpu_mappings) {
        if (!identity.HasSpace(id)) continue;
        (void)as->UnmapNative4K(identity.address, identity.size);
        identity.RemoveSpace(id);
    }
    opened_gpu_as.erase(id);
    return replay;
}

void GpuWorkerSharedState::UnbindImportedAddressSpace(
    std::uint32_t id, const Nvidia::Devices::nvhost_as_gpu* as) {
    if (!id || !as) return;
    const std::scoped_lock lock{mutex};
    const auto it = opened_gpu_as.find(id);
    if (it == opened_gpu_as.end()) return;
    const auto owned = it->second.lock();
    if (!owned || owned.get() != as) return;
    for (auto& identity : identity_gpu_mappings)
        identity.RemoveSpace(id);
    opened_gpu_as.erase(it);
}

GpuWorkerSharedState::~GpuWorkerSharedState() {
    for (auto* event : events) {
        if (event) event_context.CloseEvent(event);
    }
    // NVDRV owns imported descriptors. GSV owns only the opaque VM records:
    // never close an unrelated/recycled NVDRV fd when the GPU session dies.
    // First retire any still-open imported descriptor logically. Its channel
    // may keep the AS/GMMU alive only long enough to drain/destroy queued work.
    for (const auto& [id, weak_as] : opened_gpu_as)
        if (auto as = weak_as.lock())
            as->RetireNativeVm(id);
    for (const auto id : address_spaces.OwnedIds())
        if (id)
            (void)Nvidia::Devices::NativeGpuProcessRegistry::Instance().RemoveVm(
                native_process, *id, this);
    opened_gpu_as.clear();
    if (native_registered)
        Nvidia::Devices::NativeGpuProcessRegistry::Instance().Unregister(native_process);
}
} // namespace Service::AM
