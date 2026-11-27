// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Opt-in marker for processes that completed the experimental NS2 graphics
// bootstrap and own an IGpuSession. This replaces the upstream-only
// KProcess::UsesPAuthReturns() discriminator, which Encore's pinned kernel
// does not implement. Neither firmware nor Switch 1 uses this registry.
#include <algorithm>
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

    // NVN2 queue semaphores use two consecutive 4-KiB pages. GPU releases
    // target the second page while acquires consume the matching word in the
    // first. Keep this process-generation scoped so a recycled KProcess or
    // GPU VA can never inherit a previous title's mirror state.
    enum class SemaphoreDirectResult {
        NotTracked,
        Tracked,
        OutOfResource,
    };
    static constexpr std::size_t MaxSemaphoreRegions = 4096;
    static constexpr std::size_t MaxSemaphoreDirectWords = 1024;
    // Best-effort provenance for values published by a real retired GPU
    // fence. A miss is treated as CPU/unknown by the command-fetch policy,
    // so bounded eviction can only make Encore fall back to live reads.
    static constexpr std::size_t MaxGpuSemaphoreValues = 2048;

    [[nodiscard]] bool RegisterSemaphoreRegion(
        Kernel::KProcess* process, std::uint64_t start, std::uint64_t size) {
        if (!process || size != 0x2000 || (start & 0xfff) != 0 ||
            start > std::numeric_limits<std::uint64_t>::max() - size)
            return false;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(process);
        if (owner == owners.end()) return false;
        auto& entry = owner->second;
        auto first = entry.semaphore_regions.begin();
        auto last = first + entry.semaphore_region_count;
        auto pos = std::lower_bound(first, last, start);
        if (pos != last && *pos == start) return true;
        if (entry.semaphore_region_count >= MaxSemaphoreRegions) return false;
        std::move_backward(pos, last, last + 1);
        *pos = start;
        ++entry.semaphore_region_count;
        return true;
    }

    void UnregisterSemaphoreRegion(
        Kernel::KProcess* process, std::uint64_t start) {
        if (!process) return;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(process);
        if (owner == owners.end()) return;
        auto& entry = owner->second;
        auto first = entry.semaphore_regions.begin();
        auto last = first + entry.semaphore_region_count;
        auto pos = std::lower_bound(first, last, start);
        if (pos == last || *pos != start) return;
        std::move(pos + 1, last, pos);
        --entry.semaphore_region_count;
        entry.semaphore_regions[entry.semaphore_region_count] = 0;
        for (std::size_t i = 0; i < entry.semaphore_word_count;) {
            if (entry.semaphore_words[i].region_start == start) {
                entry.semaphore_words[i] =
                    entry.semaphore_words[entry.semaphore_word_count - 1];
                --entry.semaphore_word_count;
            } else {
                ++i;
            }
        }
        // A recycled GPU semaphore region must never inherit the producer
        // identity of its previous mapping, even when the physical page and
        // payload value happen to be reused.
        for (std::size_t i = 0; i < entry.gpu_semaphore_value_count; ++i) {
            auto& published = entry.gpu_semaphore_values[i];
            if (published.gpu_page == start ||
                published.gpu_page == start + 0x1000)
                published.valid = false;
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> SemaphoreMirrorAddress(
        const Ticket& ticket, std::uint64_t gpu_address) const {
        if (!ticket.process || !ticket.generation) return std::nullopt;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(
            const_cast<Kernel::KProcess*>(ticket.process));
        if (owner == owners.end() ||
            owner->second.generation != ticket.generation)
            return std::nullopt;
        const auto page = gpu_address & ~std::uint64_t{0xfff};
        if (page < 0x1000) return std::nullopt;
        const auto region_start = page - 0x1000;
        if (!HasSemaphoreRegion(owner->second, region_start))
            return std::nullopt;
        return gpu_address - 0x1000;
    }

    [[nodiscard]] SemaphoreDirectResult NoteDirectSemaphoreRelease(
        const Ticket& ticket, std::uint64_t gpu_address,
        std::uint64_t device_address) {
        if (!ticket.process || !ticket.generation)
            return SemaphoreDirectResult::NotTracked;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(
            const_cast<Kernel::KProcess*>(ticket.process));
        if (owner == owners.end() ||
            owner->second.generation != ticket.generation)
            return SemaphoreDirectResult::NotTracked;
        auto& entry = owner->second;
        const auto region_start = gpu_address & ~std::uint64_t{0xfff};
        if (!HasSemaphoreRegion(entry, region_start))
            return SemaphoreDirectResult::NotTracked;
        const auto word = device_address & ~std::uint64_t{3};
        for (std::size_t i = 0; i < entry.semaphore_word_count; ++i) {
            auto& direct = entry.semaphore_words[i];
            if (direct.device_address == word &&
                direct.region_start == region_start) {
                direct.has_shadow = false;
                return SemaphoreDirectResult::Tracked;
            }
        }
        if (entry.semaphore_word_count >= MaxSemaphoreDirectWords)
            return SemaphoreDirectResult::OutOfResource;
        entry.semaphore_words[entry.semaphore_word_count++] =
            SemaphoreShadowWord{word, region_start, 0, 0, false};
        return SemaphoreDirectResult::Tracked;
    }

    [[nodiscard]] bool IsSemaphoreDirectWord(
        const Ticket& ticket, std::uint64_t device_address) const {
        if (!ticket.process || !ticket.generation) return false;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(
            const_cast<Kernel::KProcess*>(ticket.process));
        if (owner == owners.end() ||
            owner->second.generation != ticket.generation)
            return false;
        const auto word = device_address & ~std::uint64_t{3};
        for (std::size_t i = 0; i < owner->second.semaphore_word_count; ++i)
            if (owner->second.semaphore_words[i].device_address == word)
                return true;
        return false;
    }

    [[nodiscard]] bool SetSemaphoreShadow(
        const Ticket& ticket, std::uint64_t device_address,
        std::uint32_t value, std::uint32_t guest) {
        if (!ticket.process || !ticket.generation) return false;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(
            const_cast<Kernel::KProcess*>(ticket.process));
        if (owner == owners.end() ||
            owner->second.generation != ticket.generation)
            return false;
        const auto word = device_address & ~std::uint64_t{3};
        for (std::size_t i = 0; i < owner->second.semaphore_word_count; ++i) {
            auto& direct = owner->second.semaphore_words[i];
            if (direct.device_address != word) continue;
            direct.value = value;
            direct.guest = guest;
            direct.has_shadow = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] std::optional<std::uint32_t> ReadSemaphoreShadow(
        const Ticket& ticket, std::uint64_t device_address,
        std::uint32_t guest) {
        if (!ticket.process || !ticket.generation) return std::nullopt;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(
            const_cast<Kernel::KProcess*>(ticket.process));
        if (owner == owners.end() ||
            owner->second.generation != ticket.generation)
            return std::nullopt;
        const auto word = device_address & ~std::uint64_t{3};
        for (std::size_t i = 0; i < owner->second.semaphore_word_count; ++i) {
            auto& direct = owner->second.semaphore_words[i];
            if (direct.device_address != word || !direct.has_shadow)
                continue;
            if (direct.guest != guest) {
                direct.has_shadow = false;
                return std::nullopt;
            }
            return direct.value;
        }
        return std::nullopt;
    }

    // Remember the exact logical value that became visible only after a real
    // backend GPU fence retired. This is not guest state: it lets the native
    // command fetcher distinguish GPU->GPU dependencies from semaphores that
    // the guest CPU released after SubmitGPFIFO/Present.
    void RecordGpuSemaphoreValue(
        const Ticket& ticket, std::uint64_t gpu_address,
        std::uint64_t device_address, std::uint32_t value) {
        if (!ticket.process || !ticket.generation) return;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(
            const_cast<Kernel::KProcess*>(ticket.process));
        if (owner == owners.end() ||
            owner->second.generation != ticket.generation)
            return;
        auto& entry = owner->second;
        const auto word = device_address & ~std::uint64_t{3};
        const auto page = gpu_address & ~std::uint64_t{0xfff};
        for (std::size_t i = 0; i < entry.gpu_semaphore_value_count; ++i) {
            auto& published = entry.gpu_semaphore_values[i];
            if (published.valid && published.device_address == word &&
                published.gpu_page == page) {
                published.value = value;
                return;
            }
        }
        const std::size_t slot =
            entry.gpu_semaphore_value_next++ % MaxGpuSemaphoreValues;
        entry.gpu_semaphore_values[slot] =
            GpuSemaphoreValue{word, page, value, true};
        if (entry.gpu_semaphore_value_count < MaxGpuSemaphoreValues)
            ++entry.gpu_semaphore_value_count;
    }

    // True only while the currently observed logical word still equals the
    // last value published by a real GPU retirement for this process/page.
    // A mismatch revokes stale provenance immediately. Absence/eviction is
    // deliberately conservative and returns false.
    [[nodiscard]] bool IsGpuProducedSemaphoreValue(
        const Ticket& ticket, std::uint64_t gpu_address,
        std::uint64_t device_address, std::uint32_t observed) {
        if (!ticket.process || !ticket.generation) return false;
        const std::scoped_lock lock{mutex};
        const auto owner = owners.find(
            const_cast<Kernel::KProcess*>(ticket.process));
        if (owner == owners.end() ||
            owner->second.generation != ticket.generation)
            return false;
        auto& entry = owner->second;
        const auto word = device_address & ~std::uint64_t{3};
        const auto page = gpu_address & ~std::uint64_t{0xfff};
        for (std::size_t i = 0; i < entry.gpu_semaphore_value_count; ++i) {
            auto& published = entry.gpu_semaphore_values[i];
            if (!published.valid || published.device_address != word ||
                published.gpu_page != page)
                continue;
            if (published.value != observed) {
                published.valid = false;
                return false;
            }
            return true;
        }
        return false;
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
    struct SemaphoreShadowWord {
        std::uint64_t device_address{};
        std::uint64_t region_start{};
        std::uint32_t value{};
        std::uint32_t guest{};
        bool has_shadow{};
    };
    struct GpuSemaphoreValue {
        std::uint64_t device_address{};
        std::uint64_t gpu_page{};
        std::uint32_t value{};
        bool valid{};
    };
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
        std::array<std::uint64_t, MaxSemaphoreRegions> semaphore_regions{};
        std::size_t semaphore_region_count{};
        std::array<SemaphoreShadowWord, MaxSemaphoreDirectWords> semaphore_words{};
        std::size_t semaphore_word_count{};
        std::array<GpuSemaphoreValue, MaxGpuSemaphoreValues> gpu_semaphore_values{};
        std::size_t gpu_semaphore_value_count{};
        std::size_t gpu_semaphore_value_next{};
    };
    [[nodiscard]] static bool HasSemaphoreRegion(
        const Entry& entry, std::uint64_t start) {
        const auto first = entry.semaphore_regions.begin();
        const auto last = first + entry.semaphore_region_count;
        return std::binary_search(first, last, start);
    }
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
