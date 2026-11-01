// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Process-generation-scoped identity for a research-only native GPU channel.
// Never infer native ownership from a global title flag or a recycled FD.
#include <map>
#include <mutex>
#include <optional>
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"

namespace Tegra::Control { struct ChannelState; }
namespace Service::Nvidia::Devices {
class NativeGpuChannelRegistry {
public:
    static NativeGpuChannelRegistry& Instance() {
        static NativeGpuChannelRegistry instance;
        return instance;
    }
    [[nodiscard]] bool Bind(const Tegra::Control::ChannelState* channel,
                            const Kernel::KProcess* process) {
        if (!channel || !process) return false;
        auto& registry = NativeGpuProcessRegistry::Instance();
        const auto ticket = registry.CaptureTicket(process);
        if (!ticket) return false;
        const std::scoped_lock lock{mutex};
        if (channels.size() >= 256 && !channels.contains(channel)) return false;
        auto [it, inserted] = channels.try_emplace(channel, *ticket);
        if (!inserted &&
            (it->second.process != ticket->process ||
             it->second.generation != ticket->generation)) return false;
        return registry.IsCurrent(*ticket);
    }
    // A graphics worker needs the EXACT channel's process generation to
    // capture guest shaders. Never guess from a global NS2 title flag or
    // current KProcess pointer. Callers must re-check IsCurrent(ticket)
    // before publishing asynchronous shader/pipeline work.
    [[nodiscard]] std::optional<NativeGpuProcessRegistry::Ticket> CaptureTicket(
        const Tegra::Control::ChannelState* channel) const {
        if (!channel) return std::nullopt;
        std::optional<NativeGpuProcessRegistry::Ticket> ticket;
        {
            const std::scoped_lock lock{mutex};
            const auto it = channels.find(channel);
            if (it == channels.end()) return std::nullopt;
            ticket = it->second;
        }
        if (!NativeGpuProcessRegistry::Instance().IsCurrent(*ticket))
            return std::nullopt;
        return ticket;
    }
    [[nodiscard]] bool IsNative(const Tegra::Control::ChannelState* channel) const {
        return CaptureTicket(channel).has_value();
    }
    void Unbind(const Tegra::Control::ChannelState* channel) {
        if (!channel) return;
        const std::scoped_lock lock{mutex};
        channels.erase(channel);
    }
private:
    mutable std::mutex mutex;
    std::map<const Tegra::Control::ChannelState*,NativeGpuProcessRegistry::Ticket> channels;
};
} // namespace Service::Nvidia::Devices
