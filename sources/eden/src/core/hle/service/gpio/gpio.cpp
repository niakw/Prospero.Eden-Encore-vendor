// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/gpio/gpio.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/service.h"

namespace Service::GPIO {

class GPIO final : public ServiceFramework<GPIO> {
public:
    explicit GPIO(Core::System& system_)
        : ServiceFramework{system_, "gpio"}
    {
        // No emulated Switch GPIO controller exists on PS5. Retain the ABI command
        // slots, but reject hardware sessions/queries explicitly instead of
        // manufacturing a successful session, event, or pad state.
        static const FunctionInfo functions[] = {
            {0, &GPIO::HardwareUnavailable, "OpenSessionForDev"},
            {1, &GPIO::HardwareUnavailable, "OpenSession"},
            {2, &GPIO::HardwareUnavailable, "OpenSessionForTest"},
            {3, &GPIO::HardwareUnavailable, "IsWakeEventActive"},
            {4, &GPIO::HardwareUnavailable, "GetWakeEventActiveFlagSet"},
            {5, &GPIO::HardwareUnavailable, "SetWakeEventActiveFlagSetForDebug"},
            {6, &GPIO::HardwareUnavailable, "SetWakePinDebugMode"},
            {7, &GPIO::HardwareUnavailable, "OpenSession2"},
            {8, &GPIO::HardwareUnavailable, "IsWakeEventActive2"},
            {9, &GPIO::HardwareUnavailable, "SetWakeEventActiveFlagSetForDebug2"},
        };
        RegisterHandlers(functions);
    }
    ~GPIO() override = default;

private:
    void HardwareUnavailable(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultNotImplemented);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    server_manager->RegisterNamedService("gpio", std::make_shared<GPIO>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::GPIO
