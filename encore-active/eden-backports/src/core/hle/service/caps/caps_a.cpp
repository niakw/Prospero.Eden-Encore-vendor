// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/hle/service/caps/caps_a.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_result.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IAlbumAccessorService::IAlbumAccessorService(Core::System& system_,
                                             std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:a"}, manager{album_manager} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAlbumAccessorService::GetAlbumFileCount>, "GetAlbumFileCount"},
        {1, C<&IAlbumAccessorService::GetAlbumFileList>, "GetAlbumFileList"},
        {2, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "LoadAlbumFile"},
        {3, C<&IAlbumAccessorService::DeleteAlbumFile>, "DeleteAlbumFile"},
        {4, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "StorageCopyAlbumFile"},
        {5, C<&IAlbumAccessorService::IsAlbumMounted>, "IsAlbumMounted"},
        {6, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "GetAlbumUsage"},
        {7, D<&IAlbumAccessorService::GetAlbumFileSize>, "GetAlbumFileSize"},
        {8, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "LoadAlbumFileThumbnail"},
        {9, D<&IAlbumAccessorService::LoadAlbumScreenShotImage>, "LoadAlbumScreenShotImage"},
        {10, D<&IAlbumAccessorService::LoadAlbumScreenShotThumbnailImage>, "LoadAlbumScreenShotThumbnailImage"},
        {11, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "GetAlbumEntryFromApplicationAlbumEntry"},
        {12, D<&IAlbumAccessorService::LoadAlbumScreenShotImageEx>, "LoadAlbumScreenShotImageEx"},
        {13, D<&IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx>, "LoadAlbumScreenShotThumbnailImageEx"},
        {14, D<&IAlbumAccessorService::LoadAlbumScreenShotImageEx0>, "LoadAlbumScreenShotImageEx0"},
        {15, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "GetAlbumUsage3"},
        {16, D<&IAlbumAccessorService::GetAlbumMountResult>, "GetAlbumMountResult"},
        {17, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "GetAlbumUsage16"},
        {18, C<&IAlbumAccessorService::Unknown18>, "Unknown18"},
        {19, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "Unknown19"},
        {100, D<&IAlbumAccessorService::GetAlbumFileCountEx0>, "GetAlbumFileCountEx0"},
        {101, C<&IAlbumAccessorService::GetAlbumFileListEx0>, "GetAlbumFileListEx0"},
        {202, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "SaveEditedScreenShot"},
        {301, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "GetLastThumbnail"},
        {302, &IAlbumAccessorService::RejectUnavailableMovieBackend, "GetLastOverlayMovieThumbnail"},
        {401,  C<&IAlbumAccessorService::GetAutoSavingStorage>, "GetAutoSavingStorage"},
        {501, D<&IAlbumAccessorService::GetRequiredStorageSpaceSizeToCopyAll>, "GetRequiredStorageSpaceSizeToCopyAll"},
        {1001, D<&IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx0>, "LoadAlbumScreenShotThumbnailImageEx0"},
        {1002, C<&IAlbumAccessorService::LoadAlbumScreenShotImageEx1>, "LoadAlbumScreenShotImageEx1"},
        {1003, C<&IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx1>, "LoadAlbumScreenShotThumbnailImageEx1"},
        // Movie album streams are not backed by Encore: reject explicitly.
        {2001, &IAlbumAccessorService::RejectUnavailableMovieBackend, "OpenAlbumMovieReadStream"},
        {2002, &IAlbumAccessorService::RejectUnavailableMovieBackend, "CloseAlbumMovieReadStream"},
        {2003, &IAlbumAccessorService::RejectUnavailableMovieBackend, "GetAlbumMovieReadStreamMovieDataSize"},
        {2004, &IAlbumAccessorService::RejectUnavailableMovieBackend, "ReadMovieDataFromAlbumMovieReadStream"},
        {2005, &IAlbumAccessorService::RejectUnavailableMovieBackend, "GetAlbumMovieReadStreamBrokenReason"},
        {2006, &IAlbumAccessorService::RejectUnavailableMovieBackend, "GetAlbumMovieReadStreamImageDataSize"},
        {2007, &IAlbumAccessorService::RejectUnavailableMovieBackend, "ReadImageDataFromAlbumMovieReadStream"},
        {2008, &IAlbumAccessorService::RejectUnavailableMovieBackend, "ReadFileAttributeFromAlbumMovieReadStream"},
        {8001, D<&IAlbumAccessorService::ForceAlbumUnmounted>, "ForceAlbumUnmounted"},
        {8002, D<&IAlbumAccessorService::ResetAlbumMountStatus>, "ResetAlbumMountStatus"},
        {8011, D<&IAlbumAccessorService::RefreshAlbumCache>, "RefreshAlbumCache"},
        {8012, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "GetAlbumCache"},
        {8013, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "GetAlbumCacheEx"},
        {8021, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "GetAlbumEntryFromApplicationAlbumEntryAruid"},
        {10011, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "SetInternalErrorConversionEnabled"},
        {50000, &IAlbumAccessorService::UnsupportedDebugCommand, "LoadMakerNoteInfoForDebug"},
        {50011, C<&IAlbumAccessorService::GetAlbumAccessResultForDebug>, "GetAlbumAccessResultForDebug"},
        {60002, &IAlbumAccessorService::RejectUnavailableCaptureCommand, "OpenAccessorSession"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAlbumAccessorService::~IAlbumAccessorService() = default;

void IAlbumAccessorService::RejectUnavailableCaptureCommand(HLERequestContext& ctx) {
    LOG_WARNING(Service_Capture, "unsupported album/capture command {}", ctx.GetCommand());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

void IAlbumAccessorService::RejectUnavailableMovieBackend(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result IAlbumAccessorService::GetAlbumFileCount(Out<u64> out_count, AlbumStorage storage) {
    R_RETURN(TranslateResult(manager->GetAlbumFileCount(*out_count, storage, 0)));
}

Result IAlbumAccessorService::GetAlbumFileSize(Out<u64> out_size, const AlbumFileId& file_id) {
    R_RETURN(TranslateResult(manager->GetAlbumFileSize(*out_size, file_id)));
}

Result IAlbumAccessorService::GetAlbumMountResult(AlbumStorage storage) {
    R_RETURN(TranslateResult(manager->GetAlbumMountResult(storage)));
}

Result IAlbumAccessorService::GetAlbumFileCountEx0(
    Out<u64> out_count, const AlbumFileCountEx0Input& input) {
    R_RETURN(TranslateResult(manager->GetAlbumFileCount(*out_count, input.storage, input.flags)));
}

Result IAlbumAccessorService::ForceAlbumUnmounted(AlbumStorage storage) {
    R_RETURN(TranslateResult(manager->SetAlbumStorageAvailable(storage, false)));
}

Result IAlbumAccessorService::ResetAlbumMountStatus(AlbumStorage storage) {
    R_RETURN(TranslateResult(manager->SetAlbumStorageAvailable(storage, true)));
}

Result IAlbumAccessorService::RefreshAlbumCache(AlbumStorage storage) {
    R_RETURN(TranslateResult(manager->RefreshAlbumCache(storage)));
}

Result IAlbumAccessorService::LoadAlbumScreenShotImage(
    Out<u64> out_width, Out<u64> out_height, const AlbumFileId& file_id,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LoadAlbumScreenShotImageOutput output{};
    const Result result =
        manager->LoadAlbumScreenShotImage(output, out_image, file_id, ScreenShotDecodeOption{});
    *out_width = static_cast<u64>(output.width);
    *out_height = static_cast<u64>(output.height);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotThumbnailImage(
    Out<u64> out_width, Out<u64> out_height, const AlbumFileId& file_id,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LoadAlbumScreenShotImageOutput output{};
    const Result result =
        manager->LoadAlbumScreenShotThumbnail(output, out_image, file_id, ScreenShotDecodeOption{});
    *out_width = static_cast<u64>(output.width);
    *out_height = static_cast<u64>(output.height);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotImageEx(
    Out<u64> out_width, Out<u64> out_height, const AlbumFileId& file_id,
    const ScreenShotDecodeOption& decoder_options,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LoadAlbumScreenShotImageOutput output{};
    const Result result =
        manager->LoadAlbumScreenShotImage(output, out_image, file_id, decoder_options);
    *out_width = static_cast<u64>(output.width);
    *out_height = static_cast<u64>(output.height);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx(
    Out<u64> out_width, Out<u64> out_height, const AlbumFileId& file_id,
    const ScreenShotDecodeOption& decoder_options,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LoadAlbumScreenShotImageOutput output{};
    const Result result =
        manager->LoadAlbumScreenShotThumbnail(output, out_image, file_id, decoder_options);
    *out_width = static_cast<u64>(output.width);
    *out_height = static_cast<u64>(output.height);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::GetAlbumFileList(
    Out<u64> out_count, AlbumStorage storage,
    OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_INFO(Service_Capture, "called, storage={}", storage);

    const Result result = manager->GetAlbumFileList(out_entries, *out_count, storage, 0);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::DeleteAlbumFile(AlbumFileId file_id) {
    LOG_INFO(Service_Capture, "called, application_id={:#0x}, storage={}, type={}",
             file_id.application_id, file_id.storage, file_id.type);

    const Result result = manager->DeleteAlbumFile(file_id);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::IsAlbumMounted(Out<bool> out_is_mounted, AlbumStorage storage) {
    LOG_INFO(Service_Capture, "called, storage={}", storage);

    const Result result = manager->IsAlbumMounted(storage);
    *out_is_mounted = result.IsSuccess();
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::Unknown18(
    Out<u32> out_buffer_size,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_buffer) {
    LOG_WARNING(Service_Capture, "(STUBBED) called");
    *out_buffer_size = 0;
    R_SUCCEED();
}

Result IAlbumAccessorService::GetAlbumFileListEx0(
    Out<u64> out_entries_size, AlbumStorage storage, u8 flags,
    OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_INFO(Service_Capture, "called, storage={}, flags={}", storage, flags);

    const Result result = manager->GetAlbumFileList(out_entries, *out_entries_size, storage, flags);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::GetAutoSavingStorage(Out<bool> out_is_autosaving) {
    LOG_DEBUG(Service_Capture, "called");

    const Result result = manager->GetAutoSavingStorage(*out_is_autosaving);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotImageEx0(
    Out<ScreenShotAttribute> out_attribute, Out<s64> out_width, Out<s64> out_height,
    const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LoadAlbumScreenShotImageOutput output{};
    const Result result =
        manager->LoadAlbumScreenShotImage(output, out_image, file_id, decoder_options);
    if (result.IsSuccess()) {
        *out_attribute = output.attribute;
        *out_width = output.width;
        *out_height = output.height;
        std::fill(out_buffer.begin(), out_buffer.end(), 0);
    }
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx0(
    Out<ScreenShotAttribute> out_attribute, Out<s64> out_width, Out<s64> out_height,
    const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LoadAlbumScreenShotImageOutput output{};
    const Result result =
        manager->LoadAlbumScreenShotThumbnail(output, out_image, file_id, decoder_options);
    if (result.IsSuccess()) {
        *out_attribute = output.attribute;
        *out_width = output.width;
        *out_height = output.height;
        std::fill(out_buffer.begin(), out_buffer.end(), 0);
    }
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::GetRequiredStorageSpaceSizeToCopyAll(
    Out<u64> out_size, const RequiredStorageSpaceInput& input) {
    R_RETURN(TranslateResult(
        manager->GetRequiredStorageSpaceSizeToCopyAll(*out_size, input.source, input.destination)));
}

Result IAlbumAccessorService::LoadAlbumScreenShotImageEx1(
    const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
    OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_INFO(Service_Capture, "called, application_id={:#0x}, storage={}, type={}, flags={}",
             file_id.application_id, file_id.storage, file_id.type, decoder_options.flags);

    const Result result =
        manager->LoadAlbumScreenShotImage(*out_image_output, out_image, file_id, decoder_options);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx1(
    const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
    OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_INFO(Service_Capture, "called, application_id={:#0x}, storage={}, type={}, flags={}",
             file_id.application_id, file_id.storage, file_id.type, decoder_options.flags);

    const Result result = manager->LoadAlbumScreenShotThumbnail(*out_image_output, out_image,
                                                                file_id, decoder_options);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::TranslateResult(Result in_result) {
    if (in_result.IsSuccess()) {
        return in_result;
    }

    if ((in_result.raw & 0x3801ff) == ResultUnknown1024.raw) {
        if (in_result.GetDescription() - 0x514 < 100) {
            return ResultInvalidFileData;
        }
        if (in_result.GetDescription() - 0x5dc < 100) {
            return ResultInvalidFileData;
        }

        if (in_result.GetDescription() - 0x578 < 100) {
            if (in_result == ResultFileCountLimit) {
                return ResultUnknown22;
            }
            return ResultUnknown25;
        }

        if (in_result.raw < ResultUnknown1801.raw) {
            if (in_result == ResultUnknown1202) {
                return ResultUnknown810;
            }
            if (in_result == ResultUnknown1203) {
                return ResultUnknown810;
            }
            if (in_result == ResultUnknown1701) {
                return ResultUnknown5;
            }
        } else if (in_result.raw < ResultUnknown1803.raw) {
            if (in_result == ResultUnknown1801) {
                return ResultUnknown5;
            }
            if (in_result == ResultUnknown1802) {
                return ResultUnknown6;
            }
        } else {
            if (in_result == ResultUnknown1803) {
                return ResultUnknown7;
            }
            if (in_result == ResultUnknown1804) {
                return ResultOutOfRange;
            }
        }
        return ResultUnknown1024;
    }

    if (in_result.GetModule() == ErrorModule::FS) {
        if ((in_result.GetDescription() >> 0xc < 0x7d) ||
            (in_result.GetDescription() - 1000 < 2000) ||
            (((in_result.GetDescription() - 3000) >> 3) < 0x271)) {
            // TODO: Translate FS error
            return in_result;
        }
    }

    return in_result;
}


void IAlbumAccessorService::UnsupportedDebugCommand(HLERequestContext& ctx) {
    LOG_WARNING(Service_Capture, "album maker-note debug loading is not supported");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result IAlbumAccessorService::GetAlbumAccessResultForDebug(Out<Result> out_result) {
    LOG_WARNING(Service_Capture, "(STUBBED) called");
    *out_result = ResultSuccess;
    R_SUCCEED();
}

} // namespace Service::Capture
