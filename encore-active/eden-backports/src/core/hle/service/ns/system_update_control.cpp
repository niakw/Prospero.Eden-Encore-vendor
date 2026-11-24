// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/system_update_control.h"

namespace Service::NS {

ISystemUpdateControl::ISystemUpdateControl(Core::System& system_)
    : ServiceFramework{system_, "ISystemUpdateControl"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "HasDownloaded"},
        {1, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "RequestCheckLatestUpdate"},
        {2, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "RequestDownloadLatestUpdate"},
        {3, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetDownloadProgress"},
        {4, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "ApplyDownloadedUpdate"},
        {5, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "RequestPrepareCardUpdate"},
        {6, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetPrepareCardUpdateProgress"},
        {7, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "HasPreparedCardUpdate"},
        {8, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "ApplyCardUpdate"},
        {9, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetDownloadedEulaDataSize"},
        {10, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetDownloadedEulaData"},
        {11, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "SetupCardUpdate"},
        {12, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetPreparedCardUpdateEulaDataSize"},
        {13, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetPreparedCardUpdateEulaData"},
        {14, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "SetupCardUpdateViaSystemUpdater"},
        {15, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "HasReceived"},
        {16, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "RequestReceiveSystemUpdate"},
        {17, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetReceiveProgress"},
        {18, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "ApplyReceivedUpdate"},
        {19, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetReceivedEulaDataSize"},
        {20, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "GetReceivedEulaData"},
        {21, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "SetupToReceiveSystemUpdate"},
        {22, &ISystemUpdateControl::RejectUnavailableSystemUpdate, "RequestCheckLatestUpdateIncludesRebootlessUpdate"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void ISystemUpdateControl::RejectUnavailableSystemUpdate(HLERequestContext& ctx) {
    LOG_WARNING(Service_NS, "System-update control command is unavailable in Encore");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

ISystemUpdateControl::~ISystemUpdateControl() = default;

} // namespace Service::NS
