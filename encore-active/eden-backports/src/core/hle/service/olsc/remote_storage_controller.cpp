// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/olsc/remote_storage_controller.h"

namespace Service::OLSC {

IRemoteStorageController::IRemoteStorageController(Core::System& system_)
    : ServiceFramework{system_, "IRemoteStorageController"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IRemoteStorageController::RejectUnavailableCloudBackend, "GetSaveDataArchiveInfoBySaveDataId"},
        {1, &IRemoteStorageController::RejectUnavailableCloudBackend, "GetSaveDataArchiveInfoByApplicationId"},
        {3, &IRemoteStorageController::RejectUnavailableCloudBackend, "GetSaveDataArchiveCount"},
        {6, &IRemoteStorageController::RejectUnavailableCloudBackend, "CleanupSaveDataArchives"},
        {7, &IRemoteStorageController::RejectUnavailableCloudBackend, "CreateSaveDataArchiveCacheUpdationTask"},
        {8, &IRemoteStorageController::RejectUnavailableCloudBackend, "CreateSaveDataArchiveCacheUpdationForSpecifiedApplicationTask"},
        {9, &IRemoteStorageController::RejectUnavailableCloudBackend, "Delete"},
        {10, &IRemoteStorageController::RejectUnavailableCloudBackend, "GetSeriesInfo"},
        {11, &IRemoteStorageController::RejectUnavailableCloudBackend, "CreateDeleteDataTask"},
        {12, &IRemoteStorageController::RejectUnavailableCloudBackend, "DeleteSeriesInfo"},
        {13, &IRemoteStorageController::RejectUnavailableCloudBackend, "CreateRegisterNotificationTokenTask"},
        {14, D<&IRemoteStorageController::GetDataNewnessByApplicationId>, "GetDataNewnessByApplicationId"},
        {15, &IRemoteStorageController::RejectUnavailableCloudBackend, "RegisterUploadSaveDataTransferTaskForAutonomyRegistration"},
        {16, &IRemoteStorageController::RejectUnavailableCloudBackend, "CreateCleanupToDeleteSaveDataArchiveInfoTask"},
        {17, &IRemoteStorageController::RejectUnavailableCloudBackend, "ListDataInfo"},
        {18, D<&IRemoteStorageController::GetDataInfo>, "GetDataInfoV1"},
        {19, &IRemoteStorageController::RejectUnavailableCloudBackend, "GetDataInfoCacheUpdateNativeHandleHolder"},
        {20, &IRemoteStorageController::RejectUnavailableCloudBackend, "CreateSaveDataArchiveInfoCacheForSaveDataBackupUpdationTask"},
        {21, &IRemoteStorageController::RejectUnavailableCloudBackend, "ListSecondarySaves"},
        {22, D<&IRemoteStorageController::GetSecondarySave>, "GetSecondarySave"},
        {23, &IRemoteStorageController::RejectUnavailableCloudBackend, "TouchSecondarySave"},
        {24, &IRemoteStorageController::RejectUnavailableCloudBackend, "GetSecondarySaveDataInfo"},
        {25, &IRemoteStorageController::RejectUnavailableCloudBackend, "RegisterDownloadSaveDataTransferTaskForAutonomyRegistration"},
        {26, &IRemoteStorageController::RejectUnavailableCloudBackend, "Unknown26"}, //20.0.0+
        {27, D<&IRemoteStorageController::GetDataInfo>, "GetDataInfoV2"}, //20.0.0+
        {28, &IRemoteStorageController::RejectUnavailableCloudBackend, "Unknown28"}, //20.0.0+
        {29, &IRemoteStorageController::RejectUnavailableCloudBackend, "Unknown29"}, //21.0.0+
        {800, &IRemoteStorageController::RejectUnavailableCloudBackend, "Unknown800"}, //20.0.0+
        {900, &IRemoteStorageController::RejectUnavailableCloudBackend, "SetLoadedDataMissing"},
        {901, &IRemoteStorageController::RejectUnavailableCloudBackend, "Unknown901"}, //20.2.0+
    };
    // clang-format on

    RegisterHandlers(functions);
}

IRemoteStorageController::~IRemoteStorageController() = default;

void IRemoteStorageController::RejectUnavailableCloudBackend(HLERequestContext& ctx) {
    // OLSC backup/transfer operations require Nintendo cloud authorization and storage.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}



Result IRemoteStorageController::GetSecondarySave(Out<bool> out_has_secondary_save,
                                                  Out<std::array<u64, 3>> out_unknown,
                                                  u64 application_id) {
    LOG_ERROR(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    *out_has_secondary_save = false;
    *out_unknown = {};
    R_SUCCEED();
}

Result IRemoteStorageController::GetDataNewnessByApplicationId(Out<u8> out_newness,
                                                              u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    *out_newness = 0;
    R_SUCCEED();
}

Result IRemoteStorageController::GetDataInfo(Out<std::array<u8, 0x38>> out_data, u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    out_data->fill(0);
    R_SUCCEED();
}

} // namespace Service::OLSC
