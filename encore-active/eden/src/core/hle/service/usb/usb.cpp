// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/usb/usb.h"
#include "frontend_common/firmware_manager.h"

namespace Service::USB {

class IDsInterface final : public ServiceFramework<IDsInterface> {
public:
    explicit IDsInterface(Core::System& system_) : ServiceFramework{system_, "IDsInterface"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IDsInterface::RejectUnavailableUsbHardware, "AddEndpoint"},
            {1, &IDsInterface::RejectUnavailableUsbHardware, "GetSetupEvent"},
            {2, &IDsInterface::RejectUnavailableUsbHardware, "GetSetupPacket"},
            {3, &IDsInterface::RejectUnavailableUsbHardware, "Enable"},
            {4, &IDsInterface::RejectUnavailableUsbHardware, "Disable"},
            {5, &IDsInterface::RejectUnavailableUsbHardware, "CtrlIn"},
            {6, &IDsInterface::RejectUnavailableUsbHardware, "CtrlOut"},
            {7, &IDsInterface::RejectUnavailableUsbHardware, "GetCtrlInCompletionEvent"},
            {8, &IDsInterface::RejectUnavailableUsbHardware, "GetCtrlInUrbReport"},
            {9, &IDsInterface::RejectUnavailableUsbHardware, "GetCtrlOutCompletionEvent"},
            {10, &IDsInterface::RejectUnavailableUsbHardware, "GetCtrlOutUrbReport"},
            {11, &IDsInterface::RejectUnavailableUsbHardware, "CtrlStall"},
            {12, &IDsInterface::RejectUnavailableUsbHardware, "AppendConfigurationData"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IDsInterface USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IDsRootSession final : public ServiceFramework<IDsRootSession> {
public:
    explicit IDsRootSession(Core::System& system_) : ServiceFramework{system_, "usb:ds"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IDsRootSession::RejectUnavailableUsbHardware, "OpenDsService"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IDsRootSession USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IClientEpSession final : public ServiceFramework<IClientEpSession> {
public:
    explicit IClientEpSession(Core::System& system_)
        : ServiceFramework{system_, "IClientEpSession"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IClientEpSession::RejectUnavailableUsbHardware, "ReOpen"},
            {1, &IClientEpSession::RejectUnavailableUsbHardware, "Close"},
            {2, &IClientEpSession::RejectUnavailableUsbHardware, "GetCompletionEvent"},
            {3, &IClientEpSession::RejectUnavailableUsbHardware, "PopulateRing"},
            {4, &IClientEpSession::RejectUnavailableUsbHardware, "PostBufferAsync"},
            {5, &IClientEpSession::RejectUnavailableUsbHardware, "GetXferReport"},
            {6, &IClientEpSession::RejectUnavailableUsbHardware, "PostBufferMultiAsync"},
            {7, &IClientEpSession::RejectUnavailableUsbHardware, "CreateSmmuSpace"},
            {8, &IClientEpSession::RejectUnavailableUsbHardware, "ShareReportRing"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IClientEpSession USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IClientIfSession final : public ServiceFramework<IClientIfSession> {
public:
    explicit IClientIfSession(Core::System& system_)
        : ServiceFramework{system_, "IClientIfSession"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IClientIfSession::RejectUnavailableUsbHardware, "GetStateChangeEvent"},
            {1, &IClientIfSession::RejectUnavailableUsbHardware, "SetInterface"},
            {2, &IClientIfSession::RejectUnavailableUsbHardware, "GetInterface"},
            {3, &IClientIfSession::RejectUnavailableUsbHardware, "GetAlternateInterface"},
            {4, &IClientIfSession::RejectUnavailableUsbHardware, "GetCurrentFrame"},
            {5, &IClientIfSession::RejectUnavailableUsbHardware, "CtrlXferAsync"},
            {6, &IClientIfSession::RejectUnavailableUsbHardware, "GetCtrlXferCompletionEvent"},
            {7, &IClientIfSession::RejectUnavailableUsbHardware, "GetCtrlXferReport"},
            {8, &IClientIfSession::RejectUnavailableUsbHardware, "ResetDevice"},
            {9, &IClientIfSession::RejectUnavailableUsbHardware, "OpenUsbEp"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IClientIfSession USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IClientRootSession final : public ServiceFramework<IClientRootSession> {
public:
    explicit IClientRootSession(Core::System& system_) : ServiceFramework{system_, "usb:hs"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IClientRootSession::RejectUnavailableUsbHardware, "BindClientProcess"},
            {1, &IClientRootSession::RejectUnavailableUsbHardware, "QueryAllInterfaces"},
            {2, &IClientRootSession::RejectUnavailableUsbHardware, "QueryAvailableInterfaces"},
            {3, &IClientRootSession::RejectUnavailableUsbHardware, "QueryAcquiredInterfaces"},
            {4, &IClientRootSession::RejectUnavailableUsbHardware, "CreateInterfaceAvailableEvent"},
            {5, &IClientRootSession::RejectUnavailableUsbHardware, "DestroyInterfaceAvailableEvent"},
            {6, &IClientRootSession::RejectUnavailableUsbHardware, "GetInterfaceStateChangeEvent"},
            {7, &IClientRootSession::RejectUnavailableUsbHardware, "AcquireUsbIf"},
            {8, &IClientRootSession::RejectUnavailableUsbHardware, "SetTestMode"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IClientRootSession USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IPdSession final : public ServiceFramework<IPdSession> {
public:
    explicit IPdSession(Core::System& system_) : ServiceFramework{system_, "IPdSession"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IPdSession::RejectUnavailableUsbHardware, "BindNoticeEvent"},
            {1, &IPdSession::RejectUnavailableUsbHardware, "UnbindNoticeEvent"},
            {2, &IPdSession::RejectUnavailableUsbHardware, "GetStatus"},
            {3, &IPdSession::RejectUnavailableUsbHardware, "GetNotice"},
            {4, &IPdSession::RejectUnavailableUsbHardware, "EnablePowerRequestNotice"},
            {5, &IPdSession::RejectUnavailableUsbHardware, "DisablePowerRequestNotice"},
            {6, &IPdSession::RejectUnavailableUsbHardware, "ReplyPowerRequest"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IPdSession USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IPdManager final : public ServiceFramework<IPdManager> {
public:
    explicit IPdManager(Core::System& system_) : ServiceFramework{system_, "usb:pd"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IPdManager::OpenSession, "OpenSession"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void OpenSession(HLERequestContext& ctx) {
        LOG_DEBUG(Service_USB, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IPdSession>(ctx, system);
    }
};

class IPdCradleSession final : public ServiceFramework<IPdCradleSession> {
public:
    explicit IPdCradleSession(Core::System& system_)
        : ServiceFramework{system_, "IPdCradleSession"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IPdCradleSession::RejectUnavailableUsbHardware, "SetCradleVdo"},
            {1, &IPdCradleSession::RejectUnavailableUsbHardware, "GetCradleVdo"},
            {2, &IPdCradleSession::RejectUnavailableUsbHardware, "ResetCradleUsbHub"},
            {3, &IPdCradleSession::RejectUnavailableUsbHardware, "GetHostPdcFirmwareType"},
            {4, &IPdCradleSession::RejectUnavailableUsbHardware, "GetHostPdcFirmwareRevision"},
            {5, &IPdCradleSession::RejectUnavailableUsbHardware, "GetHostPdcManufactureId"},
            {6, &IPdCradleSession::RejectUnavailableUsbHardware, "GetHostPdcDeviceId"},
            {7, &IPdCradleSession::RejectUnavailableUsbHardware, "EnableCradleRecovery"},
            {8, &IPdCradleSession::RejectUnavailableUsbHardware, "DisableCradleRecovery"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IPdCradleSession USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IPdCradleManager final : public ServiceFramework<IPdCradleManager> {
public:
    explicit IPdCradleManager(Core::System& system_) : ServiceFramework{system_, "usb:pd:c"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IPdCradleManager::OpenCradleSession, "OpenCradleSession"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void OpenCradleSession(HLERequestContext& ctx) {
        LOG_DEBUG(Service_USB, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IPdCradleSession>(ctx, system);
    }
};

class IPmMainService final : public ServiceFramework<IPmMainService> {
public:
    explicit IPmMainService(Core::System& system_) : ServiceFramework{system_, "usb:pm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IPmMainService::RejectUnavailableUsbHardware, "GetPowerEvent"},
            {1, &IPmMainService::RejectUnavailableUsbHardware, "GetPowerState"},
            {2, &IPmMainService::RejectUnavailableUsbHardware, "GetDataEvent"},
            {3, &IPmMainService::RejectUnavailableUsbHardware, "GetDataRole"},
            {4, &IPmMainService::RejectUnavailableUsbHardware, "SetDiagData"},
            {5, &IPmMainService::RejectUnavailableUsbHardware, "GetDiagData"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IPmMainService USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IPdManufactureManager final : public ServiceFramework<IPdManufactureManager> {
public:
    explicit IPdManufactureManager(Core::System& system_) : ServiceFramework{system_, "usb:pd:m"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IPdManufactureManager::RejectUnavailableUsbHardware, "OpenManufactureSession"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IPdManufactureManager USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IQdbManager final : public ServiceFramework<IQdbManager> {
public:
    explicit IQdbManager(Core::System& system_) : ServiceFramework{system_, "usb:qdb"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IQdbManager::RejectUnavailableUsbHardware, "ImportQuirkDevices"},
            {1, &IQdbManager::RejectUnavailableUsbHardware, "HasQuirk"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IQdbManager USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IPmObserverService final : public ServiceFramework<IPmObserverService> {
public:
    explicit IPmObserverService(Core::System& system_) : ServiceFramework{system_, "usb:obsv"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IPmObserverService::RejectUnavailableUsbHardware, "GetTopologyChangeEvent"},
            {1, &IPmObserverService::RejectUnavailableUsbHardware, "GetFlattenedTopology"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    void RejectUnavailableUsbHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_USB, "Rejected IPmObserverService USB hardware command: no guest USB backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("usb:ds", std::make_shared<IDsRootSession>(system));
    server_manager->RegisterNamedService("usb:hs", std::make_shared<IClientRootSession>(system));
    server_manager->RegisterNamedService("usb:pd", std::make_shared<IPdManager>(system), 6);
    server_manager->RegisterNamedService("usb:pd:c", std::make_shared<IPdCradleManager>(system), 4);
    server_manager->RegisterNamedService("usb:pd:m", std::make_shared<IPdManufactureManager>(system));
    server_manager->RegisterNamedService("usb:pm", std::make_shared<IPmMainService>(system), 5);
    // +7.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 7) {
        server_manager->RegisterNamedService("usb:qdb", std::make_shared<IQdbManager>(system));
    }
    // +8.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 8) {
        server_manager->RegisterNamedService("usb:obsv", std::make_shared<IPmObserverService>(system), 2);
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::USB
