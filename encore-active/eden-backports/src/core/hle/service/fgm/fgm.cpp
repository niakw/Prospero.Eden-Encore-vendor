// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/fgm/fgm.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::FGM {

class IRequest final : public ServiceFramework<IRequest> {
public:
    explicit IRequest(Core::System& system_) : ServiceFramework{system_, "IRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IRequest::RejectUnavailableClockRequest, "Initialize"},
            {1, &IRequest::RejectUnavailableClockRequest, "Set"},
            {2, &IRequest::RejectUnavailableClockRequest, "Get"},
            {3, &IRequest::RejectUnavailableClockRequest, "Cancel"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableClockRequest(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class FGM final : public ServiceFramework<FGM> {
public:
    explicit FGM(Core::System& system_, const char* name) : ServiceFramework{system_, name} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &FGM::Initialize, "Initialize"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void Initialize(HLERequestContext& ctx) {
        LOG_DEBUG(Service_FGM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IRequest>(ctx, system);
    }
};


void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("fgm", std::make_shared<FGM>(system, "fgm"));
    server_manager->RegisterNamedService("fgm:0", std::make_shared<FGM>(system, "fgm:0"));
    server_manager->RegisterNamedService("fgm:9", std::make_shared<FGM>(system, "fgm:9"));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::FGM
