// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>

#include "common/logging.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/caps/caps_u.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IAlbumApplicationService::IAlbumApplicationService(Core::System& system_,
                                                   std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:u"}, manager{album_manager} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {32, C<&IAlbumApplicationService::SetShimLibraryVersion>, "SetShimLibraryVersion"},
        {102, C<&IAlbumApplicationService::GetAlbumFileList0AafeAruidDeprecated>, "GetAlbumFileList0AafeAruidDeprecated"},
        {103, C<&IAlbumApplicationService::DeleteAlbumFileByAruid>, "DeleteAlbumFileByAruid"},
        {104, C<&IAlbumApplicationService::GetAlbumFileSizeByAruid>, "GetAlbumFileSizeByAruid"},
        {105, C<&IAlbumApplicationService::DeleteAlbumFileByAruidForDebug>, "DeleteAlbumFileByAruidForDebug"},
        {110, C<&IAlbumApplicationService::LoadAlbumScreenShotImageByAruid>, "LoadAlbumScreenShotImageByAruid"},
        {120, C<&IAlbumApplicationService::LoadAlbumScreenShotThumbnailImageByAruid>, "LoadAlbumScreenShotThumbnailImageByAruid"},
        {130, &IAlbumApplicationService::RejectUnavailableAlbumVariant, "PrecheckToCreateContentsByAruid"},
        {140, C<&IAlbumApplicationService::GetAlbumFileList1AafeAruidDeprecated>, "GetAlbumFileList1AafeAruidDeprecated"},
        {141, &IAlbumApplicationService::RejectUnavailableAlbumVariant, "GetAlbumFileList2AafeUidAruidDeprecated"},
        {142, C<&IAlbumApplicationService::GetAlbumFileList3AaeAruid>, "GetAlbumFileList3AaeAruid"},
        {143, &IAlbumApplicationService::RejectUnavailableAlbumVariant, "GetAlbumFileList4AaeUidAruid"},
        {144, &IAlbumApplicationService::RejectUnavailableAlbumVariant, "GetAllAlbumFileList3AaeAruid"},
        // No movie-read stream is created by the PS5 album backend.
        {2001, &IAlbumApplicationService::RejectUnavailableMovieOrSessionBackend, "OpenAlbumMovieReadStream"},
        {2002, &IAlbumApplicationService::RejectUnavailableMovieOrSessionBackend, "CloseAlbumMovieReadStream"},
        {2003, &IAlbumApplicationService::RejectUnavailableMovieOrSessionBackend, "GetAlbumMovieReadStreamMovieDataSize"},
        {2004, &IAlbumApplicationService::RejectUnavailableMovieOrSessionBackend, "ReadMovieDataFromAlbumMovieReadStream"},
        {2005, &IAlbumApplicationService::RejectUnavailableMovieOrSessionBackend, "GetAlbumMovieReadStreamBrokenReason"},
        {60002, &IAlbumApplicationService::RejectUnavailableMovieOrSessionBackend, "OpenAccessorSessionForApplication"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IAlbumApplicationService::RejectUnavailableAlbumVariant(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IAlbumApplicationService::~IAlbumApplicationService() = default;

void IAlbumApplicationService::RejectUnavailableMovieOrSessionBackend(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result IAlbumApplicationService::SetShimLibraryVersion(ShimLibraryVersion library_version,
                                                       ClientAppletResourceUserId aruid) {
    // The shim version is a client compatibility hint. Encore does not branch album/screenshot
    // behavior on it, so consuming it successfully is an intentional no-op policy.
    LOG_DEBUG(Service_Capture, "called. library_version={}, applet_resource_user_id={}",
              library_version, aruid.pid);
    R_SUCCEED();
}

Result IAlbumApplicationService::GetAlbumFileList0AafeAruidDeprecated(
    Out<u64> out_entries_count, ContentType content_type, s64 start_posix_time, s64 end_posix_time,
    ClientAppletResourceUserId aruid,
    OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_WARNING(Service_Capture,
                "(STUBBED) called. content_type={}, start_posix_time={}, end_posix_time={}, "
                "applet_resource_user_id={}",
                content_type, start_posix_time, end_posix_time, aruid.pid);

    R_TRY(manager->IsAlbumMounted(AlbumStorage::Sd));
    R_RETURN(manager->GetAlbumFileList(out_entries, *out_entries_count, content_type,
                                       start_posix_time, end_posix_time, aruid.pid));
}

AlbumFileId IAlbumApplicationService::MakeAlbumFileId(
    const ApplicationAlbumFileEntry& file_entry, ClientAppletResourceUserId aruid,
    ContentType content_type) const {
    return {
        .application_id = system.ResolveCallerProgramId(aruid.pid),
        .date = file_entry.datetime,
        .storage = file_entry.entry.storage,
        .type = content_type,
        .unknown = file_entry.entry.unknown,
    };
}

Result IAlbumApplicationService::DeleteAlbumFileByAruid(
    ContentType content_type, const ApplicationAlbumFileEntry& file_entry,
    ClientAppletResourceUserId aruid) {
    const auto file_id = MakeAlbumFileId(file_entry, aruid, content_type);
    R_RETURN(manager->DeleteAlbumFile(file_id));
}

Result IAlbumApplicationService::GetAlbumFileSizeByAruid(
    Out<u64> out_size, const ApplicationAlbumFileEntry& file_entry,
    ClientAppletResourceUserId aruid) {
    const auto file_id = MakeAlbumFileId(file_entry, aruid, file_entry.entry.content);
    R_RETURN(manager->GetAlbumFileSize(*out_size, file_id));
}

Result IAlbumApplicationService::DeleteAlbumFileByAruidForDebug(
    const ApplicationAlbumFileEntry& file_entry, ClientAppletResourceUserId aruid) {
    const auto file_id = MakeAlbumFileId(file_entry, aruid, file_entry.entry.content);
    R_RETURN(manager->DeleteAlbumFile(file_id));
}

Result IAlbumApplicationService::LoadAlbumScreenShotImageByAruid(
    const ApplicationAlbumFileEntry& file_entry, const ScreenShotDecodeOption& decoder_options,
    ClientAppletResourceUserId aruid,
    OutLargeData<LoadAlbumScreenShotImageOutputForApplication, BufferAttr_HipcMapAlias>
        out_image_output,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_work_buffer) {
    (void)out_work_buffer;
    const auto file_id = MakeAlbumFileId(file_entry, aruid, file_entry.entry.content);
    LoadAlbumScreenShotImageOutput output{};
    R_TRY(manager->LoadAlbumScreenShotImage(output, out_image, file_id, decoder_options));

    *out_image_output = {};
    out_image_output->width = output.width;
    out_image_output->height = output.height;
    out_image_output->attribute = output.attribute;
    R_SUCCEED();
}

Result IAlbumApplicationService::LoadAlbumScreenShotThumbnailImageByAruid(
    const ApplicationAlbumFileEntry& file_entry, const ScreenShotDecodeOption& decoder_options,
    ClientAppletResourceUserId aruid,
    OutLargeData<LoadAlbumScreenShotImageOutputForApplication, BufferAttr_HipcMapAlias>
        out_image_output,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_work_buffer) {
    const auto file_id = MakeAlbumFileId(file_entry, aruid, file_entry.entry.content);
    LoadAlbumScreenShotImageOutput output{};
    R_TRY(manager->LoadAlbumScreenShotThumbnail(output, out_image, file_id, decoder_options));

    *out_image_output = {};
    out_image_output->width = output.width;
    out_image_output->height = output.height;
    out_image_output->attribute = output.attribute;
    R_SUCCEED();
}

Result IAlbumApplicationService::GetAlbumFileList1AafeAruidDeprecated(
    Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
    AlbumFileDateTime end_date_time, ClientAppletResourceUserId aruid,
    OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries) {
    std::vector<ApplicationAlbumEntry> entries(out_entries.size());
    R_TRY(manager->GetAlbumFileList(entries, *out_entries_count, content_type, start_date_time,
                                    end_date_time, aruid.pid));
    for (size_t i = 0; i < *out_entries_count; ++i) {
        out_entries[i] = {
            .entry = entries[i],
            .datetime = entries[i].datetime,
            .unknown = {},
        };
    }
    R_SUCCEED();
}

Result IAlbumApplicationService::GetAlbumFileList2AafeUidAruidDeprecated(
    Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
    AlbumFileDateTime end_date_time, Common::UUID user_id, ClientAppletResourceUserId aruid,
    OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries) {
    (void)user_id;
    R_RETURN(GetAlbumFileList1AafeAruidDeprecated(out_entries_count, content_type, start_date_time,
                                                  end_date_time, aruid, out_entries));
}

Result IAlbumApplicationService::GetAlbumFileList4AaeUidAruid(
    Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
    AlbumFileDateTime end_date_time, Common::UUID user_id, ClientAppletResourceUserId aruid,
    OutArray<ApplicationAlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    (void)user_id;
    R_RETURN(manager->GetAlbumFileList(out_entries, *out_entries_count, content_type,
                                       start_date_time, end_date_time, aruid.pid));
}

Result IAlbumApplicationService::GetAlbumFileList3AaeAruid(
    Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
    AlbumFileDateTime end_date_time, ClientAppletResourceUserId aruid,
    OutArray<ApplicationAlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_WARNING(Service_Capture,
                "(STUBBED) called. content_type={}, start_date={}/{}/{}, "
                "end_date={}/{}/{}, applet_resource_user_id={}",
                content_type, start_date_time.year, start_date_time.month, start_date_time.day,
                end_date_time.year, end_date_time.month, end_date_time.day, aruid.pid);

    R_TRY(manager->IsAlbumMounted(AlbumStorage::Sd));
    R_RETURN(manager->GetAlbumFileList(out_entries, *out_entries_count, content_type,
                                       start_date_time, end_date_time, aruid.pid));
}

} // namespace Service::Capture
