// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/hid/hid_debug_server.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/hid/hid_system_server.h"
#include "core/hle/service/hid/hidbus.h"
#include "core/hle/service/hid/irs.h"
#include "core/hle/service/hid/xcd.h"
#include "core/hle/service/server_manager.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/hid_firmware_settings.h"
#include "frontend_common/firmware_manager.h"

namespace Service::HID {

class IHidTemporaryServer final : public ServiceFramework<IHidTemporaryServer> {
public:
    explicit IHidTemporaryServer(Core::System& system_)
    : ServiceFramework{system_, "hid:tmp"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IHidTemporaryServer::RejectUnavailableLegacyCommand, "GetConsoleSixAxisSensorCalibrationValues"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
    ~IHidTemporaryServer() override = default;

private:
    void RejectUnavailableLegacyCommand(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }
};

class AHID_CD final : public ServiceFramework<AHID_CD> {
public:
    explicit AHID_CD(Core::System& system_)
    : ServiceFramework{system_, "ahid:cd"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &AHID_CD::RejectUnavailableLegacyCommand, "AcquireDevice"},
            {1, &AHID_CD::RejectUnavailableLegacyCommand, "ReleaseDevice"},
            {2, &AHID_CD::RejectUnavailableLegacyCommand, "GetCtrlSession"},
            {3, &AHID_CD::RejectUnavailableLegacyCommand, "GetReadSession"},
            {4, &AHID_CD::RejectUnavailableLegacyCommand, "GetWriteSession"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
    ~AHID_CD() override = default;

private:
    void RejectUnavailableLegacyCommand(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }
};

class AHID_HDR final : public ServiceFramework<AHID_HDR> {
public:
    explicit AHID_HDR(Core::System& system_)
    : ServiceFramework{system_, "ahid:hdr"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &AHID_HDR::RejectUnavailableLegacyCommand, "GetDeviceEntries"},
            {1, &AHID_HDR::RejectUnavailableLegacyCommand, "GetDeviceList"},
            {2, &AHID_HDR::RejectUnavailableLegacyCommand, "GetDeviceParameters"},
            {3, &AHID_HDR::RejectUnavailableLegacyCommand, "AttachDevice"},
            {4, &AHID_HDR::RejectUnavailableLegacyCommand, "DetachDevice"},
            {5, &AHID_HDR::RejectUnavailableLegacyCommand, "SetDeviceFilter"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
    ~AHID_HDR() override = default;

private:
    void RejectUnavailableLegacyCommand(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto firmware_settings = std::make_shared<HidFirmwareSettings>(system);
    auto resource_manager = std::make_shared<ResourceManager>(system, firmware_settings);

    resource_manager->Initialize();

    server_manager->RegisterNamedService("hid", std::make_shared<IHidServer>(system, resource_manager, firmware_settings));
    server_manager->RegisterNamedService("hid:dbg", std::make_shared<IHidDebugServer>(system, resource_manager, firmware_settings));
    server_manager->RegisterNamedService("hid:sys", std::make_shared<IHidSystemServer>(system, resource_manager, firmware_settings));
    const auto firmware_major = FirmwareManager::GetFirmwareVersion(system).first.major;
    // hid:tmp exists only on HOS 3.0.0-7.0.1.
    if (firmware_major >= 3 && firmware_major < 8) {
        server_manager->RegisterNamedService("hid:tmp", std::make_shared<IHidTemporaryServer>(system));
    }

    server_manager->RegisterNamedService("hidbus", std::make_shared<Hidbus>(system));

    server_manager->RegisterNamedService("irs", std::make_shared<IRS::IRS>(system));
    server_manager->RegisterNamedService(
        "irs:sys", std::make_shared<IRS::IRS_SYS>(system, resource_manager));


    server_manager->RegisterNamedService("xcd:sys", std::make_shared<XCD_SYS>(system));

    system.RunServer(std::move(server_manager));
}

} // namespace Service::HID
