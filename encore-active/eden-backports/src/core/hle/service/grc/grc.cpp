// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/grc/grc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::GRC {

class GRC final : public ServiceFramework<GRC> {
public:
    explicit GRC(Core::System& system_) : ServiceFramework{system_, "grc:c"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, &GRC::RejectUnavailableGameRecording, "OpenContinuousRecorder"},
            {2, &GRC::RejectUnavailableGameRecording, "OpenGameMovieTrimmer"},
            {3, &GRC::RejectUnavailableGameRecording, "OpenOffscreenRecorder"},
            {101, &GRC::RejectUnavailableGameRecording, "CreateMovieMaker"},
            {9903, &GRC::RejectUnavailableGameRecording, "SetOffscreenRecordingMarker"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableGameRecording(HLERequestContext& ctx) {
        // No Horizon recording or trimming backend; never report a fabricated movie.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

class GRC_D final : public ServiceFramework<GRC_D> {
public:
    explicit GRC_D(Core::System& system_) : ServiceFramework{system_, "grc:d"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, &GRC_D::RejectUnavailableGameRecording, "Initialize"},
            {2, &GRC_D::RejectUnavailableGameRecording, "Transfer"},
            {3, &GRC_D::RejectUnavailableGameRecording, "Cmd3"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableGameRecording(HLERequestContext& ctx) {
        // No Horizon recording or trimming backend; never report a fabricated movie.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("grc:c", std::make_shared<GRC>(system), 4);
    server_manager->RegisterNamedService("grc:d", std::make_shared<GRC_D>(system), 4);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::GRC
