// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ldn/sf_service_monitor.h"

namespace Service::LDN {

ISfServiceMonitor::ISfServiceMonitor(Core::System& system_)
    : ServiceFramework{system_, "ISfServiceMonitor"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ISfServiceMonitor::Initialize>, "Initialize"},
            {256, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "AttachNetworkInterfaceStateChangeEvent"},
            {264, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetNetworkInterfaceLastError"},
            {272, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetRole"},
            {280, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetAdvertiseData"},
            {281, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetAdvertiseData2"},
            {288, C<&ISfServiceMonitor::GetGroupInfo>, "GetGroupInfo"},
            {296, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetGroupInfo2"},
            {304, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetGroupOwner"},
            {312, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetIpConfig"},
            {320, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetLinkLevel"},
            {328, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "AttachJoinEvent"},
            {336, &ISfServiceMonitor::RejectUnsupportedSfMonitorQuery, "GetMembers"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

void ISfServiceMonitor::RejectUnsupportedSfMonitorQuery(HLERequestContext& ctx) {
    // Monitoring queries have no independent PS5 backend. Core LDN state,
    // scan/connect/create-network services remain unchanged.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

ISfServiceMonitor::~ISfServiceMonitor() = default;

Result ISfServiceMonitor::Initialize(Out<u32> out_value) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    *out_value = 0;
    R_SUCCEED();
}

Result ISfServiceMonitor::GetGroupInfo(
    OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info, GroupInfo in_group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    *out_group_info = in_group_info;
    R_SUCCEED();
}

} // namespace Service::LDN
