// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <iterator>
#include <utility>
#include <cstdint>
#include <limits>
#include <new>
#include "core/hle/service/am/service/gpu_worker_shared.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"
#include "core/hle/service/nvdrv/devices/native_gpu_va_layout.h"

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

bool GpuWorkerSharedState::CanStageBeforeImport(std::uint32_t id) const {
    return id && address_spaces.Registered(id) && !opened_gpu_as.contains(id) &&
           std::find(imported_vm_ids.begin(), imported_vm_ids.end(), id) ==
               imported_vm_ids.end();
}

namespace {
bool Overlaps(std::uint64_t a, std::uint64_t asize,
              std::uint64_t b, std::uint64_t bsize) {
    return a <= b ? b - a < asize : a - b < bsize;
}
bool ExactIdentity(const IdentityGpuMapping& identity,
                   const GpuWorkerProtocol::Map4K& request) {
    return identity.address == request.address && identity.size == request.size &&
           identity.handle == request.memory && request.memory_offset == 0;
}
} // namespace

bool GpuWorkerSharedState::OverlapsPendingIdentity(std::uint64_t address,
                                                   std::uint64_t size) const {
    for (const auto& [key, request] : pending_211) {
        (void)key;
        if (Overlaps(address, size, request.address, request.size)) return true;
    }
    return false;
}

Nvidia::NvResult GpuWorkerSharedState::StageBeforeImport(
    const GpuWorkerProtocol::Map4K& request) {
    using Nvidia::Devices::NativeNs2GpuVaStart;
    using Nvidia::Devices::NativeNs2GpuVaSmallSplit;
    if (!CanStageBeforeImport(request.address_space))
        return Nvidia::NvResult::InvalidState;
    if (!request.size || request.address < NativeNs2GpuVaStart ||
        request.address >= NativeNs2GpuVaSmallSplit ||
        request.size > NativeNs2GpuVaSmallSplit - request.address)
        return Nvidia::NvResult::BadParameter;
    if (pending_211.size() >= GpuWorkerProtocol::MaxMappings)
        return Nvidia::NvResult::InsufficientMemory;
    // The worker validated the backing handle, offset, alignment, device
    // and event before staging. Check conflicts across all workers here.
    const auto key = PendingMapKey{request.address_space, request.address};
    auto next = pending_211.lower_bound(key);
    if (next != pending_211.end() && next->first.first == request.address_space &&
        Overlaps(request.address, request.size, next->second.address, next->second.size))
        return Nvidia::NvResult::BadParameter;
    if (next != pending_211.begin()) {
        const auto prior = std::prev(next);
        if (prior->first.first == request.address_space &&
            Overlaps(request.address, request.size, prior->second.address, prior->second.size))
            return Nvidia::NvResult::BadParameter;
    }
    for (const auto& identity : identity_gpu_mappings) {
        if (Overlaps(request.address, request.size, identity.address, identity.size) &&
            !ExactIdentity(identity, request))
            return Nvidia::NvResult::BadParameter;
    }
    try {
        if (!pending_211.try_emplace(key, request).second)
            return Nvidia::NvResult::InvalidState;
    } catch (const std::bad_alloc&) {
        return Nvidia::NvResult::InsufficientMemory;
    }
    return Nvidia::NvResult::Success;
}

bool GpuWorkerSharedState::CancelBeforeImport(std::uint32_t id, std::uint64_t address,
                                              std::uint64_t size) {
    if (!CanStageBeforeImport(id)) return false;
    const auto it = pending_211.find({id, address});
    if (it == pending_211.end() || it->second.size != size) return false;
    pending_211.erase(it);
    return true;
}

bool GpuWorkerSharedState::ModifyBeforeImport(const GpuWorkerProtocol::Modify4K& request) {
    if (!CanStageBeforeImport(request.address_space)) return false;
    const auto it = pending_211.find({request.address_space, request.address});
    if (it == pending_211.end() || it->second.size != request.size) return false;
    it->second.selected_kind = request.selected_kind;
    it->second.backend_kind = request.backend_kind;
    return true;
}

Nvidia::NvResult GpuWorkerSharedState::BindImportedAddressSpace(
    std::uint32_t id, const std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>& as) {
    if (!id || !as) return Nvidia::NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!address_spaces.Registered(id) || !as->OwnsNativeProcess(native_process))
        return Nvidia::NvResult::InvalidState;

    // Reserve the history slot before touching GMMU. A closed imported VM
    // must not be mistaken for a new, never-imported one by a later 211.
    const auto first = pending_211.lower_bound({id, 0});
    auto imported_slot = std::find(imported_vm_ids.begin(), imported_vm_ids.end(), id);
    if (imported_slot != imported_vm_ids.end() &&
        first != pending_211.end() && first->first.first == id)
        return Nvidia::NvResult::InvalidState; // Failed rollback, never replay twice.
    if (imported_slot == imported_vm_ids.end())
        imported_slot = std::find(imported_vm_ids.begin(), imported_vm_ids.end(), 0);
    if (imported_slot == imported_vm_ids.end())
        return Nvidia::NvResult::InsufficientMemory;
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
    auto completed = first;
    for (auto& identity : identity_gpu_mappings) {
        // An exact explicit 211 view takes ownership of the same identity
        // mapping in this VM. Do not allocate/pin the same VA twice.
        const auto explicit_map = pending_211.find({id, identity.address});
        if (explicit_map != pending_211.end() && ExactIdentity(identity, explicit_map->second))
            continue;
        replay = as->MapNative4K(identity.handle, identity.address, 0, identity.size, 0);
        if (replay != Nvidia::NvResult::Success) break;
        if (!identity.AddSpace(id)) {
            // Capacity is bounded by the VM slots. On invariant failure,
            // retain pins if unmap failed; ImportNativeVm poisons the FD.
            (void)as->UnmapNative4K(identity.address, identity.size);
            replay = Nvidia::NvResult::InvalidState;
            break;
        }
    }
    // Replay the process/VM-local explicit maps before exposing the FD.
    // The existing worker ledger remains the owner for 212/213/102.
    if (replay == Nvidia::NvResult::Success) {
        for (; completed != pending_211.end() && completed->first.first == id; ++completed) {
            const auto& request = completed->second;
            replay = as->MapNative4K(request.memory, request.address, request.memory_offset,
                                     request.size, request.backend_kind, request.cache != 0);
            if (replay != Nvidia::NvResult::Success) break;
        }
    }
    // Publish the local NVDRV identity before releasing the GSV mutex.
    // Destroy must never observe opened_gpu_as with an uncommitted local ID.
    if (replay == Nvidia::NvResult::Success) {
        if (as->CommitNativeVmImport(id)) {
            *imported_slot = id;
            pending_211.erase(first, completed);
            return replay;
        }
        replay = Nvidia::NvResult::InvalidState;
    }

    bool rollback_safe = !as->IsNativeSparsePoisoned();
    while (completed != first) {
        --completed;
        const auto& request = completed->second;
        if (as->UnmapNative4K(request.address, request.size) != Nvidia::NvResult::Success)
            rollback_safe = false;
    }
    for (auto& identity : identity_gpu_mappings) {
        if (!identity.HasSpace(id)) continue;
        if (as->UnmapNative4K(identity.address, identity.size) != Nvidia::NvResult::Success)
            rollback_safe = false;
        identity.RemoveSpace(id);
    }
    // An unsuccessful rollback retains real NVMap pins; FreeHandleIfUnpinned
    // refuses to release them. ImportNativeVm poisons the failed FD.
    if (!rollback_safe) {
        *imported_slot = id;
        replay = Nvidia::NvResult::InvalidState;
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
