// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/download_task_interface.h"

namespace Service::NS {

IDownloadTaskInterface::IDownloadTaskInterface(Core::System& system_)
    : ServiceFramework{system_, "IDownloadTaskInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {701, &IDownloadTaskInterface::RejectUnavailableDownloadTask, "ClearTaskStatusList"},
        {702, &IDownloadTaskInterface::RejectUnavailableDownloadTask, "RequestDownloadTaskList"},
        {703, &IDownloadTaskInterface::RejectUnavailableDownloadTask, "RequestEnsureDownloadTask"},
        {704, &IDownloadTaskInterface::RejectUnavailableDownloadTask, "ListDownloadTaskStatus"},
        {705, &IDownloadTaskInterface::RejectUnavailableDownloadTask, "RequestDownloadTaskListData"},
        {706, &IDownloadTaskInterface::RejectUnavailableDownloadTask, "TryCommitCurrentApplicationDownloadTask"},
        {707, D<&IDownloadTaskInterface::EnableAutoCommit>, "EnableAutoCommit"},
        {708, D<&IDownloadTaskInterface::DisableAutoCommit>, "DisableAutoCommit"},
        {709, &IDownloadTaskInterface::RejectUnavailableDownloadTask, "TriggerDynamicCommitEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IDownloadTaskInterface::RejectUnavailableDownloadTask(HLERequestContext& ctx) {
    LOG_WARNING(Service_NS, "download task backend is unavailable in Encore");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IDownloadTaskInterface::~IDownloadTaskInterface() = default;

Result IDownloadTaskInterface::EnableAutoCommit() {
    LOG_DEBUG(Service_NS, "called; no NS download-task backend is modeled");
    R_SUCCEED();
}
Result IDownloadTaskInterface::DisableAutoCommit() {
    LOG_DEBUG(Service_NS, "called; no NS download-task backend is modeled");
    R_SUCCEED();
}

} // namespace Service::NS
