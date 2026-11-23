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
        {2001, nullptr, "NotifyAlbumStorageIsAvailable"},
        {2002, nullptr, "NotifyAlbumStorageIsUnavailable"},
        {2003, nullptr, "GetAlbumMovieReadStreamMovieDataSize"},
        {2004, nullptr, "ReadMovieDataFromAlbumMovieReadStream"},
        {2005, nullptr, "GetAlbumMovieReadStreamBrokenReason"},
        {2006, nullptr, "GetAlbumMovieReadStreamImageDataSize"},
        {2007, nullptr, "ReadImageDataFromAlbumMovieReadStream"},
        {2008, nullptr, "ReadFileAttributeFromAlbumMovieReadStream"},
        {2011, nullptr, "RegisterAppletResourceUserId"},
        {2012, nullptr, "UnregisterAppletResourceUserId"},
        {2013, nullptr, "GetApplicationIdFromAruid"},
        {2014, nullptr, "CheckApplicationIdRegistered"},
        {2101, nullptr, "GenerateCurrentAlbumFileId"},
        {2102, nullptr, "GenerateApplicationAlbumEntry"},
        {2201, nullptr, "SaveAlbumScreenShotFile"},
        {2202, nullptr, "SaveAlbumScreenShotFileEx"},
        {2301, nullptr, "SetOverlayScreenShotThumbnailData"},
        {2302, nullptr, "SetOverlayMovieThumbnailData"},
        // Album movie-write streams are unsupported; never report fake success.
        {2401, nullptr, "OpenAlbumMovieWriteStream"},
        {2402, nullptr, "FinishAlbumMovieWriteStream"},
        {2403, nullptr, "CommitAlbumMovieWriteStream"},
        {2404, nullptr, "DiscardAlbumMovieWriteStream"},
        {2405, nullptr, "DiscardAlbumMovieWriteStreamNoDelete"},
        {2411, nullptr, "StartAlbumMovieWriteStreamDataSection"},
        {2412, nullptr, "EndAlbumMovieWriteStreamDataSection"},
        {2413, nullptr, "StartAlbumMovieWriteStreamMetaSection"},
        {2414, nullptr, "EndAlbumMovieWriteStreamMetaSection"},
        {2421, nullptr, "ReadDataFromAlbumMovieWriteStream"},
        {2422, nullptr, "WriteDataToAlbumMovieWriteStream"},
        {2424, nullptr, "WriteMetaToAlbumMovieWriteStream"},
        {2431, nullptr, "GetAlbumMovieWriteStreamBrokenReason"},
        {2433, nullptr, "GetAlbumMovieWriteStreamDataSize"},
        {2434, nullptr, "SetAlbumMovieWriteStreamDataSize"},
        {60001, nullptr, "OpenControlSession"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAlbumControlService::~IAlbumControlService() = default;

Result IAlbumControlService::SetShimLibraryVersion(ShimLibraryVersion library_version,
                                                   ClientAppletResourceUserId aruid) {
    // The shim version is a client compatibility hint. Encore does not branch album/screenshot
    // behavior on it, so consuming it successfully is an intentional no-op policy.
    LOG_DEBUG(Service_Capture, "called. library_version={}, applet_resource_user_id={}",
              library_version, aruid.pid);
    R_SUCCEED();
}

} // namespace Service::Capture
