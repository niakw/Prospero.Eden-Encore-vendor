// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/service/glue/bgtc.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Glue {

BGTC_T::BGTC_T(Core::System& system_) : ServiceFramework{system_, "bgtc:t"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {100, &BGTC_T::OpenTaskService, "OpenTaskService"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

BGTC_T::~BGTC_T() = default;

void BGTC_T::OpenTaskService(HLERequestContext& ctx) {
    LOG_DEBUG(Service_BGTC, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ITaskService>(ctx, system);
}

ITaskService::ITaskService(Core::System& system_) : ServiceFramework{system_, "ITaskService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, nullptr, "NotifyTaskStarting"},
        {2, nullptr, "NotifyTaskFinished"},
        {3, nullptr, "GetTriggerEvent"},
        {4, D<&ITaskService::IsInHalfAwake>, "IsInHalfAwake"},
        {5, nullptr, "NotifyClientName"},
        {6, D<&ITaskService::IsInFullAwake>, "IsInFullAwake"},
        {11, nullptr, "ScheduleTask"},
        {12, nullptr, "GetScheduledTaskInterval"},
        {13, nullptr, "UnscheduleTask"},
        {14, nullptr, "GetScheduleEvent"},
        {15, nullptr, "SchedulePeriodicTask"},
        {16, nullptr, "Unknown16"},
        {101, D<&ITaskService::GetOperationMode>, "GetOperationMode"},
        {102, D<&ITaskService::WillDisconnectNetworkWhenEnteringSleep>, "WillDisconnectNetworkWhenEnteringSleep"},
        {103, D<&ITaskService::WillStayHalfAwakeInsteadSleep>, "WillStayHalfAwakeInsteadSleep"},
        {200, nullptr, "Unknown200"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ITaskService::~ITaskService() = default;

// The running guest has no half-awake or suspended background execution mode.
// Host PS5 sleep and its network policy are not controlled by BGTC.
Result ITaskService::IsInHalfAwake(Out<bool> out_half_awake) {
    *out_half_awake = false;
    R_SUCCEED();
}

Result ITaskService::IsInFullAwake(Out<bool> out_full_awake) {
    *out_full_awake = true;
    R_SUCCEED();
}

Result ITaskService::GetOperationMode(Out<u32> out_operation_mode) {
    // nn::am::OperationMode / BGTC ABI: 0 = handheld, 1 = docked.
    *out_operation_mode = Settings::IsDockedMode() ? 1U : 0U;
    R_SUCCEED();
}

Result ITaskService::WillDisconnectNetworkWhenEnteringSleep(Out<bool> out_disconnect) {
    *out_disconnect = false;
    R_SUCCEED();
}

Result ITaskService::WillStayHalfAwakeInsteadSleep(Out<bool> out_half_awake) {
    *out_half_awake = false;
    R_SUCCEED();
}

BGTC_SC::BGTC_SC(Core::System& system_) : ServiceFramework{system_, "bgtc:sc"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, nullptr, "GetState"},
        {2, nullptr, "GetStateChangedEvent"},
        {3, nullptr, "NotifyEnteringHalfAwake"},
        {4, nullptr, "NotifyLeavingHalfAwake"},
        {5, nullptr, "SetIsUsingSleepUnsupportedDevices"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

BGTC_SC::~BGTC_SC() = default;

} // namespace Service::Glue
