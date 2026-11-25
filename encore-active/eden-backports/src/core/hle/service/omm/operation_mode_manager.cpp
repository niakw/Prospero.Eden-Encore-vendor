// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/omm/operation_mode_manager.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::OMM {

IOperationModeManager::IOperationModeManager(Core::System& system_)
    : ServiceFramework{system_, "omm"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IOperationModeManager::GetOperationMode, "GetOperationMode"},
        {1, &IOperationModeManager::RejectUnavailableHardware, "GetOperationModeChangeEvent"},
        {2, &IOperationModeManager::RejectUnavailableHardware, "EnableAudioVisual"},
        {3, &IOperationModeManager::RejectUnavailableHardware, "DisableAudioVisual"},
        {4, &IOperationModeManager::RejectUnavailableHardware, "EnterSleepAndWait"},
        {5, &IOperationModeManager::GetCradleStatus, "GetCradleStatus"},
        {6, &IOperationModeManager::RejectUnavailableHardware, "FadeInDisplay"},
        {7, &IOperationModeManager::RejectUnavailableHardware, "FadeOutDisplay"},
        {8, &IOperationModeManager::RejectUnavailableHardware, "GetCradleFwVersion"},
        {9, &IOperationModeManager::RejectUnavailableHardware, "NotifyCecSettingsChanged"},
        {10, &IOperationModeManager::RejectUnavailableHardware, "SetOperationModePolicy"},
        {11, &IOperationModeManager::GetDefaultDisplayResolution, "GetDefaultDisplayResolution"},
        {12, &IOperationModeManager::RejectUnavailableHardware, "GetDefaultDisplayResolutionChangeEvent"},
        {13, &IOperationModeManager::RejectUnavailableHardware, "UpdateDefaultDisplayResolution"},
        {14, &IOperationModeManager::RejectUnavailableHardware, "ShouldSleepOnBoot"},
        {15, &IOperationModeManager::RejectUnavailableHardware, "NotifyHdcpApplicationExecutionStarted"},
        {16, &IOperationModeManager::RejectUnavailableHardware, "NotifyHdcpApplicationExecutionFinished"},
        {17, &IOperationModeManager::RejectUnavailableHardware, "NotifyHdcpApplicationDrawingStarted"},
        {18, &IOperationModeManager::RejectUnavailableHardware, "NotifyHdcpApplicationDrawingFinished"},
        {19, &IOperationModeManager::RejectUnavailableHardware, "GetHdcpAuthenticationFailedEvent"},
        {20, &IOperationModeManager::RejectUnavailableHardware, "GetHdcpAuthenticationFailedEmulationEnabled"},
        {21, &IOperationModeManager::RejectUnavailableHardware, "SetHdcpAuthenticationFailedEmulation"},
        {22, &IOperationModeManager::RejectUnavailableHardware, "GetHdcpStateChangeEvent"},
        {23, &IOperationModeManager::RejectUnavailableHardware, "GetHdcpState"},
        {24, &IOperationModeManager::RejectUnavailableHardware, "ShowCardUpdateProcessing"},
        {25, &IOperationModeManager::RejectUnavailableHardware, "SetApplicationCecSettingsAndNotifyChanged"},
        {26, &IOperationModeManager::RejectUnavailableHardware, "GetOperationModeSystemInfo"},
        {27, &IOperationModeManager::RejectUnavailableHardware, "GetAppletFullAwakingSystemEvent"},
        {28, &IOperationModeManager::RejectUnavailableHardware, "CreateCradleFirmwareUpdater"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IOperationModeManager::GetOperationMode(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(Settings::IsDockedMode() ? 1 : 0);
}

void IOperationModeManager::GetCradleStatus(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(Settings::IsDockedMode() ? 1 : 0);
}

void IOperationModeManager::GetDefaultDisplayResolution(HLERequestContext& ctx) {
    const bool docked = Settings::IsDockedMode();
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(static_cast<s32>(docked ? VI::DisplayResolution::DockedWidth
                                       : VI::DisplayResolution::UndockedWidth));
    rb.Push<s32>(static_cast<s32>(docked ? VI::DisplayResolution::DockedHeight
                                       : VI::DisplayResolution::UndockedHeight));
}

void IOperationModeManager::RejectUnavailableHardware(HLERequestContext& ctx) {
    // No Switch cradle, HDCP controller, sleep scheduler or CEC backend on PS5.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

IOperationModeManager::~IOperationModeManager() = default;

} // namespace Service::OMM
