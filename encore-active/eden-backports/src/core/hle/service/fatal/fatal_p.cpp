// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/fatal/fatal_p.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Fatal {

Fatal_P::Fatal_P(std::shared_ptr<Module> module_, Core::System& system_)
    : Interface(std::move(module_), system_, "fatal:p") {
    static const FunctionInfo functions[] = {
        {0, &Fatal_P::RejectUnavailableFatalProcessState, "GetFatalEvent"},
        {10, &Fatal_P::RejectUnavailableFatalProcessState, "GetFatalContext"},
    };
    RegisterHandlers(functions);
}

void Fatal_P::RejectUnavailableFatalProcessState(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Fatal_P::~Fatal_P() = default;

} // namespace Service::Fatal
