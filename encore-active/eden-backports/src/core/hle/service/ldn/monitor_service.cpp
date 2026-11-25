// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_results.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ldn/monitor_service.h"

namespace Service::LDN {

IMonitorService::IMonitorService(Core::System& system_)
    : ServiceFramework{system_, "IMonitorService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IMonitorService::GetStateForMonitor>, "GetStateForMonitor"},
        {1, &IMonitorService::RejectUnsupportedMonitorQuery, "GetNetworkInfoForMonitor"},
        {2, &IMonitorService::RejectUnsupportedMonitorQuery, "GetIpv4AddressForMonitor"},
        {3, &IMonitorService::RejectUnsupportedMonitorQuery, "GetDisconnectReasonForMonitor"},
        {4, &IMonitorService::RejectUnsupportedMonitorQuery, "GetSecurityParameterForMonitor"},
        {5, &IMonitorService::RejectUnsupportedMonitorQuery, "GetNetworkConfigForMonitor"},
        {100, C<&IMonitorService::InitializeMonitor>, "InitializeMonitor"},
        {101, C<&IMonitorService::FinalizeMonitor>, "FinalizeMonitor"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IMonitorService::RejectUnsupportedMonitorQuery(HLERequestContext& ctx) {
    // Monitoring queries have no independent PS5 backend. Core LDN state,
    // scan/connect/create-network services remain unchanged.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IMonitorService::~IMonitorService() = default;

Result IMonitorService::GetStateForMonitor(Out<State> out_state) {
    LOG_DEBUG(Service_LDN, "called, state={}", state);
    *out_state = state;
    R_SUCCEED();
}

Result IMonitorService::InitializeMonitor() {
    LOG_INFO(Service_LDN, "called");
    state = State::Initialized;
    R_SUCCEED();
}

Result IMonitorService::FinalizeMonitor() {
    LOG_INFO(Service_LDN, "called");
    state = State::None;
    R_SUCCEED();
}

} // namespace Service::LDN
