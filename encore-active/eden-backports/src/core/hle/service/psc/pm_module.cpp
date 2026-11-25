// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/psc/pm_module.h"

namespace Service::PSC {

IPmModule::IPmModule(Core::System& system_) : ServiceFramework{system_, "IPmModule"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IPmModule::RejectUnavailablePowerModule, "Initialize"},
            {1, &IPmModule::RejectUnavailablePowerModule, "GetRequest"},
            {2, &IPmModule::RejectUnavailablePowerModule, "Acknowledge"},
            {3, &IPmModule::RejectUnavailablePowerModule, "Finalize"},
            {4, &IPmModule::RejectUnavailablePowerModule, "AcknowledgeEx"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

void IPmModule::RejectUnavailablePowerModule(HLERequestContext& ctx) {
    // Horizon power-state control has no native guest backend on PS5.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IPmModule::~IPmModule() = default;

} // namespace Service::PSC
