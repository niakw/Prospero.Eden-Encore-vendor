// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/hle/service/caps/caps_c.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_result.h"
#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IAlbumControlService::IAlbumControlService(Core::System& system_,
                                           std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:c"}, manager{album_manager} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, nullptr, "CaptureRawImage"},
        {2, nullptr, "CaptureRawImageWithTimeout"},
        {33, C<&IAlbumControlService::SetShimLibraryVersion>, "SetShimLibraryVersion"},
        {1001, nullptr, "RequestTakingScreenShot"},
        {1002, nullptr, "RequestTakingScreenShotWithTimeout"},
        {1011, nullptr, "NotifyTakingScreenShotRefused"},
        {2001, D<&IAlbumControlService::NotifyAlbumStorageIsAvailable>, "NotifyAlbumStorageIsAvailable"},
        {2002, D<&IAlbumControlService::NotifyAlbumStorageIsUnavailable>, "NotifyAlbumStorageIsUnavailable"},
        {2003, &IAlbumControlService::RejectUnavailableMovieBackend, "GetAlbumMovieReadStreamMovieDataSize"},
        {2004, &IAlbumControlService::RejectUnavailableMovieBackend, "ReadMovieDataFromAlbumMovieReadStream"},
        {2005, &IAlbumControlService::RejectUnavailableMovieBackend, "GetAlbumMovieReadStreamBrokenReason"},
        {2006, &IAlbumControlService::RejectUnavailableMovieBackend, "GetAlbumMovieReadStreamImageDataSize"},
        {2007, &IAlbumControlService::RejectUnavailableMovieBackend, "ReadImageDataFromAlbumMovieReadStream"},
        {2008, &IAlbumControlService::RejectUnavailableMovieBackend, "ReadFileAttributeFromAlbumMovieReadStream"},
        {2011, D<&IAlbumControlService::RegisterAppletResourceUserId>, "RegisterAppletResourceUserId"},
        {2012, D<&IAlbumControlService::UnregisterAppletResourceUserId>, "UnregisterAppletResourceUserId"},
        {2013, D<&IAlbumControlService::GetApplicationIdFromAruid>, "GetApplicationIdFromAruid"},
        {2014, D<&IAlbumControlService::CheckApplicationIdRegistered>, "CheckApplicationIdRegistered"},
        {2101, nullptr, "GenerateCurrentAlbumFileId"},
        {2102, nullptr, "GenerateApplicationAlbumEntry"},
        {2201, nullptr, "SaveAlbumScreenShotFile"},
        {2202, nullptr, "SaveAlbumScreenShotFileEx"},
        {2301, nullptr, "SetOverlayScreenShotThumbnailData"},
        {2302, &IAlbumControlService::RejectUnavailableMovieBackend, "SetOverlayMovieThumbnailData"},
        // Album movie-write streams are unsupported; never report fake success.
        {2401, &IAlbumControlService::RejectUnavailableMovieBackend, "OpenAlbumMovieWriteStream"},
        {2402, &IAlbumControlService::RejectUnavailableMovieBackend, "FinishAlbumMovieWriteStream"},
        {2403, &IAlbumControlService::RejectUnavailableMovieBackend, "CommitAlbumMovieWriteStream"},
        {2404, &IAlbumControlService::RejectUnavailableMovieBackend, "DiscardAlbumMovieWriteStream"},
        {2405, &IAlbumControlService::RejectUnavailableMovieBackend, "DiscardAlbumMovieWriteStreamNoDelete"},
        {2411, &IAlbumControlService::RejectUnavailableMovieBackend, "StartAlbumMovieWriteStreamDataSection"},
        {2412, &IAlbumControlService::RejectUnavailableMovieBackend, "EndAlbumMovieWriteStreamDataSection"},
        {2413, &IAlbumControlService::RejectUnavailableMovieBackend, "StartAlbumMovieWriteStreamMetaSection"},
        {2414, &IAlbumControlService::RejectUnavailableMovieBackend, "EndAlbumMovieWriteStreamMetaSection"},
        {2421, &IAlbumControlService::RejectUnavailableMovieBackend, "ReadDataFromAlbumMovieWriteStream"},
        {2422, &IAlbumControlService::RejectUnavailableMovieBackend, "WriteDataToAlbumMovieWriteStream"},
        {2424, &IAlbumControlService::RejectUnavailableMovieBackend, "WriteMetaToAlbumMovieWriteStream"},
        {2431, &IAlbumControlService::RejectUnavailableMovieBackend, "GetAlbumMovieWriteStreamBrokenReason"},
        {2433, &IAlbumControlService::RejectUnavailableMovieBackend, "GetAlbumMovieWriteStreamDataSize"},
        {2434, &IAlbumControlService::RejectUnavailableMovieBackend, "SetAlbumMovieWriteStreamDataSize"},
        {60001, nullptr, "OpenControlSession"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAlbumControlService::~IAlbumControlService() = default;

void IAlbumControlService::RejectUnavailableMovieBackend(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result IAlbumControlService::SetShimLibraryVersion(ShimLibraryVersion library_version,
                                                   ClientAppletResourceUserId aruid) {
    // The shim version is a client compatibility hint. Encore does not branch album/screenshot
    // behavior on it, so consuming it successfully is an intentional no-op policy.
    LOG_DEBUG(Service_Capture, "called. library_version={}, applet_resource_user_id={}",
              library_version, aruid.pid);
    R_SUCCEED();
}

Result IAlbumControlService::NotifyAlbumStorageIsAvailable(AlbumStorage storage) {
    LOG_DEBUG(Service_Capture, "called, storage={}", storage);
    R_RETURN(manager->SetAlbumStorageAvailable(storage, true));
}

Result IAlbumControlService::NotifyAlbumStorageIsUnavailable(AlbumStorage storage) {
    LOG_DEBUG(Service_Capture, "called, storage={}", storage);
    R_RETURN(manager->SetAlbumStorageAvailable(storage, false));
}

Result IAlbumControlService::RegisterAppletResourceUserId(
    u64 aruid, const ControlApplicationId& application_id) {
    registered_applications[aruid] = application_id;
    LOG_DEBUG(Service_Capture, "called, aruid={:#x}, application_id={:#x}", aruid,
              application_id.application_id);
    R_SUCCEED();
}

Result IAlbumControlService::UnregisterAppletResourceUserId(
    u64 aruid, const ControlApplicationId& application_id) {
    const auto it = registered_applications.find(aruid);
    if (it != registered_applications.end() &&
        it->second.application_id == application_id.application_id) {
        registered_applications.erase(it);
    }
    LOG_DEBUG(Service_Capture, "called, aruid={:#x}, application_id={:#x}", aruid,
              application_id.application_id);
    R_SUCCEED();
}

Result IAlbumControlService::GetApplicationIdFromAruid(
    Out<ControlApplicationId> out_application_id, u64 aruid) {
    if (const auto it = registered_applications.find(aruid); it != registered_applications.end()) {
        *out_application_id = it->second;
    } else {
        // The core already tracks the title owning an ARUID; use it as the authoritative fallback
        // when capture registration happened before caps:c was opened.
        *out_application_id = {};
        out_application_id->application_id = system.ResolveCallerProgramId(aruid);
    }
    R_SUCCEED();
}

Result IAlbumControlService::CheckApplicationIdRegistered(u64 application_id) {
    for (const auto& [aruid, registered] : registered_applications) {
        if (registered.application_id == application_id) {
            R_SUCCEED();
        }
    }
    R_RETURN(ResultControlNotOpened);
}

} // namespace Service::Capture
