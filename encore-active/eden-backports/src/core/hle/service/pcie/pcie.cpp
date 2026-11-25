// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/pcie/pcie.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/ipc_helpers.h"
#include "frontend_common/firmware_manager.h"

namespace Service::PCIe {

class ISession final : public ServiceFramework<ISession> {
public:
    explicit ISession(Core::System& system_) : ServiceFramework{system_, "ISession"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ISession::RejectUnavailableSwitchBackend, "QueryFunctions"},
            {1, &ISession::RejectUnavailableSwitchBackend, "AcquireFunction"},
            {2, &ISession::RejectUnavailableSwitchBackend, "ReleaseFunction"},
            {3, &ISession::RejectUnavailableSwitchBackend, "GetFunctionState"},
            {4, &ISession::RejectUnavailableSwitchBackend, "GetBarProfile"},
            {5, &ISession::RejectUnavailableSwitchBackend, "ReadConfig"},
            {6, &ISession::RejectUnavailableSwitchBackend, "WriteConfig"},
            {7, &ISession::RejectUnavailableSwitchBackend, "ReadBarRegion"},
            {8, &ISession::RejectUnavailableSwitchBackend, "WriteBarRegion"},
            {9, &ISession::RejectUnavailableSwitchBackend, "FindCapability"},
            {10, &ISession::RejectUnavailableSwitchBackend, "FindExtendedCapability"},
            {11, &ISession::RejectUnavailableSwitchBackend, "MapDma"},
            {12, &ISession::RejectUnavailableSwitchBackend, "UnmapDma"},
            {13, &ISession::RejectUnavailableSwitchBackend, "UnmapDmaBusAddress"},
            {14, &ISession::RejectUnavailableSwitchBackend, "GetDmaBusAddress"},
            {15, &ISession::RejectUnavailableSwitchBackend, "GetDmaBusAddressRange"},
            {16, &ISession::RejectUnavailableSwitchBackend, "SetDmaEnable"},
            {17, &ISession::RejectUnavailableSwitchBackend, "AcquireIrq"},
            {18, &ISession::RejectUnavailableSwitchBackend, "ReleaseIrq"},
            {19, &ISession::RejectUnavailableSwitchBackend, "SetIrqEnable"},
            {20, &ISession::RejectUnavailableSwitchBackend, "GetIrqEvent"},
            {21, &ISession::RejectUnavailableSwitchBackend, "SetAspmEnable"},
            {22, &ISession::RejectUnavailableSwitchBackend, "SetResetUponResumeEnable"},
            {23, &ISession::RejectUnavailableSwitchBackend, "ResetFunction"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableSwitchBackend(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

class PCIE final : public ServiceFramework<PCIE> {
public:
    explicit PCIE(Core::System& system_) : ServiceFramework{system_, "pcie"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &PCIE::RejectUnavailableSwitchBackend, "RegisterClassDriver"},
            {1, &PCIE::RejectUnavailableSwitchBackend, "QueryFunctionsUnregistered"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableSwitchBackend(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

class PCIE_LOG final : public ServiceFramework<PCIE_LOG> {
public:
    explicit PCIE_LOG(Core::System& system_) : ServiceFramework{system_, "pcie:log"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &PCIE_LOG::RejectUnavailableSwitchBackend, "GetLoggedState"},
            {1, &PCIE_LOG::RejectUnavailableSwitchBackend, "GetLoggedStateEvent"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableSwitchBackend(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("pcie", std::make_shared<PCIE>(system));
    // +6.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 6) {
        server_manager->RegisterNamedService("pcie:log", std::make_shared<PCIE_LOG>(system));
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PCIe
