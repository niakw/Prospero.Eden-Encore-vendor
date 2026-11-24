// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {
class AlbumManager;

class IAlbumApplicationService final : public ServiceFramework<IAlbumApplicationService> {
public:
    explicit IAlbumApplicationService(Core::System& system_,
                                      std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumApplicationService() override;

private:
    void RejectUnavailableMovieOrSessionBackend(HLERequestContext& ctx);
    Result SetShimLibraryVersion(ShimLibraryVersion library_version,
                                 ClientAppletResourceUserId aruid);

    Result GetAlbumFileList0AafeAruidDeprecated(
        Out<u64> out_entries_count, ContentType content_type, s64 start_posix_time,
        s64 end_posix_time, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries);

    Result DeleteAlbumFileByAruid(ContentType content_type,
                                  const ApplicationAlbumFileEntry& file_entry,
                                  ClientAppletResourceUserId aruid);
    Result GetAlbumFileSizeByAruid(Out<u64> out_size,
                                   const ApplicationAlbumFileEntry& file_entry,
                                   ClientAppletResourceUserId aruid);
    Result DeleteAlbumFileByAruidForDebug(const ApplicationAlbumFileEntry& file_entry,
                                          ClientAppletResourceUserId aruid);
    Result LoadAlbumScreenShotImageByAruid(
        const ApplicationAlbumFileEntry& file_entry, const ScreenShotDecodeOption& decoder_options,
        ClientAppletResourceUserId aruid,
        OutLargeData<LoadAlbumScreenShotImageOutputForApplication, BufferAttr_HipcMapAlias>
            out_image_output,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_work_buffer);
    Result LoadAlbumScreenShotThumbnailImageByAruid(
        const ApplicationAlbumFileEntry& file_entry, const ScreenShotDecodeOption& decoder_options,
        ClientAppletResourceUserId aruid,
        OutLargeData<LoadAlbumScreenShotImageOutputForApplication, BufferAttr_HipcMapAlias>
            out_image_output,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_work_buffer);
    Result GetAlbumFileList1AafeAruidDeprecated(
        Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
        AlbumFileDateTime end_date_time, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries);
    Result GetAlbumFileList2AafeUidAruidDeprecated(
        Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
        AlbumFileDateTime end_date_time, Common::UUID user_id, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries);

    Result GetAlbumFileList3AaeAruid(
        Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
        AlbumFileDateTime end_date_time, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumEntry, BufferAttr_HipcMapAlias> out_entries);
    Result GetAlbumFileList4AaeUidAruid(
        Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
        AlbumFileDateTime end_date_time, Common::UUID user_id, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    AlbumFileId MakeAlbumFileId(const ApplicationAlbumFileEntry& file_entry,
                                ClientAppletResourceUserId aruid, ContentType content_type) const;

    std::shared_ptr<AlbumManager> manager = nullptr;
};

} // namespace Service::Capture
