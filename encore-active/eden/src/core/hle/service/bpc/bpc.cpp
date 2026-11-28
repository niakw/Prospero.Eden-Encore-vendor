// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/result.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/bpc/bpc.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::BPC {

class BPC final : public ServiceFramework<BPC> {
public:
    explicit BPC(Core::System& system_) : ServiceFramework{system_, "bpc"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &BPC::RejectUnavailablePowerHardware, "ShutdownSystem"},
            {1, &BPC::RejectUnavailablePowerHardware, "RebootSystem"},
            {2, &BPC::RejectUnavailablePowerHardware, "GetWakeupReason"},
            {3, &BPC::RejectUnavailablePowerHardware, "GetShutdownReason"},
            {4, &BPC::RejectUnavailablePowerHardware, "GetAcOk"},
            {5, &BPC::RejectUnavailablePowerHardware, "GetBoardPowerControlEvent"},
            {6, &BPC::RejectUnavailablePowerHardware, "GetSleepButtonState"},
            {7, &BPC::RejectUnavailablePowerHardware, "GetPowerEvent"},
            {8, &BPC::RejectUnavailablePowerHardware, "CreateWakeupTimer"},
            {9, &BPC::RejectUnavailablePowerHardware, "CancelWakeupTimer"},
            {10, &BPC::RejectUnavailablePowerHardware, "EnableWakeupTimerOnDevice"},
            {11, &BPC::RejectUnavailablePowerHardware, "CreateWakeupTimerEx"},
            {12, &BPC::RejectUnavailablePowerHardware, "GetLastEnabledWakeupTimerType"},
            {13, &BPC::RejectUnavailablePowerHardware, "CleanAllWakeupTimers"},
            {14, &BPC::RejectUnavailablePowerHardware, "GetPowerButton"},
            {15, &BPC::RejectUnavailablePowerHardware, "SetEnableWakeupTimer"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailablePowerHardware(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class BPC_R final : public ServiceFramework<BPC_R> {
public:
    explicit BPC_R(Core::System& system_) : ServiceFramework{system_, "bpc:r"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &BPC_R::RejectUnavailablePowerHardware, "GetRtcTime"},
            {1, &BPC_R::RejectUnavailablePowerHardware, "SetRtcTime"},
            {2, &BPC_R::RejectUnavailablePowerHardware, "GetRtcResetDetected"},
            {3, &BPC_R::RejectUnavailablePowerHardware, "ClearRtcResetDetected"},
            {4, &BPC_R::RejectUnavailablePowerHardware, "SetUpRtcResetOnShutdown"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailablePowerHardware(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class BPC_C final : public ServiceFramework<BPC_C> {
public:
    explicit BPC_C(Core::System& system_) : ServiceFramework{system_, "bpc:c"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &BPC_C::RejectUnavailablePowerHardware, "ShutdownSystem"},
            {1, &BPC_C::RejectUnavailablePowerHardware, "RebootSystem"},
            {2, &BPC_C::RejectUnavailablePowerHardware, "GetWakeupReason"},
            {3, &BPC_C::RejectUnavailablePowerHardware, "GetShutdownReason"},
            {4, &BPC_C::RejectUnavailablePowerHardware, "GetAcOk"},
            {5, &BPC_C::RejectUnavailablePowerHardware, "GetPowerEvent"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    void RejectUnavailablePowerHardware(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class BPC_B final : public ServiceFramework<BPC_B> {
public:
    explicit BPC_B(Core::System& system_) : ServiceFramework{system_, "bpc:b"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &BPC_B::RejectUnavailablePowerHardware, "GetSleepButtonState"},
            {1, &BPC_B::RejectUnavailablePowerHardware, "GetPowerButtonEvent"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    void RejectUnavailablePowerHardware(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class BPC_W final : public ServiceFramework<BPC_W> {
public:
    explicit BPC_W(Core::System& system_) : ServiceFramework{system_, "bpc:w"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &BPC_W::RejectUnavailablePowerHardware, "CreateWakeupTimer"},
            {1, &BPC_W::RejectUnavailablePowerHardware, "CancelWakeupTimer"},
            {2, &BPC_W::RejectUnavailablePowerHardware, "EnableWakeupTimerOnDevice"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    void RejectUnavailablePowerHardware(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class BPC_AMS final : public ServiceFramework<BPC_AMS> {
public:
    explicit BPC_AMS(Core::System& system_) : ServiceFramework{system_, "bpc:ams"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {65000, &BPC_AMS::RejectUnavailablePowerHardware, "RebootToFatalError"},
            {65001, &BPC_AMS::RejectUnavailablePowerHardware, "SetRebootPayload"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    void RejectUnavailablePowerHardware(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("bpc", std::make_shared<BPC>(system), 13);
    server_manager->RegisterNamedService("bpc:r", std::make_shared<BPC_R>(system), 13);
    server_manager->RegisterNamedService("bpc:c", std::make_shared<BPC_C>(system), 13);
    server_manager->RegisterNamedService("bpc:b", std::make_shared<BPC_B>(system), 13);
    server_manager->RegisterNamedService("bpc:w", std::make_shared<BPC_W>(system), 13);
    server_manager->RegisterNamedService("bpc:ams", std::make_shared<BPC_AMS>(system), 4);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::BPC
