// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/btm/btm_debug.h"

namespace Service::BTM {

IBtmDebug::IBtmDebug(Core::System& system_) : ServiceFramework{system_, "btm:dbg"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IBtmDebug::RejectUnsupportedDebugCommand, "AcquireDiscoveryEvent"},
        {1, &IBtmDebug::RejectUnsupportedDebugCommand, "StartDiscovery"},
        {2, &IBtmDebug::RejectUnsupportedDebugCommand, "CancelDiscovery"},
        {3, &IBtmDebug::RejectUnsupportedDebugCommand, "GetDeviceProperty"},
        {4, &IBtmDebug::RejectUnsupportedDebugCommand, "CreateBond"},
        {5, &IBtmDebug::RejectUnsupportedDebugCommand, "CancelBond"},
        {6, &IBtmDebug::RejectUnsupportedDebugCommand, "SetTsiMode"},
        {7, &IBtmDebug::RejectUnsupportedDebugCommand, "GeneralTest"},
        {8, &IBtmDebug::RejectUnsupportedDebugCommand, "HidConnect"},
        {9, &IBtmDebug::RejectUnsupportedDebugCommand, "GeneralGet"}, //5.0.0+
        {10, &IBtmDebug::RejectUnsupportedDebugCommand, "GetGattClientDisconnectionReason"}, //5.0.0+
        {11, &IBtmDebug::RejectUnsupportedDebugCommand, "GetBleConnectionParameter"}, //5.1.0+
        {12, &IBtmDebug::RejectUnsupportedDebugCommand, "GetBleConnectionParameterRequest"}, //5.1.0+
        {13, &IBtmDebug::RejectUnsupportedDebugCommand, "GetDiscoveredDevice"}, //12.0.0+
        {14, &IBtmDebug::RejectUnsupportedDebugCommand, "SleepAwakeLoopTest"}, //15.0.0+
        {15, &IBtmDebug::RejectUnsupportedDebugCommand, "SleepTest"}, //15.0.0+
        {16, &IBtmDebug::RejectUnsupportedDebugCommand, "MinimumAwakeTest"}, //15.0.0+
        {17, &IBtmDebug::RejectUnsupportedDebugCommand, "ForceEnableBtm"}, //15.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);
}

IBtmDebug::~IBtmDebug() = default;

void IBtmDebug::RejectUnsupportedDebugCommand(HLERequestContext& ctx) {
    LOG_DEBUG(Service_BTM, "Rejecting unsupported BTM debug command {}", ctx.GetCommand());

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented);
}

} // namespace Service::BTM
