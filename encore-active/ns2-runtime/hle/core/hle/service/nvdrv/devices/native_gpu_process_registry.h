// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Opt-in marker for processes that completed the experimental NS2 graphics
// bootstrap and own an IGpuSession. This replaces the upstream-only
// KProcess::UsesPAuthReturns() discriminator, which Encore's pinned kernel
// does not implement. Neither firmware nor Switch 1 uses this registry.
#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <limits>
#include <cstdint>

namespace Kernel { class KProcess; }
namespace Service::AM { struct GpuWorkerSharedState; }
namespace Service::Nvidia::Devices {

class NativeGpuProcessRegistry {
public:
    // A ticket witnesses one uninterrupted NS2 GPU-session lifetime. An
    // unregister/re-register on the same KProcess* invalidates old tickets.
    struct Ticket {
        const Kernel::KProcess* process{};
        std::uint64_t generation{};
    };
    static NativeGpuProcessRegistry& Instance() {
        static NativeGpuProcessRegistry registry;
        return registry;
    }
    bool Register(Kernel::KProcess* process) {
        if (!process) return false;
        const std::scoped_lock lock{mutex};
        if (owners.size() >= 256 && !owners.contains(process)) return false;
        // A guest GPU session can cause a map-node allocation. Allocation
        // failure must not unwind through HLE or partially publish a native
        // process epoch: no ticket exists until insertion succeeds.
        try {
            auto [it, inserted] = owners.try_emplace(process);
            if (inserted) {
                it->second.generation = NextGeneration();
                if (!it->second.generation) {
                    owners.erase(it);
                    return false;
                }
            }
            if (it->second.references == std::numeric_limits<std::size_t>::max()) return false;
            ++it->second.references;
            return true;
        } catch (const std::bad_alloc&) {
            return false;
        }
    }
    void Unregister(Kernel::KProcess* process) {
        if (!process) return;
        const std::scoped_lock lock{mutex};
        const auto it = owners.find(process);
        if (it == owners.end()) return;
        if (--it->second.references == 0) owners.erase(it);
    }
    [[nodiscard]] bool Contains(const Kernel::KProcess* process) const {
        if (!process) return false;
        const std::scoped_lock lock{mutex};
        return owners.contains(const_cast<Kernel::KProcess*>(process));
    }
    [[nodiscard]] std::optional<Ticket> CaptureTicket(
        const Kernel::KProcess* process) const {
        if (!process) return std::nullopt;
        const std::scoped_lock lock{mutex};
        const auto it = owners.find(const_cast<Kernel::KProcess*>(process));
        if (it == owners.end()) return std::nullopt;
        return Ticket{process, it->second.generation};
    }
    [[nodiscard]] bool IsCurrent(const Ticket& ticket) const {
        if (!ticket.process || !ticket.generation) return false;
        const std::scoped_lock lock{mutex};
        const auto it = owners.find(const_cast<Kernel::KProcess*>(ticket.process));
        return it != owners.end() && it->second.generation == ticket.generation;
    }
    [[nodiscard]] std::size_t Count() const {
        const std::scoped_lock lock{mutex};
        return owners.size();
    }
    // libounce GSV/NvRm contract: command 33 creates an opaque VM id,
    // root 434 registers that id, and only a later nvhost-as-gpu ioctl
    // imports it. VM ids are therefore never NVDRV file descriptors.
    [[nodiscard]] std::optional<std::uint32_t> AllocateVm(
        Kernel::KProcess* process,
        const std::shared_ptr<::Service::AM::GpuWorkerSharedState>& owner) {
        if (!process || !owner) return std::nullopt;
        const std::scoped_lock lock{mutex};
        const auto it = owners.find(process);
        if (it == owners.end()) return std::nullopt;
        for (auto& slot : it->second.vms) {
            if (slot && slot->owner.expired()) slot.reset();
            if (slot) continue;
            const auto id = NextVmId();
            if (!id) return std::nullopt;
            slot = VmEntry{id, false, owner};
            return id;
        }
        return std::nullopt;
    }
    [[nodiscard]] bool RegisterVm(Kernel::KProcess* process, std::uint32_t id) {
        if (!process || !id) return false;
        const std::scoped_lock lock{mutex};
        const auto it = owners.find(process);
        if (it == owners.end()) return false;
        for (auto& slot : it->second.vms) {
            if (!slot || slot->id != id) continue;
            if (slot->owner.expired()) {
                slot.reset();
                return false;
            }
            slot->registered = true;
            return true;
        }
        return false;
    }
    [[nodiscard]] std::shared_ptr<::Service::AM::GpuWorkerSharedState>
    FindRegisteredVmOwner(const Kernel::KProcess* process, std::uint32_t id) const {
        if (!process || !id) return {};
        const std::scoped_lock lock{mutex};
        const auto it = owners.find(const_cast<Kernel::KProcess*>(process));
        if (it == owners.end()) return {};
        for (const auto& slot : it->second.vms)
            if (slot && slot->id == id && slot->registered)
                return slot->owner.lock();
        return {};
    }
    [[nodiscard]] bool RemoveVm(
        Kernel::KProcess* process, std::uint32_t id,
        const ::Service::AM::GpuWorkerSharedState* owner) {
        if (!process || !id || !owner) return false;
        const std::scoped_lock lock{mutex};
        const auto it = owners.find(process);
        if (it == owners.end()) return false;
        for (auto& slot : it->second.vms) {
            if (!slot || slot->id != id) continue;
            const auto live = slot->owner.lock();
            if (live && live.get() != owner) return false;
            slot.reset();
            return true;
        }
        return false;
    }
private:
    mutable std::mutex mutex;
    struct VmEntry {
        std::uint32_t id{};
        bool registered{};
        std::weak_ptr<::Service::AM::GpuWorkerSharedState> owner;
    };
    struct Entry {
        std::size_t references{};
        std::uint64_t generation{};
        std::array<std::optional<VmEntry>, 8> vms{};
    };
    [[nodiscard]] std::uint64_t NextGeneration() {
        // Reserve zero as an invalid sentinel. An exhausted clock must not
        // silently reuse an older ticket and authenticate a stale shader.
        if (last_generation == std::numeric_limits<std::uint64_t>::max()) return 0;
        return ++last_generation;
    }
    [[nodiscard]] std::uint32_t NextVmId() {
        constexpr auto MaxVmId =
            static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
        if (last_vm_id >= MaxVmId) return 0;
        return ++last_vm_id;
    }
    std::map<Kernel::KProcess*, Entry> owners;
    std::uint64_t last_generation{};
    std::uint32_t last_vm_id{};
};
} // namespace Service::Nvidia::Devices
