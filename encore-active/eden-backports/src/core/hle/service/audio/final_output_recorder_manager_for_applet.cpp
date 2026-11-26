// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/hle/service/audio/final_output_recorder_manager_for_applet.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Audio {

IFinalOutputRecorderManagerForApplet::IFinalOutputRecorderManagerForApplet(Core::System& system_)
    : ServiceFramework{system_, "audrec:a"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IFinalOutputRecorderManagerForApplet::RejectUnavailableFinalOutputRecorderControl, "RequestSuspend"},
        {1, &IFinalOutputRecorderManagerForApplet::RejectUnavailableFinalOutputRecorderControl, "RequestResume"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IFinalOutputRecorderManagerForApplet::RejectUnavailableFinalOutputRecorderControl(
    HLERequestContext& ctx) {
    LOG_WARNING(Service_Audio, "audrec:a final-output-recorder control is unavailable");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IFinalOutputRecorderManagerForApplet::~IFinalOutputRecorderManagerForApplet() = default;

} // namespace Service::Audio
