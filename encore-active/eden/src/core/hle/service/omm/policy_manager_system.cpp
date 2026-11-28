// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/omm/policy_manager_system.h"

namespace Service::OMM {

IPolicyManagerSystem::IPolicyManagerSystem(Core::System& system_)
    : ServiceFramework{system_, "idle:sys"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IPolicyManagerSystem::GetAutoPowerDownEvent, "GetAutoPowerDownEvent"},
        {1, &IPolicyManagerSystem::IsAutoPowerDownRequested, "IsAutoPowerDownRequested"},
        {2, &IPolicyManagerSystem::RejectUnknownPolicyCommand, "Unknown2"},
        {3, &IPolicyManagerSystem::AcceptPolicyNoop, "SetHandlingContext"},
        {4, &IPolicyManagerSystem::AcceptPolicyNoop, "LoadAndApplySettings"},
        {5, &IPolicyManagerSystem::AcceptPolicyNoop, "ReportUserIsActive"},
    };
    // clang-format on

    RegisterHandlers(functions);
    auto_power_down_event = service_context.CreateEvent("IPolicyManagerSystem::AutoPowerDownEvent");
}

IPolicyManagerSystem::~IPolicyManagerSystem() {
    service_context.CloseEvent(auto_power_down_event);
}

void IPolicyManagerSystem::GetAutoPowerDownEvent(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, auto_power_down_event->GetReadableEvent());
}

void IPolicyManagerSystem::IsAutoPowerDownRequested(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(false);
}

void IPolicyManagerSystem::AcceptPolicyNoop(HLERequestContext& ctx) {
    // Encore does not run Horizon's automatic guest sleep timer. Applying idle
    // settings or reporting guest activity therefore leaves the host PS5 untouched.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IPolicyManagerSystem::RejectUnknownPolicyCommand(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

} // namespace Service::OMM
