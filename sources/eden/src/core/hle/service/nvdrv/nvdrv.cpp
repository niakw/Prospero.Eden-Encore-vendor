// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: 2021 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#include <fmt/ranges.h>
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_ctrl.h"
#include "core/hle/service/nvdrv/devices/nvhost_ctrl_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_nvdec.h"
#include "core/hle/service/nvdrv/devices/nvhost_nvdec_common.h"
#include "core/hle/service/nvdrv/devices/nvhost_nvjpg.h"
#include "core/hle/service/nvdrv/devices/nvhost_vic.h"
#include "core/hle/service/nvdrv/devices/nvmap.h"
#include "core/hle/service/nvdrv/devices/nx2_stub_device.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvdrv/nvmemp.h"
#include "core/hle/service/nvnflinger/nvnflinger.h"
#include "core/hle/service/server_manager.h"
#include "frontend_common/firmware_manager.h"
#include "video_core/gpu.h"

namespace Service::Nvidia {

EventInterface::EventInterface(Module& module_) : module{module_}, guard{}, on_signal{} {}

EventInterface::~EventInterface() = default;

Kernel::KEvent* EventInterface::CreateEvent(std::string name) {
    Kernel::KEvent* new_event = module.service_context.CreateEvent(std::move(name));
    return new_event;
}

void EventInterface::FreeEvent(Kernel::KEvent* event) {
    module.service_context.CloseEvent(event);
}

class NVGEM_C final : public ServiceFramework<NVGEM_C> {
public:
    explicit NVGEM_C(Core::System& system_)
        : ServiceFramework{system_, "nvgem:c"}
    {
        static const FunctionInfo functions[] = {
            {0, nullptr, "Initialize"},
            {1, nullptr, "GetEventHandle"},
            {2, nullptr, "ControlNotification"},
            {3, nullptr, "SetNotificationPerm"},
            {4, nullptr, "SetCoreDumpPerm"},
            {5, nullptr, "GetAruid"},
            {6, nullptr, "Reset"},
            {7, nullptr, "GetAruid2"},
        };
        RegisterHandlers(functions);
    }
};

class NVGEM_CD final : public ServiceFramework<NVGEM_CD> {
public:
    explicit NVGEM_CD(Core::System& system_)
        : ServiceFramework{system_, "nvgem:cd"}
    {
        static const FunctionInfo functions[] = {
            {0, nullptr, "Initialize"},
            {1, nullptr, "GetAruid"},
            {2, nullptr, "ReadNextBlock"},
            {3, nullptr, "GetNextBlockSize"},
            {4, nullptr, "ReadNextBlock2"},
        };
        RegisterHandlers(functions);
    }
};

class NVDBG_D final : public ServiceFramework<NVDBG_D> {
public:
    explicit NVDBG_D(Core::System& system_)
        : ServiceFramework{system_, "nvdbg:d"}
    {
        static const FunctionInfo functions[] = {
            {0, nullptr, "Open"},
            {1, nullptr, "Ioctl"},
            {2, nullptr, "Close"},
            {4, nullptr, "QueryEvent"},
            {9, nullptr, "DumpStatus"},
            {10, nullptr, "InitializeDevtools"},
            {11, nullptr, "Ioctl2"},
            {12, nullptr, "Ioctl3"},
            {13, nullptr, "SetConfiguration"},
        };
        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto module = std::make_shared<Module>(system);
    server_manager->RegisterNamedService("nvdrv", std::make_shared<NVDRV>(system, module, "nvdrv"));

    const auto NvdrvInterfaceFactoryForApplets = [&, module] {
        return std::make_shared<NVDRV>(system, module, "nvdrv:a");
    };

    server_manager->RegisterNamedService("nvdrv:a", NvdrvInterfaceFactoryForApplets);
    server_manager->RegisterNamedService("nvdrv:s", std::make_shared<NVDRV>(system, module, "nvdrv:s"));
    server_manager->RegisterNamedService("nvdrv:t", std::make_shared<NVDRV>(system, module, "nvdrv:t"));
    server_manager->RegisterNamedService("nvmemp", std::make_shared<NVMEMP>(system));
    server_manager->RegisterNamedService("nvgem:c", std::make_shared<NVGEM_C>(system));
    server_manager->RegisterNamedService("nvgem:cd", std::make_shared<NVGEM_CD>(system));
    // +10.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 10) {
        server_manager->RegisterNamedService("nvdbg:d", std::make_shared<NVDBG_D>(system));
    }
    ServerManager::RunServer(std::move(server_manager));
}

Module::Module(Core::System& system)
    : container{system.Host1x()}, service_context{system, "nvdrv"}, events_interface{*this} {
    nx2_stub_builder = [this, &system](DeviceFD fd, const std::string& name) {
        auto device = std::make_shared<Devices::nx2_stub_device>(
            system, name,
            [this](NvCore::SessionId owner, DeviceFD channel_fd) {
                const auto it = open_files.find(channel_fd);
                return it != open_files.end() &&
                       it->second->CanBindNativeTsgChannel(channel_fd, owner);
            },
            [this](NvCore::SessionId owner, DeviceFD channel_fd, bool enabled) {
                const auto it = open_files.find(channel_fd);
                if (it == open_files.end()) return NvResult::InvalidState;
                return it->second->SetNativeTsgChannelEnabled(
                    channel_fd, owner, enabled);
            });
        open_files.emplace(fd, device);
        return std::static_pointer_cast<Devices::nvdevice>(device);
    };
    builders["/dev/nvhost-as-gpu"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_as_gpu>(system, *this, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-gpu"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_gpu>(system, events_interface, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-ctrl-gpu"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_ctrl_gpu>(system, events_interface);
        // The native owner queries this Container to distinguish the exact
        // Switch 2 KProcess on its FD; legacy controller C++ ignores it.
        device->SetNativeContainer(container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvmap"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvmap>(system, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvdisp_disp0"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvdisp_disp0>(system, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-ctrl"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_ctrl>(system, events_interface, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-nvdec"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_nvdec>(system, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-nvjpg"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_nvjpg>(system);
        return open_files.emplace(fd, std::move(device)).first;
    };
    builders["/dev/nvhost-vic"] = [this, &system](DeviceFD fd) {
        auto device = std::make_shared<Devices::nvhost_vic>(system, container);
        return open_files.emplace(fd, std::move(device)).first;
    };
}

Module::~Module() {
    // FD devices borrow EventInterface, ServiceContext and Container.
    // Implicit member destruction would destroy the first two before
    // open_files, leaving e.g. nvhost_gpu and nvhost_ctrl_gpu to close
    // their kernel events through dead service owners during relaunch.
    // Service loops are joined before the last Module owner is retired.
    // Detach each descriptor before its destructor can run reentrant
    // cleanup, without allocating or calling guest ioctl close again.
    while (!open_files.empty()) {
        auto it = open_files.begin();
        auto device = std::move(it->second);
        open_files.erase(it);
        device.reset();
    }
}

NvResult Module::VerifyFD(DeviceFD fd) const {
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    if (open_files.find(fd) == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    return NvResult::Success;
}

DeviceFD Module::Open(const std::string& device_name, NvCore::SessionId session_id) {
    auto it = builders.find(device_name);
    if (it == builders.end() && device_name.starts_with("/dev/nvhost-")) {
        // Lichii 5cb4786: Switch 2 NvRm opens additional nvgpu nodes such as
        // /dev/nvhost-tsg-gpu. Failing the open aborts NVN device init before
        // NativeWindow::Acquire. Never shadow a known device.
        LOG_WARNING(Service_NVDRV, "Opening unknown device {} as a Switch 2 stub", device_name);
        const DeviceFD fd = next_fd++;
        auto device = nx2_stub_builder(fd, device_name);
        try {
            device->OnOpen(session_id, fd);
        } catch (...) {
            // The builder publishes the device before OnOpen. Roll it back if
            // session/process setup throws so a failed launch cannot retain an
            // unreachable NVDRV device in the shared module.
            open_files.erase(fd);
            throw;
        }
        return fd;
    }
    if (it == builders.end()) {
        LOG_ERROR(Service_NVDRV, "Trying to open unknown device {}", device_name);
        return INVALID_NVDRV_FD;
    }

    const DeviceFD fd = next_fd++;
    auto& builder = it->second;
    auto device = builder(fd)->second;

    try {
        device->OnOpen(session_id, fd);
    } catch (...) {
        // Known builders also publish before device-specific OnOpen. Preserve
        // the map's strong ownership invariant when an allocation or process
        // lease fails during launch.
        open_files.erase(fd);
        throw;
    }

    return fd;
}

NvResult Module::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                        std::span<u8> output) {
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    return itr->second->Ioctl1(fd, command, input, output);
}

NvResult Module::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                        std::span<const u8> inline_input, std::span<u8> output) {
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    return itr->second->Ioctl2(fd, command, input, inline_input, output);
}

NvResult Module::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input, std::span<u8> output,
                        std::span<u8> inline_output) {
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    return itr->second->Ioctl3(fd, command, input, output, inline_output);
}

NvResult Module::Close(DeviceFD fd) {
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    try {
        itr->second->OnClose(fd);
    } catch (...) {
        // Never leave a published FD behind if device-specific teardown
        // reports a fatal host error. The exception still propagates, but the
        // shared module no longer retains the failed device into a relaunch.
        open_files.erase(itr);
        throw;
    }

    open_files.erase(itr);

    return NvResult::Success;
}

NvResult Module::QueryEvent(DeviceFD fd, u32 event_id, Kernel::KEvent*& event) {
    if (fd < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid DeviceFD={}!", fd);
        return NvResult::InvalidState;
    }

    const auto itr = open_files.find(fd);

    if (itr == open_files.end()) {
        LOG_ERROR(Service_NVDRV, "Could not find DeviceFD={}!", fd);
        return NvResult::NotImplemented;
    }

    // Preserve the descriptor all the way to devices that have per-FD
    // event admission (native Switch 2 GPU channels). Legacy devices inherit
    // NvDevice's delegating overload and keep their existing event behavior.
    event = itr->second->QueryEvent(fd, event_id);
    if (!event) {
        return NvResult::BadParameter;
    }
    return NvResult::Success;
}

} // namespace Service::Nvidia
