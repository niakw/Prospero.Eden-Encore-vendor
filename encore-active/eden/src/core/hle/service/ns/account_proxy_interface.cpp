// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/account_proxy_interface.h"

namespace Service::NS {

IAccountProxyInterface::IAccountProxyInterface(Core::System& system_)
    : ServiceFramework{system_, "IAccountProxyInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IAccountProxyInterface::RejectUnsupportedAccountMutation, "CreateUserAccount"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IAccountProxyInterface::RejectUnsupportedAccountMutation(HLERequestContext& ctx) {
    // Encore has no account-creation backend. Never report a fictitious user mutation.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IAccountProxyInterface::~IAccountProxyInterface() = default;

} // namespace Service::NS
