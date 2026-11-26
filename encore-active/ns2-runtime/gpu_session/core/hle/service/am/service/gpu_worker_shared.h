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
#include <vector>

#include "core/hle/service/am/service/gpu_session_protocol.h"
#include "core/hle/service/am/service/gpu_worker_protocol.h"
#include "core/hle/service/nvdrv/nvdata.h"
#include "core/hle/service/kernel_helpers.h"

namespace Kernel { class KEvent; class KProcess; }
namespace Service::Nvidia { class Module; namespace Devices { class nvhost_as_gpu; } }
namespace Service::AM {

struct IdentityGpuMapping {
    std::uint32_t handle{};
    std::uint64_t address{};
    std::uint64_t size{};
    std::array<std::uint32_t, GpuSessionProtocol::MaxAddressSpaces> mapped_spaces{};
    std::size_t mapped_count{};

    [[nodiscard]] bool HasSpace(std::uint32_t id) const {
        for (std::size_t i = 0; i < mapped_count; ++i)
            if (mapped_spaces[i] == id) return true;
        return false;
    }
    [[nodiscard]] bool AddSpace(std::uint32_t id) {
        if (HasSpace(id)) return true;
        if (mapped_count >= mapped_spaces.size()) return false;
        mapped_spaces[mapped_count++] = id;
        return true;
    }
    void RemoveSpace(std::uint32_t id) {
        for (std::size_t i = 0; i < mapped_count; ++i) {
            if (mapped_spaces[i] != id) continue;
            mapped_spaces[i] = mapped_spaces[mapped_count - 1];
            mapped_spaces[--mapped_count] = 0;
            return;
        }
    }
};

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
    // Opaque GSV VM id -> externally opened/imported nvhost-as-gpu object.
    // libounce proves the VM id is distinct from the NVDRV descriptor.
    std::map<std::uint32_t, std::weak_ptr<Nvidia::Devices::nvhost_as_gpu>> opened_gpu_as;
    // gpu sub 101 is an identity map on Switch 2: GPU VA == CPU VA.
    // Keep it process-local and replay it into later-created GMMUs.
    std::vector<IdentityGpuMapping> identity_gpu_mappings;
    // A registered GSV VM exists before NVDRV imports it. 211 owns real
    // backing through its worker ledger even in that interval. No channel
    // can submit until the exact VM has replayed all these mappings.
    using PendingMapKey = std::pair<std::uint32_t, std::uint64_t>;
    std::map<PendingMapKey, GpuWorkerProtocol::Map4K> pending_211;
    std::array<std::uint32_t, GpuSessionProtocol::MaxAddressSpaces> imported_vm_ids{};
    [[nodiscard]] bool CanStageBeforeImport(std::uint32_t id) const;
    Nvidia::NvResult StageBeforeImport(const GpuWorkerProtocol::Map4K& request);
    bool CancelBeforeImport(std::uint32_t id, std::uint64_t address, std::uint64_t size);
    bool ModifyBeforeImport(const GpuWorkerProtocol::Modify4K& request);
    [[nodiscard]] bool OverlapsPendingIdentity(std::uint64_t address, std::uint64_t size) const;
    // Caller holds mutex except during final shared-state destruction.
    [[nodiscard]] std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
    FindOwnedAddressSpace(std::uint32_t id) const;
    Nvidia::NvResult BindImportedAddressSpace(
        std::uint32_t id, const std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>& as);
    void UnbindImportedAddressSpace(
        std::uint32_t id, const Nvidia::Devices::nvhost_as_gpu* as);
    KernelHelpers::ServiceContext event_context;
    std::array<Kernel::KEvent*, GpuSessionProtocol::MaxNotificationEvents> events{};
    std::shared_ptr<Nvidia::Module> module;
    Kernel::KProcess* native_process{};
    bool native_registered{};
};
} // namespace Service::AM
