// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/filesystem/fsp/fsp_pr.h"

namespace Service::FileSystem {

FSP_PR::FSP_PR(Core::System& system_) : ServiceFramework{system_, "fsp:pr"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &FSP_PR::RejectProgramRegistryMutation, "RegisterProgram"},
        {1, &FSP_PR::RejectProgramRegistryMutation, "UnregisterProgram"},
        {2, D<&FSP_PR::SetCurrentProcess>, "SetCurrentProcess"},
        {256, D<&FSP_PR::SetEnabledProgramVerification>, "SetEnabledProgramVerification"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

FSP_PR::~FSP_PR() = default;

void FSP_PR::RejectProgramRegistryMutation(HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "fsp:pr program registry mutation is unavailable");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result FSP_PR::SetCurrentProcess(ClientProcessId process_id) {
    current_process_id = *process_id;
    LOG_DEBUG(Service_FS, "called, process_id={}", current_process_id);
    R_SUCCEED();
}

Result FSP_PR::SetEnabledProgramVerification(bool enabled) {
    program_verification_enabled = enabled;
    LOG_DEBUG(Service_FS, "called, enabled={}", enabled);
    R_SUCCEED();
}

} // namespace Service::FileSystem
