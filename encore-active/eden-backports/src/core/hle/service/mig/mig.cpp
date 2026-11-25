// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/mig/mig.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Migration {

class MIG_USR final : public ServiceFramework<MIG_USR> {
public:
    explicit MIG_USR(Core::System& system_) : ServiceFramework{system_, "mig:usr"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown0"}, //19.0.0+
            {1, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown1"}, //20.0.0+
            {2, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2"}, //20.0.0+
            {10, &MIG_USR::RejectUnavailableSwitchBackend, "TryGetLastMigrationInfo"},
            {11, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown11"}, //20.0.0+
            {100, &MIG_USR::RejectUnavailableSwitchBackend, "CreateUserMigrationServer"}, //7.0.0+
            {101, &MIG_USR::RejectUnavailableSwitchBackend, "ResumeUserMigrationServer"}, //7.0.0+
            {200, &MIG_USR::RejectUnavailableSwitchBackend, "CreateUserMigrationClient"}, //7.0.0+
            {201, &MIG_USR::RejectUnavailableSwitchBackend, "ResumeUserMigrationClient"}, //7.0.0+
            {1001, &MIG_USR::RejectUnavailableSwitchBackend, "GetSaveDataMigrationPolicyInfoAsync"}, //8.0.0-20.5.0
            {1010, &MIG_USR::RejectUnavailableSwitchBackend, "TryGetLastSaveDataMigrationInfo"}, //7.0.0+
            {1100, &MIG_USR::RejectUnavailableSwitchBackend, "CreateSaveDataMigrationServer"}, //7.0.0-19.0.1
            {1101, &MIG_USR::RejectUnavailableSwitchBackend, "ResumeSaveDataMigrationServer"}, //7.0.0+
            {1110, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown1101"}, //17.0.0+
            {1200, &MIG_USR::RejectUnavailableSwitchBackend, "CreateSaveDataMigrationClient"}, //7.0.0+
            {1201, &MIG_USR::RejectUnavailableSwitchBackend, "ResumeSaveDataMigrationClient"}, //7.0.0+
            {2001, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2001"}, //20.0.0+
            {2010, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2010"}, //20.0.0+
            {2100, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2100"}, //20.0.0+
            {2110, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2110"}, //20.0.0+
            {2200, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2200"}, //20.0.0+
            {2210, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2210"}, //20.0.0+
            {2220, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2220"}, //20.0.0+
            {2230, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2230"}, //20.0.0+
            {2231, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2231"}, //20.0.0+
            {2232, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2232"}, //20.0.0+
            {2233, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2233"}, //20.0.0+
            {2234, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2234"}, //20.0.0+
            {2250, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2250"}, //20.0.0+
            {2260, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2260"}, //20.0.0+
            {2270, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2270"}, //20.0.0+
            {2280, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2280"}, //20.0.0+
            {2300, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2300"}, //20.0.0+
            {2310, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2310"}, //20.0.0+
            {2400, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2400"}, //20.0.0+
            {2420, &MIG_USR::RejectUnavailableSwitchBackend, "Unknown2420"}, //20.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableSwitchBackend(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("mig:user", std::make_shared<MIG_USR>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Migration
