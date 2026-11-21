// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#ifdef PS5_NATIVE
#include "core/hle/service/nvdrv/devices/native_gpu_context.h"
#endif
#include <string>
#include <utility>
#include "common/logging.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"

namespace Service::Nvidia::Devices {
class nx2_stub_device final : public nvdevice {
public:
    using TsgChannelVerifier = std::function<bool(NvCore::SessionId, DeviceFD)>;
    explicit nx2_stub_device(Core::System& system_, std::string name_,
                             TsgChannelVerifier verify_channel_ = {})
        : nvdevice{system_}, name{std::move(name_)},
          verify_channel{std::move(verify_channel_)} {}
    NvResult Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<u8> output) override {
        return Stub(fd, command, input, output, {});
    }
    NvResult Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<const u8> inline_input, std::span<u8> output) override {
        (void)inline_input;
        return Stub(fd, command, input, output, {});
    }
    NvResult Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<u8> output, std::span<u8> inline_output) override {
        return Stub(fd, command, input, output, inline_output);
    }
    void OnOpen(NvCore::SessionId session, DeviceFD fd) override {
#ifdef PS5_NATIVE
        tsg_owner = session;
#endif
        LOG_WARNING(Service_NVDRV, "(STUBBED) {} opened as fd {}", name, fd);
    }
    void OnClose(DeviceFD) override {
#ifdef PS5_NATIVE
        const std::scoped_lock lock{tsg_mutex};
        tsg_bindings.fill(std::nullopt);
        tsg_owner.reset();
#endif
    }
private:
#ifdef PS5_NATIVE
    // Switch 2 NVGPU_TSG_IOCTL_BIND_CHANNEL(_EX) really links a GPU channel
    // FD to a process-owned TSG subcontext. Unlike the old generic stub, no
    // foreign FD, invalid VEID or duplicate conflicting association succeeds.
    NvResult BindTsgChannel(std::span<const u8> input, std::span<u8> output,
                            bool extended) {
        const auto request = DecodeNativeTsgBinding(input, extended);
        if (!request || output.size() < input.size())
            return NvResult::InvalidSize;
        const std::scoped_lock lock{tsg_mutex};
        if (!tsg_owner || !verify_channel ||
            !verify_channel(*tsg_owner, request->channel_fd))
            return NvResult::InvalidState;
        for (auto& current : tsg_bindings) {
            if (current && current->channel_fd == request->channel_fd) {
                if (current->subcontext_id != request->subcontext_id)
                    return NvResult::AlreadyAllocated;
                std::copy(input.begin(), input.end(), output.begin());
                return NvResult::Success;
            }
        }
        for (auto& current : tsg_bindings) {
            if (!current) {
                current = *request;
                std::copy(input.begin(), input.end(), output.begin());
                return NvResult::Success;
            }
        }
        return NvResult::InsufficientMemory;
    }
    NvResult TsgIoctl(Ioctl command, std::span<const u8> input,
                      std::span<u8> output) {
        switch (static_cast<u32>(command.raw)) {
        case 0xC0045401U: // BIND_CHANNEL: s32 channel FD
            return BindTsgChannel(input, output, false);
        case 0xC018540BU: // BIND_CHANNEL_EX: s32 FD, u32 VEID, reserved[4]
            return BindTsgChannel(input, output, true);
        case 0xC0045402U: { // UNBIND_CHANNEL
            const auto request = DecodeNativeTsgBinding(input, false);
            if (!request || output.size() < input.size())
                return NvResult::InvalidSize;
            const std::scoped_lock lock{tsg_mutex};
            for (auto& current : tsg_bindings) {
                if (current && current->channel_fd == request->channel_fd) {
                    current.reset();
                    std::copy(input.begin(), input.end(), output.begin());
                    return NvResult::Success;
                }
            }
            return NvResult::BadParameter;
        }
        default:
            // Enable, preempt, event control and runlist scheduling cannot be
            // claimed until backed by real host GPU scheduling and events.
            return NvResult::NotImplemented;
        }
    }
#endif
    NvResult Stub(DeviceFD fd, Ioctl command, std::span<const u8> input,
                  std::span<u8> output, std::span<u8> inline_output) {
#ifdef PS5_NATIVE
        if (name == "/dev/nvhost-tsg-gpu")
            return TsgIoctl(command, input, output);
#endif
        LOG_WARNING(Service_NVDRV,
                    "(NOT IMPLEMENTED) {} fd={} ioctl={:08X} group={:01X} command={:02X} in={:#x}",
                    name, fd, command.raw, command.group, command.cmd, input.size());
        // not implemented yet on Encore: opening an unknown Switch 2 nvhost
        // node is allowed so NVN can continue discovery, but an unknown ioctl
        // must never be acknowledged with fabricated output/state.
        (void)output;
        (void)inline_output;
        return NvResult::NotImplemented;
    }
    std::string name;
    TsgChannelVerifier verify_channel;
#ifdef PS5_NATIVE
    std::mutex tsg_mutex;
    std::optional<NvCore::SessionId> tsg_owner;
    std::array<std::optional<NativeTsgBinding>, NativeGpuSubcontextCount>
        tsg_bindings{};
#endif
};
} // namespace Service::Nvidia::Devices
