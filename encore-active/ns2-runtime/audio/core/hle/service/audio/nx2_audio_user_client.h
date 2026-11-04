// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

#include "core/hle/service/audio/audio_in_manager.h"
#include "core/hle/service/audio/errors.h"
#include "core/hle/service/audio/audio_out_manager.h"
#include "core/hle/service/audio/audio_renderer_manager.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

// Switch 2 "aud:u" (nn::audio::CreateUserClientSessionByHipc). The SDK reaches the Switch 1
// audio interfaces through it: command 2 returns an object used exactly like
// IAudioRendererManager (vtable slot 1 = GetWorkBufferSize with a Switch 1 parameter block), so
// it is backed by Eden's audren:u implementation. Command 1 is nn::audio::OpenAudioOut's
// manager, called like IAudioOutManager (slot 1 = OpenAudioOut), backed by audout:u. Unknown commands remain explicitly unimplemented in Encore;
// no blanket success fallback is enabled.
class INx2AudioUserClient final : public ServiceFramework<INx2AudioUserClient> {
public:
    explicit INx2AudioUserClient(Core::System& system_)
        : ServiceFramework{system_, "nx2:aud:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, &INx2AudioUserClient::GetAudioOutManager, "GetAudioOutManager (guessed)"},
            {2, &INx2AudioUserClient::GetAudioRendererManager, "GetAudioRendererManager (guessed)"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

private:
    void GetAudioOutManager(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 arg = rp.Pop<u32>();
        LOG_WARNING(Service_Audio, "(STUBBED) Switch 2 aud:u 1 arg={}, returning IAudioOutManager",
                    arg);
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IAudioOutManager>(ctx, system);
    }

    void GetAudioRendererManager(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 arg = rp.Pop<u32>();
        LOG_WARNING(Service_Audio, "(STUBBED) Switch 2 aud:u 2 arg={}, returning IAudioRendererManager",
                    arg);
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IAudioRendererManager>(ctx, system);
    }

};

// NS2 virtual-output DEVICE DISCOVERY. This is deliberately not a synthetic
// sound mixer: the actual sysmix stream/ring owner has not been linked into
// Encore. Discovery returns truthful static device metadata, while format
// admission and stream attach explicitly refuse rather than claiming audio
// playback or publishing fabricated stream IDs. The maintained NS1 audio
// managers and audio sink are not replaced.
class INx2AudioVirtualDeviceManager final
    : public ServiceFramework<INx2AudioVirtualDeviceManager> {
public:
    explicit INx2AudioVirtualDeviceManager(Core::System& system_)
        : ServiceFramework{system_, "nx2:auddmg:u:manager"} {
        static const FunctionInfo functions[] = {
            {0, &INx2AudioVirtualDeviceManager::ListVirtualOutputDevices,
             "ListVirtualOutputDevices"},
            {100, &INx2AudioVirtualDeviceManager::GetDefaultVirtualOutputDevice,
             "GetDefaultVirtualOutputDevice"},
            {300, &INx2AudioVirtualDeviceManager::UnsupportedStream,
             "IsVirtualOutputDeviceStreamFormatSupported (not available)"},
            {401, &INx2AudioVirtualDeviceManager::GetVirtualOutputDeviceProperties,
             "GetVirtualOutputDeviceProperties"},
            {700, &INx2AudioVirtualDeviceManager::UnsupportedStream,
             "AttachStream (not available)"},
            {800, &INx2AudioVirtualDeviceManager::UnsupportedStream,
             "DetachStream (not available)"},
        };
        RegisterHandlers(functions);
    }

private:
    static constexpr std::size_t NameSize = 0x100;
    static constexpr std::string_view DeviceName = "AudioVirtualOutput0";
    static constexpr std::string_view PhysicalDeviceName = "AudioTvOutput";

    static void CopyName(u8* to, std::string_view text) {
        std::memcpy(to, text.data(), (std::min)(text.size(), NameSize - 1));
    }

    void ListVirtualOutputDevices(HLERequestContext& ctx) {
        const bool has_room = ctx.GetWriteBufferSize() >= NameSize;
        if (has_room) {
            std::array<u8, NameSize> names{};
            CopyName(names.data(), DeviceName);
            ctx.WriteBuffer(names);
        }
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(has_room ? 1U : 0U);
    }

    void GetDefaultVirtualOutputDevice(HLERequestContext& ctx) {
        if (ctx.GetWriteBufferSize() < NameSize) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultInsufficientBuffer);
            return;
        }
        std::array<u8, NameSize> name{};
        CopyName(name.data(), DeviceName);
        ctx.WriteBuffer(name);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetVirtualOutputDeviceProperties(HLERequestContext& ctx) {
        const auto name = ctx.ReadBuffer(0);
        // Do not manufacture hardware properties for a non-existent device,
        // or write beyond the guest's declared output map-alias buffer.
        if (name.size() < DeviceName.size() ||
            !std::equal(DeviceName.begin(), DeviceName.end(), name.begin()) ||
            (name.size() > DeviceName.size() && name[DeviceName.size()] != 0)) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultNotFound);
            return;
        }
        if (ctx.GetWriteBufferSize() < 0x228) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultInsufficientBuffer);
            return;
        }
        std::array<u8, 0x228> block{};
        CopyName(block.data(), DeviceName);
        const auto put32 = [&](std::size_t at, u32 value) {
            std::memcpy(block.data() + at, &value, sizeof(value));
        };
        put32(0x100, 3);      // virtual output
        put32(0x104, 2);      // output kind
        put32(0x108, 2);      // native stereo PCM
        put32(0x10c, 48000);  // sink sample rate
        CopyName(block.data() + 0x110, PhysicalDeviceName);
        // Do not claim a measured device latency: no sysmix host stream is
        // created yet, so 0x218 stays zero until a real backend owns it.
        put32(0x220, 1);      // virtual device index, NOT a live stream ID
        ctx.WriteBuffer(block);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void UnsupportedStream(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNotSupported);
    }
};

class INx2AudioDeviceManagerService final
    : public ServiceFramework<INx2AudioDeviceManagerService> {
public:
    explicit INx2AudioDeviceManagerService(Core::System& system_)
        : ServiceFramework{system_, "nx2:auddmg:u"} {
        static const FunctionInfo functions[] = {
            {0, &INx2AudioDeviceManagerService::OpenVirtualDeviceManager,
             "OpenVirtualDeviceManager"},
        };
        RegisterHandlers(functions);
    }

private:
    void OpenVirtualDeviceManager(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<INx2AudioVirtualDeviceManager>(ctx, system);
    }
};

} // namespace Service::Audio
