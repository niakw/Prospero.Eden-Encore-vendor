// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Opt-in marker for processes that completed the experimental NS2 graphics
// bootstrap and own an IGpuSession. This replaces the upstream-only
// KProcess::UsesPAuthReturns() discriminator, which Encore's pinned kernel
// does not implement. Neither firmware nor Switch 1 uses this registry.
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <limits>
#include <cstdint>

namespace Kernel { class KProcess; }
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
private:
    mutable std::mutex mutex;
    struct Entry {
        std::size_t references{};
        std::uint64_t generation{};
    };
    [[nodiscard]] std::uint64_t NextGeneration() {
        // Reserve zero as an invalid sentinel. An exhausted clock must not
        // silently reuse an older ticket and authenticate a stale shader.
        if (last_generation == std::numeric_limits<std::uint64_t>::max()) return 0;
        return ++last_generation;
    }
    std::map<Kernel::KProcess*, Entry> owners;
    std::uint64_t last_generation{};
};
} // namespace Service::Nvidia::Devices
