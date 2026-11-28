// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/olsc/olsc_service_for_application.h"

namespace Service::OLSC {

IOlscServiceForApplication::IOlscServiceForApplication(Core::System& system_)
    : ServiceFramework{system_, "olsc:u"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IOlscServiceForApplication::Initialize>, "Initialize"},
            {10, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "VerifySaveDataBackupLicenseAsync"},
            {13, D<&IOlscServiceForApplication::GetSaveDataBackupSetting>, "GetSaveDataBackupSetting"},
            {14, D<&IOlscServiceForApplication::SetSaveDataBackupSettingEnabled>, "SetSaveDataBackupSettingEnabled"},
            {15, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "SetCustomData"},
            {16, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "DeleteSaveDataBackupSetting"},
            {18, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "GetSaveDataBackupInfoCache"},
            {19, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "UpdateSaveDataBackupInfoCacheAsync"},
            {22, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "DeleteSaveDataBackupAsync"},
            {25, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "ListDownloadableSaveDataBackupInfoAsync"},
            {26, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "DownloadSaveDataBackupAsync"},
            {27, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "UploadSaveDataBackupAsync"},
            {9010, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "VerifySaveDataBackupLicenseAsyncForDebug"},
            {9013, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "GetSaveDataBackupSettingForDebug"},
            {9014, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "SetSaveDataBackupSettingEnabledForDebug"},
            {9015, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "SetCustomDataForDebug"},
            {9016, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "DeleteSaveDataBackupSettingForDebug"},
            {9018, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "GetSaveDataBackupInfoCacheForDebug"},
            {9019, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "UpdateSaveDataBackupInfoCacheAsyncForDebug"},
            {9022, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "DeleteSaveDataBackupAsyncForDebug"},
            {9025, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "ListDownloadableSaveDataBackupInfoAsyncForDebug"},
            {9026, &IOlscServiceForApplication::RejectUnavailableCloudBackend, "DownloadSaveDataBackupAsyncForDebug"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IOlscServiceForApplication::~IOlscServiceForApplication() = default;

void IOlscServiceForApplication::RejectUnavailableCloudBackend(HLERequestContext& ctx) {
    // OLSC backup/transfer operations require Nintendo cloud authorization and storage.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}



Result IOlscServiceForApplication::Initialize(ClientProcessId process_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    initialized = true;
    R_SUCCEED();
}

Result IOlscServiceForApplication::GetSaveDataBackupSetting(Out<u8> out_save_data_backup_setting) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    // backup_setting is set to 0 since real value is unknown
    *out_save_data_backup_setting = 0;
    R_SUCCEED();
}

Result IOlscServiceForApplication::SetSaveDataBackupSettingEnabled(bool enabled,
                                                                   NS::Uid account_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, enabled={}, account_id={}", enabled,
                account_id.uuid.FormattedString());
    R_SUCCEED();
}

} // namespace Service::OLSC
