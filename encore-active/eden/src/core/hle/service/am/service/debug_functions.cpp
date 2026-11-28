// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/debug_functions.h"

namespace Service::AM {

IDebugFunctions::IDebugFunctions(Core::System& system_)
    : ServiceFramework{system_, "IDebugFunctions"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IDebugFunctions::RejectDebugOnlyCommand>, "NotifyMessageToHomeMenuForDebug"},
        {1, D<&IDebugFunctions::RejectDebugOnlyCommand>, "OpenMainApplication"},
        {10, D<&IDebugFunctions::RejectDebugOnlyCommand>, "PerformSystemButtonPressing"},
        {20, D<&IDebugFunctions::RejectDebugOnlyCommand>, "InvalidateTransitionLayer"},
        {30, D<&IDebugFunctions::RejectDebugOnlyCommand>, "RequestLaunchApplicationWithUserAndArgumentForDebug"},
        {31, D<&IDebugFunctions::RejectDebugOnlyCommand>, "RequestLaunchApplicationByApplicationLaunchInfoForDebug"},
        {40, D<&IDebugFunctions::RejectDebugOnlyCommand>, "GetAppletResourceUsageInfo"},
        {50, D<&IDebugFunctions::RejectDebugOnlyCommand>, "AddSystemProgramIdAndAppletIdForDebug"},
        {51, D<&IDebugFunctions::RejectDebugOnlyCommand>, "AddOperationConfirmedLibraryAppletIdForDebug"},
        {100, D<&IDebugFunctions::RejectDebugOnlyCommand>, "SetCpuBoostModeForApplet"},
        {101, D<&IDebugFunctions::RejectDebugOnlyCommand>, "CancelCpuBoostModeForApplet"},
        {110, D<&IDebugFunctions::RejectDebugOnlyCommand>, "PushToAppletBoundChannelForDebug"},
        {111, D<&IDebugFunctions::RejectDebugOnlyCommand>, "TryPopFromAppletBoundChannelForDebug"},
        {120, D<&IDebugFunctions::RejectDebugOnlyCommand>, "AlarmSettingNotificationEnableAppEventReserve"},
        {121, D<&IDebugFunctions::RejectDebugOnlyCommand>, "AlarmSettingNotificationDisableAppEventReserve"},
        {122, D<&IDebugFunctions::RejectDebugOnlyCommand>, "AlarmSettingNotificationPushAppEventNotify"},
        {130, D<&IDebugFunctions::RejectDebugOnlyCommand>, "FriendInvitationSetApplicationParameter"},
        {131, D<&IDebugFunctions::RejectDebugOnlyCommand>, "FriendInvitationClearApplicationParameter"},
        {132, D<&IDebugFunctions::RejectDebugOnlyCommand>, "FriendInvitationPushApplicationParameter"},
        {140, D<&IDebugFunctions::RejectDebugOnlyCommand>, "RestrictPowerOperationForSecureLaunchModeForDebug"},
        {200, D<&IDebugFunctions::RejectDebugOnlyCommand>, "CreateFloatingLibraryAppletAccepterForDebug"},
        {300, D<&IDebugFunctions::RejectDebugOnlyCommand>, "TerminateAllRunningApplicationsForDebug"},
        {900, D<&IDebugFunctions::RejectDebugOnlyCommand>, "GetGrcProcessLaunchedSystemEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDebugFunctions::~IDebugFunctions() = default;

Result IDebugFunctions::RejectDebugOnlyCommand() {
    LOG_WARNING(Service_AM, "Rejected IDebugFunctions command on release runtime");
    R_RETURN(ResultUnknown);
}

} // namespace Service::AM
