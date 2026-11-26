// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/psc/ovln/ovln_types.h"
#include "core/hle/service/psc/ovln/receiver_service.h"
#include "core/hle/service/psc/ovln/sender_service.h"
#include "core/hle/service/psc/pm_control.h"
#include "core/hle/service/psc/pm_service.h"
#include "core/hle/service/psc/psc.h"
#include "core/hle/service/psc/time/manager.h"
#include "core/hle/service/psc/time/power_state_service.h"
#include "core/hle/service/psc/time/service_manager.h"
#include "core/hle/service/psc/time/static.h"
#include "core/hle/service/service.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::PSC {

class PSC_L final : public ServiceFramework<PSC_L> {
public:
    explicit PSC_L(Core::System& system_) : ServiceFramework{system_, "psc:l"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &PSC_L::RejectUnsupportedDevice, "Initialize_3"},
            {1, &PSC_L::RejectUnsupportedDevice, "Lock"},
            {2, &PSC_L::RejectUnsupportedDevice, "Unlock"},
            {3, &PSC_L::RejectUnsupportedDevice, "IsLocked"},
            {4, &PSC_L::RejectUnsupportedDevice, "GetRelatedState"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class INS_R final : public ServiceFramework<INS_R> {
public:
    explicit INS_R(Core::System& system_) : ServiceFramework{system_, "ins:r"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &INS_R::RejectUnsupportedDevice, "GetInputSourceState"},
            {1, &INS_R::RejectUnsupportedDevice, "GetTriggerTargetEvent"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class INS_S final : public ServiceFramework<INS_S> {
public:
    explicit INS_S(Core::System& system_) : ServiceFramework{system_, "ins:s"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &INS_S::RejectUnsupportedDevice, "GetNotifyEvent"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class HSHL_SYS final : public ServiceFramework<HSHL_SYS> {
public:
    explicit HSHL_SYS(Core::System& system_) : ServiceFramework{system_, "hshl:sys"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &HSHL_SYS::RejectUnsupportedDevice, "GetBatteryPercentage"},
            {1, &HSHL_SYS::RejectUnsupportedDevice, "GetChargerType"},
            {2, &HSHL_SYS::RejectUnsupportedDevice, "OpenChargeSession"},
            {3, &HSHL_SYS::RejectUnsupportedDevice, "GetRawBatteryPercentage"},
            {4, &HSHL_SYS::RejectUnsupportedDevice, "GetBatteryVoltageLevel"},
            {5, &HSHL_SYS::RejectUnsupportedDevice, "OpenThermalSession"},
            {6, &HSHL_SYS::RejectUnsupportedDevice, "GetAbnormalTemperatureSet"},
            {7, &HSHL_SYS::RejectUnsupportedDevice, "OpenClockSession"},
            {8, &HSHL_SYS::RejectUnsupportedDevice, "GetClockRate"},
            {9, &HSHL_SYS::RejectUnsupportedDevice, "OpenBridgeSession"},
            {10, &HSHL_SYS::RejectUnsupportedDevice, "GetBridgePowerSupply"},
            {11, &HSHL_SYS::RejectUnsupportedDevice, "OpenVsysVoltageSession"},
            {12, &HSHL_SYS::RejectUnsupportedDevice, "GetIsBatteryEnoughForFullAwake"},
            {13, &HSHL_SYS::RejectUnsupportedDevice, "GetIsCharging"},
            {14, &HSHL_SYS::RejectUnsupportedDevice, "Cmd14"},
            {15, &HSHL_SYS::RejectUnsupportedDevice, "Cmd15"},
            {16, &HSHL_SYS::RejectUnsupportedDevice, "Cmd16"},
            {17, &HSHL_SYS::RejectUnsupportedDevice, "Cmd17"},
            {18, &HSHL_SYS::RejectUnsupportedDevice, "Cmd18"},
            {19, &HSHL_SYS::RejectUnsupportedDevice, "Cmd19"},
            {20, &HSHL_SYS::RejectUnsupportedDevice, "Cmd20"},
            {21, &HSHL_SYS::RejectUnsupportedDevice, "Cmd21"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class HSHL_SET final : public ServiceFramework<HSHL_SET> {
public:
    explicit HSHL_SET(Core::System& system_) : ServiceFramework{system_, "hshl:set"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &HSHL_SET::RejectUnsupportedDevice, "OpenChargeSession_2"},
            {1, &HSHL_SET::RejectUnsupportedDevice, "OpenThermalSession_2"},
            {2, &HSHL_SET::RejectUnsupportedDevice, "SetClockRate"},
            {3, &HSHL_SET::RejectUnsupportedDevice, "SetBridgePowerSupply"},
            {4, &HSHL_SET::RejectUnsupportedDevice, "Cmd4"},
            {5, &HSHL_SET::RejectUnsupportedDevice, "Cmd5"},
            {6, &HSHL_SET::RejectUnsupportedDevice, "Cmd6"},
            {7, &HSHL_SET::RejectUnsupportedDevice, "Cmd7"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("psc:c", std::make_shared<IPmControl>(system));
    server_manager->RegisterNamedService("psc:m", std::make_shared<IPmService>(system));
    server_manager->RegisterNamedService("psc:l", std::make_shared<PSC_L>(system));
    server_manager->RegisterNamedService("ins:r", std::make_shared<INS_R>(system));
    server_manager->RegisterNamedService("ins:s", std::make_shared<INS_S>(system));
    server_manager->RegisterNamedService("hshl:sys", std::make_shared<HSHL_SYS>(system));
    server_manager->RegisterNamedService("hshl:set", std::make_shared<HSHL_SET>(system));

    auto ovln_broker = std::make_shared<OverlayNotificationBroker>(system);
    server_manager->RegisterNamedService(
        "ovln:rcv", std::make_shared<IReceiverService>(system, ovln_broker));
    server_manager->RegisterNamedService(
        "ovln:snd", std::make_shared<ISenderService>(system, ovln_broker));

    auto time = std::make_shared<Time::TimeManager>(system);

    server_manager->RegisterNamedService("time:m", std::make_shared<Time::ServiceManager>(system, time, server_manager.get()));
    server_manager->RegisterNamedService("time:su", std::make_shared<Time::StaticService>(system, Time::StaticServiceSetupInfo{0, 0, 0, 0, 0, 1}, time, "time:su"));
    server_manager->RegisterNamedService("time:al", std::make_shared<Time::IAlarmService>(system, time));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PSC
