// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "common/container/unordered_map.h"
#include <concepts>

#include "core/hle/kernel/k_port.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/result.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KClientPort;
class KClientSession;
class KernelCore;
class KPort;
class SessionRequestHandler;
} // namespace Kernel

namespace Service::SM {

class Controller;

/// Interface to "sm:" service
class SM final : public ServiceFramework<SM> {
public:
    explicit SM(ServiceManager& service_manager_, Core::System& system_);
    ~SM() override;

private:
    void Initialize(HLERequestContext& ctx);
    void GetServiceCmif(HLERequestContext& ctx);
    void GetServiceTipc(HLERequestContext& ctx);
    void RegisterServiceCmif(HLERequestContext& ctx);
    void RegisterServiceTipc(HLERequestContext& ctx);
    void UnregisterService(HLERequestContext& ctx);
    void AtmosphereHasService(HLERequestContext& ctx);

    Result GetServiceImpl(Kernel::KClientSession** out_client_session, HLERequestContext& ctx);
    void RegisterServiceImpl(HLERequestContext& ctx, std::string name, u32 max_session_count,
                             bool is_light);

    ServiceManager& service_manager;
    Kernel::KernelCore& kernel;
};

class ServiceManager {
public:
    explicit ServiceManager(Kernel::KernelCore& kernel_);
    ~ServiceManager();

    Result RegisterService(Kernel::KServerPort** out_server_port, std::string name,
                           u32 max_sessions, SessionRequestHandlerFactory handler_factory);
    Result UnregisterService(const std::string& name);
    Result GetServicePort(Kernel::KClientPort** out_client_port, const std::string& name);

    template <std::derived_from<SessionRequestHandler> T>
    std::shared_ptr<T> GetService(const std::string& name, bool block = false) const {
        using namespace std::literals::chrono_literals;
        while (true) {
            SessionRequestHandlerFactory factory;
            {
                std::scoped_lock l{lock};
                const auto it = registered_services.find(name);
                if (it != registered_services.end()) {
                    factory = it->second;
                }
            }
            if (factory) {
                return std::static_pointer_cast<T>(factory());
            }
            if (!block) {
                LOG_DEBUG(Service, "Can't find service: {}", name);
                return nullptr;
            }

            // A host HLE worker is represented by a dummy KThread. Calling the guest
            // SleepThread SVC from that host worker can park service initialization itself.
            if (Kernel::GetCurrentThread(kernel).IsDummyThread()) {
                std::this_thread::sleep_for(1ms);
            } else {
                Kernel::Svc::SleepThread(
                    kernel.System(),
                    std::chrono::duration_cast<std::chrono::nanoseconds>(100ms).count());
            }
        }
    }

    void InvokeControlRequest(HLERequestContext& context);

    void SetDeferralEvent(Kernel::KEvent* deferral_event_) {
        deferral_event = deferral_event_;
    }

private:
    std::shared_ptr<SM> sm_interface;
    std::unique_ptr<Controller> controller_interface;

    /// Map of registered services, retrieved using GetServicePort.
    mutable std::mutex lock;
    ::Common::unordered_map<std::string, SessionRequestHandlerFactory> registered_services;
    ::Common::unordered_map<std::string, Kernel::KClientPort*> service_ports;

    /// Kernel context
    Kernel::KernelCore& kernel;
    Kernel::KEvent* deferral_event{};
};

/// Runs SM services.
void LoopProcess(Core::System& system);

} // namespace Service::SM
