// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/develop_interface.h"

namespace Service::NS {

IDevelopInterface::IDevelopInterface(Core::System& system_) : ServiceFramework{system_, "ns:dev"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IDevelopInterface::RejectUnsupportedDevelopCommand, "LaunchProgram"},
        {1, &IDevelopInterface::RejectUnsupportedDevelopCommand, "TerminateProcess"},
        {2, &IDevelopInterface::RejectUnsupportedDevelopCommand, "TerminateProgram"},
        {4, &IDevelopInterface::RejectUnsupportedDevelopCommand, "GetShellEvent"},
        {5, &IDevelopInterface::RejectUnsupportedDevelopCommand, "GetShellEventInfo"},
        {6, &IDevelopInterface::RejectUnsupportedDevelopCommand, "TerminateApplication"},
        {7, &IDevelopInterface::RejectUnsupportedDevelopCommand, "PrepareLaunchProgramFromHost"},
        {8, &IDevelopInterface::RejectUnsupportedDevelopCommand, "LaunchApplicationFromHost"},
        {9, &IDevelopInterface::RejectUnsupportedDevelopCommand, "LaunchApplicationWithStorageIdForDevelop"},
        {10, &IDevelopInterface::RejectUnsupportedDevelopCommand, "IsSystemMemoryResourceLimitBoosted"},
        {11, &IDevelopInterface::RejectUnsupportedDevelopCommand, "GetRunningApplicationProcessIdForDevelop"},
        {12, &IDevelopInterface::RejectUnsupportedDevelopCommand, "SetCurrentApplicationRightsEnvironmentCanBeActiveForDevelop"},
        {13, &IDevelopInterface::RejectUnsupportedDevelopCommand, "CreateApplicationResourceForDevelop"},
        {14, &IDevelopInterface::RejectUnsupportedDevelopCommand, "IsPreomiaForDevelop"},
        {15, &IDevelopInterface::RejectUnsupportedDevelopCommand, "GetApplicationProgramIdFromHost"},
        {16, &IDevelopInterface::RejectUnsupportedDevelopCommand, "RefreshCachedDebugValues"},
        {17, &IDevelopInterface::RejectUnsupportedDevelopCommand, "PrepareLaunchApplicationFromHost"},
        {18, &IDevelopInterface::RejectUnsupportedDevelopCommand, "GetLaunchEvent"},
        {19, &IDevelopInterface::RejectUnsupportedDevelopCommand, "GetLaunchResult"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDevelopInterface::~IDevelopInterface() = default;

void IDevelopInterface::RejectUnsupportedDevelopCommand(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "Rejecting unsupported NS development command {}", ctx.GetCommand());

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented);
}

} // namespace Service::NS
