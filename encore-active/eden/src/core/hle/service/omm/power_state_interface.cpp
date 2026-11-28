// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/omm/power_state_interface.h"

namespace Service::OMM {

IPowerStateInterface::IPowerStateInterface(Core::System& system_)
    : ServiceFramework{system_, "spsm"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IPowerStateInterface::GetCurrentState, "GetCurrentState"},
        {1, &IPowerStateInterface::RejectUnavailablePowerControl, "EnterSleep"},
        {2, &IPowerStateInterface::RejectUnavailablePowerControl, "GetLastWakeReason"},
        {3, &IPowerStateInterface::RejectUnavailablePowerControl, "Shutdown"},
        {4, &IPowerStateInterface::RejectUnavailablePowerControl, "GetNotificationMessageEventHandle"},
        {5, &IPowerStateInterface::RejectUnavailablePowerControl, "ReceiveNotificationMessage"},
        {6, &IPowerStateInterface::RejectUnavailablePowerControl, "AnalyzeLogForLastSleepWakeSequence"},
        {7, &IPowerStateInterface::RejectUnavailablePowerControl, "ResetEventLog"},
        {8, &IPowerStateInterface::RejectUnavailablePowerControl, "AnalyzePerformanceLogForLastSleepWakeSequence"},
        {9, &IPowerStateInterface::RejectUnavailablePowerControl, "ChangeHomeButtonLongPressingTime"},
        {10, &IPowerStateInterface::RejectUnavailablePowerControl, "PutErrorState"},
        {11, &IPowerStateInterface::RejectUnavailablePowerControl, "InvalidateCurrentHomeButtonPressing"},
        {12, &IPowerStateInterface::RejectUnavailablePowerControl, "EmulatePowerButtonPressedBriefly"},
        {13, &IPowerStateInterface::RejectUnavailablePowerControl, "EmulatePowerButtonPressedFor7Sec"},
        {14, &IPowerStateInterface::RejectUnavailablePowerControl, "SetAlwaysRebootFlag"},
        {15, &IPowerStateInterface::RejectUnavailablePowerControl, "SetWakeReasonOverride"},
        {16, &IPowerStateInterface::RejectUnavailablePowerControl, "SetWakeupAlarm"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IPowerStateInterface::GetCurrentState(HLERequestContext& ctx) {
    // The emulated console is running and therefore awake. HOS 23 returns a
    // u32 power-state value here; 0 is Awake. This is a read-only query and
    // does not attempt to control the physical PS5 power state.
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0);
}

void IPowerStateInterface::RejectUnavailablePowerControl(HLERequestContext& ctx) {
    // Guest-requested sleep, shutdown and host-button manipulation must not
    // control the physical PS5 or report a fictitious completed transition.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

IPowerStateInterface::~IPowerStateInterface() = default;

} // namespace Service::OMM
