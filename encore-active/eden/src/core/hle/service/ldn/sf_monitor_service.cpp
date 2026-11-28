// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ldn/sf_monitor_service.h"

namespace Service::LDN {

ISfMonitorService::ISfMonitorService(Core::System& system_)
    : ServiceFramework{system_, "ISfMonitorService"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ISfMonitorService::Initialize>, "Initialize"},
            {288, C<&ISfMonitorService::GetGroupInfo>, "GetGroupInfo"},
            {320, &ISfMonitorService::RejectUnsupportedSfMonitorQuery, "GetLinkLevel"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

void ISfMonitorService::RejectUnsupportedSfMonitorQuery(HLERequestContext& ctx) {
    // Monitoring queries have no independent PS5 backend. Core LDN state,
    // scan/connect/create-network services remain unchanged.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

ISfMonitorService::~ISfMonitorService() = default;

Result ISfMonitorService::Initialize(Out<u32> out_value) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    *out_value = 0;
    R_SUCCEED();
}

Result ISfMonitorService::GetGroupInfo(
    OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    *out_group_info = GroupInfo{};
    R_SUCCEED();
}

} // namespace Service::LDN
