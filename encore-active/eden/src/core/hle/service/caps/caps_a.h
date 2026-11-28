// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {
class AlbumManager;

class IAlbumAccessorService final : public ServiceFramework<IAlbumAccessorService> {
public:
    explicit IAlbumAccessorService(Core::System& system_,
                                   std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumAccessorService() override;

private:
    struct AlbumFileCountEx0Input {
        AlbumStorage storage{};
        std::array<u8, 7> pad0{};
        u8 flags{};
        std::array<u8, 7> pad1{};
    };
    static_assert(sizeof(AlbumFileCountEx0Input) == 0x10);

    Result GetAlbumFileCount(Out<u64> out_count, AlbumStorage storage);
    Result GetAlbumFileSize(Out<u64> out_size, const AlbumFileId& file_id);
    Result GetAlbumMountResult(AlbumStorage storage);
    Result GetAlbumFileCountEx0(Out<u64> out_count, const AlbumFileCountEx0Input& input);
    Result ForceAlbumUnmounted(AlbumStorage storage);
    Result ResetAlbumMountStatus(AlbumStorage storage);
    Result RefreshAlbumCache(AlbumStorage storage);

    Result LoadAlbumScreenShotImage(
        Out<u64> out_width, Out<u64> out_height, const AlbumFileId& file_id,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);
    Result LoadAlbumScreenShotThumbnailImage(
        Out<u64> out_width, Out<u64> out_height, const AlbumFileId& file_id,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);
    Result LoadAlbumScreenShotImageEx(
        Out<u64> out_width, Out<u64> out_height, const AlbumFileId& file_id,
        const ScreenShotDecodeOption& decoder_options,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);
    Result LoadAlbumScreenShotThumbnailImageEx(
        Out<u64> out_width, Out<u64> out_height, const AlbumFileId& file_id,
        const ScreenShotDecodeOption& decoder_options,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    Result GetAlbumFileList(Out<u64> out_count, AlbumStorage storage,
                            OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    Result DeleteAlbumFile(AlbumFileId file_id);

    Result IsAlbumMounted(Out<bool> out_is_mounted, AlbumStorage storage);

    Result Unknown18(
        Out<u32> out_buffer_size,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure>
            out_buffer);

    Result GetAlbumFileListEx0(Out<u64> out_entries_size, AlbumStorage storage, u8 flags,
                               OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    Result GetAutoSavingStorage(Out<bool> out_is_autosaving);

    Result LoadAlbumScreenShotImageEx0(
        Out<ScreenShotAttribute> out_attribute, Out<s64> out_width, Out<s64> out_height,
        const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    struct RequiredStorageSpaceInput {
        AlbumStorage destination{};
        AlbumStorage source{};
    };
    static_assert(sizeof(RequiredStorageSpaceInput) == 2);
    Result LoadAlbumScreenShotThumbnailImageEx0(
        Out<ScreenShotAttribute> out_attribute, Out<s64> out_width, Out<s64> out_height,
        const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    void RejectUnavailableCaptureCommand(HLERequestContext& ctx);
    void RejectUnavailableMovieBackend(HLERequestContext& ctx);

    Result GetRequiredStorageSpaceSizeToCopyAll(Out<u64> out_size,
                                                const RequiredStorageSpaceInput& input);

    Result LoadAlbumScreenShotImageEx1(
        const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
        OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    Result LoadAlbumScreenShotThumbnailImageEx1(
        const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
        OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    Result TranslateResult(Result in_result);

    Result GetAlbumAccessResultForDebug(Out<Result> out_result);
    void UnsupportedDebugCommand(HLERequestContext& ctx);

    std::shared_ptr<AlbumManager> manager = nullptr;
};

} // namespace Service::Capture
