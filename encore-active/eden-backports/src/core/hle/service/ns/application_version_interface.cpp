// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/application_version_interface.h"

namespace Service::NS {

IApplicationVersionInterface::IApplicationVersionInterface(Core::System& system_)
    : ServiceFramework{system_, "IApplicationVersionInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IApplicationVersionInterface::RejectUnavailableVersionService, "GetLaunchRequiredVersion"},
        {1, &IApplicationVersionInterface::RejectUnavailableVersionService, "UpgradeLaunchRequiredVersion"},
        {35, &IApplicationVersionInterface::RejectUnavailableVersionService, "UpdateVersionList"},
        {36, &IApplicationVersionInterface::RejectUnavailableVersionService, "PushLaunchVersion"},
        {37, &IApplicationVersionInterface::RejectUnavailableVersionService, "ListRequiredVersion"},
        {800, &IApplicationVersionInterface::RejectUnavailableVersionService, "RequestVersionList"},
        {801, &IApplicationVersionInterface::RejectUnavailableVersionService, "ListVersionList"},
        {802, &IApplicationVersionInterface::RejectUnavailableVersionService, "RequestVersionListData"},
        {900, &IApplicationVersionInterface::RejectUnavailableVersionService, "ImportAutoUpdatePolicyJsonForDebug"},
        {901, &IApplicationVersionInterface::RejectUnavailableVersionService, "ListDefaultAutoUpdatePolicy"},
        {902, &IApplicationVersionInterface::RejectUnavailableVersionService, "ListAutoUpdatePolicyForSpecificApplication"},
        {1000, &IApplicationVersionInterface::RejectUnavailableVersionService, "PerformAutoUpdate"},
        {1001, &IApplicationVersionInterface::RejectUnavailableVersionService, "ListAutoUpdateSchedule"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IApplicationVersionInterface::RejectUnavailableVersionService(HLERequestContext& ctx) {
    // Nintendo version-list / auto-update infrastructure is not available.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IApplicationVersionInterface::~IApplicationVersionInterface() = default;

} // namespace Service::NS
