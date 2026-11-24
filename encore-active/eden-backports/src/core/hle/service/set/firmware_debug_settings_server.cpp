// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/result.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/set/firmware_debug_settings_server.h"

namespace Service::Set {

IFirmwareDebugSettingsServer::IFirmwareDebugSettingsServer(Core::System& system_)
    : ServiceFramework{system_, "set:fd"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {2, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "SetSettingsItemValue"},
        {3, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "ResetSettingsItemValue"},
        {4, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "CreateSettingsItemKeyIterator"},
        {10, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "ReadSettings"},
        {11, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "ResetSettings"},
        {20, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "SetWebInspectorFlag"},
        {21, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "SetAllowedSslHosts"},
        {22, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "SetHostFsMountPoint"},
        {23, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "SetMemoryUsageRateFlag"},
        {24, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "CommitSettings"}, //20.0.0+
        {27, &IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting, "SetHttpAuthConfigs"}, //21.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IFirmwareDebugSettingsServer::RejectUnavailableDebugSetting(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

IFirmwareDebugSettingsServer::~IFirmwareDebugSettingsServer() = default;

} // namespace Service::Set
