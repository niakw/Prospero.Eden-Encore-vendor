// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/omm/power_state_interface.h"

namespace Service::OMM {

IPowerStateInterface::IPowerStateInterface(Core::System& system_)
    : ServiceFramework{system_, "spsm"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IPowerStateInterface::RejectUnavailablePowerControl, "GetState"},
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
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IPowerStateInterface::RejectUnavailablePowerControl(HLERequestContext& ctx) {
    // Guest-requested sleep, shutdown and host-button manipulation must not
    // control the physical PS5 or report a fictitious completed transition.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

IPowerStateInterface::~IPowerStateInterface() = default;

} // namespace Service::OMM
