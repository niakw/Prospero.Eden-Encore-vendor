// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A pure state transition layer extracted from upstream GraphicsSupervisor
// commands 4, 11, 101, 401, 402, 497. This is NOT the full kernel-owned
// session or NVIDIA driver; success here permits (not creates) a session.
#include <cstdint>
#include <span>
#include "core/hle/service/am/service/graphics_supervisor_protocol.h"

namespace Service::AM::GraphicsSupervisorBootstrap {
using U32=std::uint32_t;
using U64=std::uint64_t;
enum class Phase { Created, Initialized, ProcessBound, Ready };
enum class Status {
    Success,
    InvalidPacket,
    InvalidHandle,
    InvalidState,
    InvalidArgument,
    Unsupported,
};
class State {
public:
    explicit State(U64 expected_aruid) : aruid{expected_aruid} {}
    Status Initialize(std::span<const U32> packet, bool process_matches) {
        const auto request=GraphicsSupervisorProtocol::DecodeBootstrap(packet);
        if (!request) return Status::InvalidPacket;
        if (!process_matches) return Status::InvalidHandle;
        if (phase!=Phase::Created) return Status::InvalidState;
        if (request->applet_resource_user_id!=aruid)
            return Status::InvalidArgument;
        if (!GraphicsSupervisorProtocol::IsLocalBootstrap(*request))
            return Status::Unsupported;
        profile=GraphicsSupervisorProtocol::NativeGpuProfileForBootstrap(request->flags);
        phase=Phase::Initialized;
        return Status::Success;
    }
    Status BindProcess(std::span<const U32> packet, bool process_matches) {
        if (packet.size()<3 || packet[0]!=0x1b ||
            packet[1]!=0x80000000 || packet[2]!=2)
            return Status::InvalidPacket;
        if (!process_matches) return Status::InvalidHandle;
        if (phase!=Phase::Initialized) return Status::InvalidState;
        phase=Phase::ProcessBound;
        return Status::Success;
    }
    Status Complete(std::span<const U32> packet) {
        if (packet.size()<2 || packet[0]!=0x75 || packet[1]!=0)
            return Status::InvalidPacket;
        if (phase!=Phase::ProcessBound) return Status::InvalidState;
        phase=Phase::Ready;
        return Status::Success;
    }
    Status InitializeGpuContext(std::span<const U32> packet, U32 command) {
        if ((command!=401 && command!=402) ||
            packet.size()<2 || packet[0]!=16+command || packet[1]!=0)
            return Status::InvalidPacket;
        if (phase!=Phase::Ready) return Status::InvalidState;
        if (command==401) {
            gpu_initialized=true;
        } else if (!gpu_initialized) {
            return Status::InvalidState;
        } else {
            gpu_ready=true;
        }
        return Status::Success;
    }
    Status CanOpenGpuSession(std::span<const U32> packet, bool module_available) const {
        if (packet.size()<4 || packet[0]!=0x201 || packet[1]!=2 ||
            (U64{packet[2]} | (U64{packet[3]} << 32))!=4)
            return Status::InvalidPacket;
        return phase==Phase::Ready && module_available
            ? Status::Success : Status::InvalidState;
    }
    [[nodiscard]] Phase CurrentPhase() const {return phase;}
    [[nodiscard]] U32 NativeProfile() const {return profile;}
    [[nodiscard]] bool GpuContextReady() const {return gpu_ready;}
private:
    const U64 aruid;
    Phase phase{Phase::Created};
    U32 profile{};
    bool gpu_initialized{};
    bool gpu_ready{};
};
} // namespace Service::AM::GraphicsSupervisorBootstrap
