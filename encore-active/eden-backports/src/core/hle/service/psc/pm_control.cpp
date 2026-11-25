// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/psc/pm_control.h"

namespace Service::PSC {

IPmControl::IPmControl(Core::System& system_) : ServiceFramework{system_, "psc:c"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IPmControl::RejectUnavailablePowerControl, "Initialize"},
        {1, &IPmControl::RejectUnavailablePowerControl, "DispatchRequest"},
        {2, &IPmControl::RejectUnavailablePowerControl, "GetResult"},
        {3, &IPmControl::RejectUnavailablePowerControl, "GetState"},
        {4, &IPmControl::RejectUnavailablePowerControl, "Cancel"},
        {5, &IPmControl::RejectUnavailablePowerControl, "PrintModuleInformation"},
        {6, &IPmControl::RejectUnavailablePowerControl, "GetModuleInformation"},
        {10, &IPmControl::RejectUnavailablePowerControl, "AcquireStateLock"},
        {11, &IPmControl::RejectUnavailablePowerControl, "HasStateLock"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IPmControl::RejectUnavailablePowerControl(HLERequestContext& ctx) {
    // Horizon power-state control has no native guest backend on PS5.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IPmControl::~IPmControl() = default;

} // namespace Service::PSC
