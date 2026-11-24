// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/caps/caps_sc.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IScreenShotControlService::IScreenShotControlService(Core::System& system_)
    : ServiceFramework{system_, "caps:sc"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, &IScreenShotControlService::RejectUnavailableScreenShotControl, "CaptureRawImage"},
        {2, &IScreenShotControlService::RejectUnavailableScreenShotControl, "CaptureRawImageWithTimeout"},
        {3, &IScreenShotControlService::RejectUnavailableScreenShotControl, "AttachSharedBuffer"},
        {5, &IScreenShotControlService::RejectUnavailableScreenShotControl, "CaptureRawImageToAttachedSharedBuffer"},
        {210, &IScreenShotControlService::RejectUnavailableScreenShotControl, "Unknown210"},
        {1001, &IScreenShotControlService::RejectUnavailableScreenShotControl, "RequestTakingScreenShot"},
        {1002, &IScreenShotControlService::RejectUnavailableScreenShotControl, "RequestTakingScreenShotWithTimeout"},
        {1003, &IScreenShotControlService::RejectUnavailableScreenShotControl, "RequestTakingScreenShotEx"},
        {1004, &IScreenShotControlService::RejectUnavailableScreenShotControl, "RequestTakingScreenShotEx1"},
        {1009, &IScreenShotControlService::RejectUnavailableScreenShotControl, "CancelTakingScreenShot"},
        {1010, &IScreenShotControlService::RejectUnavailableScreenShotControl, "SetTakingScreenShotCancelState"},
        {1011, &IScreenShotControlService::RejectUnavailableScreenShotControl, "NotifyTakingScreenShotRefused"},
        {1012, &IScreenShotControlService::RejectUnavailableScreenShotControl, "NotifyTakingScreenShotFailed"},
        {1101, &IScreenShotControlService::RejectUnavailableScreenShotControl, "SetupOverlayMovieThumbnail"},
        {1106, &IScreenShotControlService::RejectUnavailableScreenShotControl, "Unknown1106"},
        {1107, &IScreenShotControlService::RejectUnavailableScreenShotControl, "Unknown1107"},
        {1201, &IScreenShotControlService::RejectUnavailableScreenShotControl, "OpenRawScreenShotReadStream"},
        {1202, &IScreenShotControlService::RejectUnavailableScreenShotControl, "CloseRawScreenShotReadStream"},
        {1203, &IScreenShotControlService::RejectUnavailableScreenShotControl, "ReadRawScreenShotReadStream"},
        {1204, &IScreenShotControlService::RejectUnavailableScreenShotControl, "Unknown1204"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IScreenShotControlService::RejectUnavailableScreenShotControl(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IScreenShotControlService::~IScreenShotControlService() = default;

} // namespace Service::Capture
