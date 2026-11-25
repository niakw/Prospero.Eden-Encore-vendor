// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/final_output_recorder_manager_for_applet.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Audio {

IFinalOutputRecorderManagerForApplet::IFinalOutputRecorderManagerForApplet(Core::System& system_)
    : ServiceFramework{system_, "audrec:a"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IFinalOutputRecorderManagerForApplet::RejectUnavailableRecorderControl, "RequestSuspend"},
        {1, &IFinalOutputRecorderManagerForApplet::RejectUnavailableRecorderControl, "RequestResume"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IFinalOutputRecorderManagerForApplet::RejectUnavailableRecorderControl(HLERequestContext& ctx) {
    // Encore exposes no FinalOutputRecorder device/backend on PS5.
    // Fail closed instead of acknowledging suspend/resume of a recorder that cannot exist.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IFinalOutputRecorderManagerForApplet::~IFinalOutputRecorderManagerForApplet() = default;

} // namespace Service::Audio
