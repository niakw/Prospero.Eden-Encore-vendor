// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ldr/ldr.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::LDR {

class DebugMonitor final : public ServiceFramework<DebugMonitor> {
public:
    explicit DebugMonitor(Core::System& system_) : ServiceFramework{system_, "ldr:dmnt"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &DebugMonitor::RejectUnavailableLoaderCommand, "SetProgramArgument"},
            {1, &DebugMonitor::RejectUnavailableLoaderCommand, "FlushArguments"},
            {2, &DebugMonitor::RejectUnavailableLoaderCommand, "GetProcessModuleInfo"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableLoaderCommand(HLERequestContext& ctx) {
        // Encore launches the guest application through the native emulator loader;
        // Horizon ldr debug/process-manager control is not exposed as a second host loader.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ProcessManager final : public ServiceFramework<ProcessManager> {
public:
    explicit ProcessManager(Core::System& system_) : ServiceFramework{system_, "ldr:pm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ProcessManager::RejectUnavailableLoaderCommand, "CreateProcess"},
            {1, &ProcessManager::RejectUnavailableLoaderCommand, "GetProgramInfo"},
            {2, &ProcessManager::RejectUnavailableLoaderCommand, "PinProgram"},
            {3, &ProcessManager::RejectUnavailableLoaderCommand, "UnpinProgram"},
            {4, &ProcessManager::RejectUnavailableLoaderCommand, "SetEnabledProgramVerification"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableLoaderCommand(HLERequestContext& ctx) {
        // Encore launches the guest application through the native emulator loader;
        // Horizon ldr debug/process-manager control is not exposed as a second host loader.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class Shell final : public ServiceFramework<Shell> {
public:
    explicit Shell(Core::System& system_) : ServiceFramework{system_, "ldr:shel"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &Shell::RejectUnavailableLoaderCommand, "SetProgramArgument"},
            {1, &Shell::RejectUnavailableLoaderCommand, "FlushArguments"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableLoaderCommand(HLERequestContext& ctx) {
        // Encore launches the guest application through the native emulator loader;
        // Horizon ldr debug/process-manager control is not exposed as a second host loader.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("ldr:dmnt", std::make_shared<DebugMonitor>(system), 3);
    server_manager->RegisterNamedService("ldr:pm", std::make_shared<ProcessManager>(system), 1);
    server_manager->RegisterNamedService("ldr:shel", std::make_shared<Shell>(system), 3);

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::LDR
