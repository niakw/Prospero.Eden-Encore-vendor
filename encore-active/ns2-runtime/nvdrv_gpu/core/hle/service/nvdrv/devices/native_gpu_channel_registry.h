// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Process-generation-scoped identity for a research-only native GPU channel.
// Never infer native ownership from a global title flag or a recycled FD.
#include <map>
#include <mutex>
#include <optional>
#include <memory>
#include <limits>
#include <cstdint>
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"

namespace Tegra::Control { struct ChannelState; }
namespace Tegra { struct NativeQmdSession; }
namespace Shader::Ampere { class NativeTranslationCache; }
namespace Service::Nvidia::Devices {
class NativeGpuChannelRegistry {
public:
    struct Ticket {
        const Kernel::KProcess* process{};
        std::uint64_t generation{};
        // A new epoch is assigned on every Unbind / Bind transition, even
        // if the same process remains alive at the same ChannelState address.
        std::uint64_t channel_generation{};
    };
    static NativeGpuChannelRegistry& Instance() {
        static NativeGpuChannelRegistry instance;
        return instance;
    }
    [[nodiscard]] bool Bind(const Tegra::Control::ChannelState* channel,
                            const Kernel::KProcess* process) {
        if (!channel || !process) return false;
        auto& registry = NativeGpuProcessRegistry::Instance();
        const auto process_ticket = registry.CaptureTicket(process);
        if (!process_ticket) return false;
        const std::scoped_lock lock{mutex};
        if (channels.size() >= 256 && !channels.contains(channel)) return false;
        auto it = channels.find(channel);
        if (it == channels.end()) {
            if (last_channel_generation == std::numeric_limits<std::uint64_t>::max())
                return false;
            it = channels.emplace(
                channel, Ticket{process_ticket->process, process_ticket->generation,
                                ++last_channel_generation}).first;
        } else if (it->second.process != process_ticket->process ||
                   it->second.generation != process_ticket->generation) {
            return false;
        }
        if (!registry.IsCurrent(*process_ticket)) {
            // Failed authorization must never leave an apparently bound
            // channel behind, including a concurrent process shutdown.
            channels.erase(it);
            return false;
        }
        return true;
    }
    // 3D class 0xC797 is bound by the GPU command puller, not by the
    // NVDRV process registry. Track its explicit engine AND subchannel
    // identity; the renderer must not infer Ampere from a title ID.
    [[nodiscard]] bool BindGraphicsEngine(
        const Tegra::Control::ChannelState* channel,
        const void* graphics_engine, std::uint32_t subchannel) {
        if (!channel || !graphics_engine || subchannel >= 8) return false;
        const std::scoped_lock lock{mutex};
        const auto channel_it = channels.find(channel);
        if (channel_it == channels.end()) return false;
        const auto& ticket = channel_it->second;
        if (!NativeGpuProcessRegistry::Instance().IsCurrent(
                {ticket.process, ticket.generation}))
            return false;
        const auto existing = engines.find(graphics_engine);
        if (existing != engines.end() &&
            (existing->second.channel != channel ||
             existing->second.ticket.channel_generation != ticket.channel_generation))
            return false;
        auto [it, inserted] = engines.try_emplace(
            graphics_engine, GraphicsOwner{channel, ticket, 0});
        it->second.subchannels |= static_cast<std::uint8_t>(1u << subchannel);
        return true;
    }
    void UnbindGraphicsSubchannel(
        const Tegra::Control::ChannelState* channel,
        const void* graphics_engine, std::uint32_t subchannel) {
        if (!channel || !graphics_engine || subchannel >= 8) return;
        const std::scoped_lock lock{mutex};
        const auto it = engines.find(graphics_engine);
        if (it == engines.end() || it->second.channel != channel) return;
        it->second.subchannels &= static_cast<std::uint8_t>(~(1u << subchannel));
        if (!it->second.subchannels) engines.erase(it);
    }
    [[nodiscard]] std::optional<Ticket> CaptureGraphicsTicket(
        const void* graphics_engine) const {
        if (!graphics_engine) return std::nullopt;
        const std::scoped_lock lock{mutex};
        const auto owner = engines.find(graphics_engine);
        if (owner == engines.end() || !owner->second.subchannels) return std::nullopt;
        const auto channel = channels.find(owner->second.channel);
        if (channel == channels.end() ||
            channel->second.process != owner->second.ticket.process ||
            channel->second.generation != owner->second.ticket.generation ||
            channel->second.channel_generation != owner->second.ticket.channel_generation ||
            !NativeGpuProcessRegistry::Instance().IsCurrent(
                {owner->second.ticket.process, owner->second.ticket.generation}))
            return std::nullopt;
        return owner->second.ticket;
    }
    // Ampere compute class 0xC7C0 uses the same legacy register object,
    // but must not inherit the Switch 1 compute shader cache identity.
    // Require an explicit subchannel binding and the same process/channel
    // generation checks as native 3D, with an independent engine domain.
    [[nodiscard]] bool BindComputeEngine(
        const Tegra::Control::ChannelState* channel,
        const void* compute_engine, std::uint32_t subchannel) {
        if (!channel || !compute_engine || subchannel >= 8) return false;
        const std::scoped_lock lock{mutex};
        const auto owner = channels.find(channel);
        if (owner == channels.end() ||
            !NativeGpuProcessRegistry::Instance().IsCurrent(
                {owner->second.process, owner->second.generation})) return false;
        auto old = compute_engines.find(compute_engine);
        if (old != compute_engines.end() &&
            (old->second.channel != channel ||
             old->second.ticket.channel_generation != owner->second.channel_generation))
            return false;
        auto [it, inserted] = compute_engines.try_emplace(
            compute_engine, ComputeOwner{channel, owner->second, 0});
        it->second.subchannels |= static_cast<std::uint8_t>(1u << subchannel);
        return true;
    }
    void UnbindComputeSubchannel(
        const Tegra::Control::ChannelState* channel,
        const void* compute_engine, std::uint32_t subchannel) {
        if (!channel || !compute_engine || subchannel >= 8) return;
        const std::scoped_lock lock{mutex};
        auto it = compute_engines.find(compute_engine);
        if (it == compute_engines.end() || it->second.channel != channel) return;
        it->second.subchannels &= static_cast<std::uint8_t>(~(1u << subchannel));
        if (!it->second.subchannels) compute_engines.erase(it);
    }
    [[nodiscard]] std::optional<Ticket> CaptureComputeTicket(
        const void* compute_engine) const {
        if (!compute_engine) return std::nullopt;
        const std::scoped_lock lock{mutex};
        const auto owner = compute_engines.find(compute_engine);
        if (owner == compute_engines.end() || !owner->second.subchannels)
            return std::nullopt;
        const auto channel = channels.find(owner->second.channel);
        if (channel == channels.end() ||
            channel->second.process != owner->second.ticket.process ||
            channel->second.generation != owner->second.ticket.generation ||
            channel->second.channel_generation != owner->second.ticket.channel_generation ||
            !NativeGpuProcessRegistry::Instance().IsCurrent(
                {owner->second.ticket.process, owner->second.ticket.generation}))
            return std::nullopt;
        return owner->second.ticket;
    }
    [[nodiscard]] bool IsComputeCurrent(
        const void* compute_engine, const Ticket& ticket) const {
        if (!compute_engine || !ticket.process || !ticket.generation ||
            !ticket.channel_generation) return false;
        const std::scoped_lock lock{mutex};
        const auto owner = compute_engines.find(compute_engine);
        if (owner == compute_engines.end() || !owner->second.subchannels ||
            owner->second.ticket.process != ticket.process ||
            owner->second.ticket.generation != ticket.generation ||
            owner->second.ticket.channel_generation != ticket.channel_generation)
            return false;
        const auto channel = channels.find(owner->second.channel);
        return channel != channels.end() &&
               channel->second.process == ticket.process &&
               channel->second.generation == ticket.generation &&
               channel->second.channel_generation == ticket.channel_generation &&
               NativeGpuProcessRegistry::Instance().IsCurrent(
                   {ticket.process, ticket.generation});
    }
    // QMD dependency state belongs to a guest process epoch, across its
    // distinct GPU channels. Definition lives in NS2 video_core so normal
    // NVDRV never links the research QMD compiler.
    [[nodiscard]] std::shared_ptr<Tegra::NativeQmdSession> AcquireNativeQmdSession(
        const Tegra::Control::ChannelState* channel);
    // Shader translation cache is owned by this exact engine bind epoch.
    // Implemented in video_core's opt-in native shader owner, so ordinary
    // NVDRV/core builds never acquire Ampere compiler dependencies.
    [[nodiscard]] std::shared_ptr<Shader::Ampere::NativeTranslationCache>
    AcquireGraphicsTranslationCache(const void* graphics_engine, const Ticket& ticket);
    // Compute shader translations are independent of graphics shader code
    // and belong to the same authenticated Compute bind epoch as its QMDs.
    [[nodiscard]] std::shared_ptr<Shader::Ampere::NativeTranslationCache>
    AcquireComputeTranslationCache(const void* compute_engine, const Ticket& ticket);
    [[nodiscard]] bool IsGraphicsCurrent(const void* graphics_engine,
                                         const Ticket& ticket) const {
        if (!graphics_engine || !ticket.process || !ticket.generation ||
            !ticket.channel_generation) return false;
        const std::scoped_lock lock{mutex};
        const auto owner = engines.find(graphics_engine);
        if (owner == engines.end() || !owner->second.subchannels ||
            owner->second.ticket.process != ticket.process ||
            owner->second.ticket.generation != ticket.generation ||
            owner->second.ticket.channel_generation != ticket.channel_generation)
            return false;
        const auto channel = channels.find(owner->second.channel);
        return channel != channels.end() &&
               channel->second.process == ticket.process &&
               channel->second.generation == ticket.generation &&
               channel->second.channel_generation == ticket.channel_generation &&
               NativeGpuProcessRegistry::Instance().IsCurrent(
                   {ticket.process, ticket.generation});
    }
    // A native Vulkan worker may capture the exact GPU channel lifetime.
    // Before publishing shader/pipeline output, revalidate this ticket
    // against BOTH the process generation and the channel bind epoch.
    [[nodiscard]] std::optional<Ticket> CaptureTicket(
        const Tegra::Control::ChannelState* channel) const {
        if (!channel) return std::nullopt;
        const std::scoped_lock lock{mutex};
        const auto it = channels.find(channel);
        if (it == channels.end()) return std::nullopt;
        if (!NativeGpuProcessRegistry::Instance().IsCurrent(
                {it->second.process, it->second.generation}))
            return std::nullopt;
        return it->second;
    }
    [[nodiscard]] bool IsCurrent(const Tegra::Control::ChannelState* channel,
                                 const Ticket& ticket) const {
        if (!channel || !ticket.process || !ticket.generation ||
            !ticket.channel_generation) return false;
        const std::scoped_lock lock{mutex};
        const auto it = channels.find(channel);
        if (it == channels.end() ||
            it->second.process != ticket.process ||
            it->second.generation != ticket.generation ||
            it->second.channel_generation != ticket.channel_generation)
            return false;
        return NativeGpuProcessRegistry::Instance().IsCurrent(
            {ticket.process, ticket.generation});
    }
    [[nodiscard]] bool IsNative(const Tegra::Control::ChannelState* channel) const {
        return CaptureTicket(channel).has_value();
    }
    void Unbind(const Tegra::Control::ChannelState* channel) {
        if (!channel) return;
        const std::scoped_lock lock{mutex};
        const auto bound = channels.find(channel);
        const auto process = bound == channels.end() ? nullptr : bound->second.process;
        channels.erase(channel);
        for (auto it = engines.begin(); it != engines.end();) {
            if (it->second.channel == channel) it = engines.erase(it);
            else ++it;
        }
        for (auto it = compute_engines.begin(); it != compute_engines.end();) {
            if (it->second.channel == channel) it = compute_engines.erase(it);
            else ++it;
        }
        if (process) {
            bool still_owned = false;
            for (const auto& [other, ticket] : channels) {
                if (ticket.process == process) {
                    still_owned = true;
                    break;
                }
            }
            if (!still_owned) qmd_sessions.erase(process);
        }
    }
private:
    mutable std::mutex mutex;
    std::map<const Tegra::Control::ChannelState*, Ticket> channels;
    struct GraphicsOwner {
        const Tegra::Control::ChannelState* channel{};
        Ticket ticket{};
        std::uint8_t subchannels{};
        std::shared_ptr<Shader::Ampere::NativeTranslationCache> translation_cache{};
    };
    std::map<const void*, GraphicsOwner> engines;
    struct ComputeOwner {
        const Tegra::Control::ChannelState* channel{};
        Ticket ticket{};
        std::uint8_t subchannels{};
        std::shared_ptr<Shader::Ampere::NativeTranslationCache> translation_cache{};
    };
    std::map<const void*, ComputeOwner> compute_engines;
    struct QmdOwner {
        std::uint64_t generation{};
        std::shared_ptr<Tegra::NativeQmdSession> session{};
    };
    std::map<const Kernel::KProcess*, QmdOwner> qmd_sessions;
    std::uint64_t last_channel_generation{};
};
} // namespace Service::Nvidia::Devices
