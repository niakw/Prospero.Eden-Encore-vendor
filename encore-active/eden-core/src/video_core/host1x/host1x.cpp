// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <mutex>

#include "core/core.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/nvdec.h"
#include "video_core/host1x/vic.h"

namespace Tegra::Host1x {

namespace {
// Protect the device variant and its CDMA enqueue/teardown per guest FD.
// Shutdown() may join a worker: a single global lock would stall independent
// NVDEC/VIC channels for the entire duration of that join. Keep the lock table
// outside Host1x so native class layout and the selected headers remain intact.
std::array<std::mutex, 1024> device_dispatch_mutexes;
}

Host1x::Host1x(Core::System& system_)
    : system{system_}
    , syncpoint_manager{}
    , memory_manager(system.DeviceMemory())
    , gmmu_manager{system, memory_manager, 32, 0, 12}
    , allocator{1 << 12}
{}

Host1x::~Host1x() {
    // A whole Host1x teardown destroys all device variants after this
    // destructor body. Quiesce every CDMA worker while NVDEC/VIC state,
    // GPU memory and the frame queue are still constructed.
    for (auto& device : devices) {
        if (auto* nvdec = std::get_if<Tegra::Host1x::Nvdec>(&device)) {
            nvdec->Shutdown();
        } else if (auto* vic = std::get_if<Tegra::Host1x::Vic>(&device)) {
            vic->Shutdown();
        }
    }
}

void Host1x::StartDevice(s32 fd, ChannelType type, u32 syncpt) {
    // DeviceFD originates in NVDRV guest-facing handlers. The Host1x
    // device table is a fixed-size array, not an fd-indexed map.
    if (fd < 0 || static_cast<std::size_t>(fd) >= devices.size()) {
        LOG_ERROR(HW_GPU, "Reject invalid Host1x StartDevice FD {}", fd);
        return;
    }
    // Do not destroy a variant while another service thread is enqueuing
    // onto its CDMA worker. The lock spans Shutdown + variant replacement.
    std::scoped_lock lk{device_dispatch_mutexes[static_cast<std::size_t>(fd)]};
    // A repeated Open on a recycled FD also destroys the old variant.
    // Quiesce it first so its jthread cannot access freed decoder state.
    if (type == ChannelType::NvDec || type == ChannelType::VIC) {
        if (auto* nvdec = std::get_if<Tegra::Host1x::Nvdec>(&devices[fd])) {
            nvdec->Shutdown();
        } else if (auto* vic = std::get_if<Tegra::Host1x::Vic>(&devices[fd])) {
            vic->Shutdown();
        }
    }
    switch (type) {
    case ChannelType::NvDec:
#ifdef YUZU_LEGACY
        std::call_once(nvdec_first_init, []() {std::this_thread::sleep_for(std::chrono::milliseconds{500});}); // HACK: For Astroneer
#endif
        devices[fd].emplace<Tegra::Host1x::Nvdec>(*this, fd, syncpt);
        break;
    case ChannelType::VIC:
#ifdef YUZU_LEGACY
        std::call_once(vic_first_init, []() {std::this_thread::sleep_for(std::chrono::milliseconds{500});}); // HACK: For Astroneer
#endif
        devices[fd].emplace<Tegra::Host1x::Vic>(*this, fd, syncpt);
        break;
    default:
        LOG_ERROR(HW_GPU, "Unimplemented host1x device {}", u32(type));
        break;
    }
}

void Host1x::StopDevice(s32 fd, ChannelType type) {
    if (fd < 0 || static_cast<std::size_t>(fd) >= devices.size()) {
        LOG_ERROR(HW_GPU, "Reject invalid Host1x StopDevice FD {}", fd);
        return;
    }
    // Block guest submissions until the worker is quiescent and the
    // device variant is removed.
    std::scoped_lock lk{device_dispatch_mutexes[static_cast<std::size_t>(fd)]};
    auto& device = devices[static_cast<std::size_t>(fd)];
    if (auto* nvdec = std::get_if<Tegra::Host1x::Nvdec>(&device)) {
        nvdec->Shutdown();
    } else if (auto* vic = std::get_if<Tegra::Host1x::Vic>(&device)) {
        vic->Shutdown();
    }
    device.emplace<std::monostate>();
}

void Host1x::PushEntries(s32 fd, ChCommandHeaderList&& entries) {
    // Signed DeviceFD is supplied by NVDRV. Keep the original bounds
    // guard, but protect the variant lookup AND enqueue against a
    // simultaneous StopDevice / StartDevice on the same descriptor.
    if (fd < 0 || static_cast<std::size_t>(fd) >= devices.size()) {
        return;
    }
    std::scoped_lock lk{device_dispatch_mutexes[static_cast<std::size_t>(fd)]};
    auto& device = devices[static_cast<std::size_t>(fd)];
    if (auto* nvdec = std::get_if<Tegra::Host1x::Nvdec>(&device)) {
        nvdec->PushEntries(std::move(entries));
    } else if (auto* vic = std::get_if<Tegra::Host1x::Vic>(&device)) {
        vic->PushEntries(std::move(entries));
    }
}

} // namespace Tegra::Host1x
