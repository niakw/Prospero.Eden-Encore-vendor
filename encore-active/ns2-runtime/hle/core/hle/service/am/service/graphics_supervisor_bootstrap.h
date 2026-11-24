// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A pure state transition layer extracted from upstream GraphicsSupervisor
// commands 4, 11, 101, 401, 402, 497. This is NOT the full kernel-owned
// session or external guest driver; success here permits (not creates) a session.
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
    Status InitializeTransferMemory(std::span<const U32> packet, bool owner_matches) {
        const auto request=GraphicsSupervisorProtocol::DecodeBootstrapTransferMemory(packet);
        if (!request) return Status::InvalidPacket;
        if (!owner_matches) return Status::InvalidHandle;
        if (phase!=Phase::Created) return Status::InvalidState;
        if (request->applet_resource_user_id!=aruid)
            return Status::InvalidArgument;
        // GSV 1 does not advertise a GPU architecture or feature grant.
        // Preserve the unknown profile, and leave real VA allocation to NVDRV.
        profile=0;
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
        // The observed NS2 GSV boot sequence is 4 -> 101 -> 1402: some
        // SDKs never issue the optional SetCurrentProcessHandle (11). The
        // initial 4 already checked the copied KProcess and ARUID, so 101
        // may complete that authenticated bootstrap directly. Retain 11 as
        // an additional identity check when the guest does send it.
        if (phase!=Phase::Initialized && phase!=Phase::ProcessBound)
            return Status::InvalidState;
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
    Status DestroyGpuContext(std::span<const U32> packet, bool resources_released) {
        // TIPC 403 has no input/output; do not revoke a live VA/session.
        if (packet.size()<2 || packet[0]!=16+403 || packet[1]!=0)
            return Status::InvalidPacket;
        if (phase!=Phase::Ready || !gpu_initialized || !resources_released)
            return Status::InvalidState;
        gpu_initialized=false;
        gpu_ready=false;
        return Status::Success;
    }
    // The SDK initializes this process-owned context during graphics boot,
    // before creating any GPU session. It has no image/remote-memory handle.
    // Track context lifetime separately from actual native image backing;
    // admission does not grant cross-process access or allocate guest RAM.
    Status InitializeInterModuleImageContext(std::span<const U32> packet, U32 command) {
        if ((command != 1501 && command != 1502) || packet.size() < 2 ||
            packet[0] != 16 + command || packet[1] != 0)
            return Status::InvalidPacket;
        if (phase != Phase::Ready) return Status::InvalidState;
        if (command == 1501 && !inter_module_image_created) {
            inter_module_image_created = true;
            return Status::Success;
        }
        if (command == 1502 && inter_module_image_created && !inter_module_image_ready) {
            inter_module_image_ready = true;
            return Status::Success;
        }
        return Status::InvalidState;
    }
    [[nodiscard]] bool InterModuleImageReady() const { return inter_module_image_ready; }
    Status CanOpenGpuSession(std::span<const U32> packet, bool module_available) const {
        // Remote-memory 497 is the native SDK20+ form. The firmware-19
        // 496 carries a COPY TransferMemory handle and an additional size.
        // Its guest handle/owner/bounds are checked by the HLE dispatcher.
        U64 version{};
        if (packet.size() >= 4 && packet[0] == 16+497 && packet[1] == 2) {
            version = U64{packet[2]} | (U64{packet[3]} << 32);
        } else if (const auto transfer =
                       GraphicsSupervisorProtocol::DecodeGpuTransferSession(packet)) {
            version = transfer->version;
        } else {
            return Status::InvalidPacket;
        }
        // Firmware 19.0.0 documents version 2; later SDK captures include
        // version 4 (Eglantine #574). Preserve both observed ABI versions.
        if (version != 2 && version != 4)
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
    bool inter_module_image_created{};
    bool inter_module_image_ready{};
};
} // namespace Service::AM::GraphicsSupervisorBootstrap
