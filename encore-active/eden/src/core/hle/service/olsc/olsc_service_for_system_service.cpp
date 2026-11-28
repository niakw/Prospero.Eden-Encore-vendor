// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/olsc/daemon_controller.h"
#include "core/hle/service/olsc/olsc_service_for_system_service.h"
#include "core/hle/service/olsc/remote_storage_controller.h"
#include "core/hle/service/olsc/transfer_task_list_controller.h"

namespace Service::OLSC {

IOlscServiceForSystemService::IOlscServiceForSystemService(Core::System& system_)
    : ServiceFramework{system_, "olsc:s"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IOlscServiceForSystemService::GetTransferTaskListController>, "GetTransferTaskListController"},
        {1, D<&IOlscServiceForSystemService::GetRemoteStorageController>, "GetRemoteStorageController"},
        {2, D<&IOlscServiceForSystemService::GetDaemonController>, "GetDaemonController"},
        {10, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "PrepareDeleteUserProperty"},
        {11, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteUserSaveDataProperty"},
        {12, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "InvalidateMountCache"},
        {13, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteDeviceSaveDataProperty"},
        {100, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "ListLastTransferTaskErrorInfo"},
        {101, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "GetLastErrorInfoCount"},
        {102, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RemoveLastErrorInfoOld"},
        {103, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "GetLastErrorInfo"},
        {104, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "GetLastErrorEventHolder"},
        {105, D<&IOlscServiceForSystemService::GetTransferTaskErrorInfo>, "GetTransferTaskErrorInfo"},
        {200, D<&IOlscServiceForSystemService::GetDataTransferPolicy>, "GetDataTransferPolicy"},
        {201, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteDataTransferPolicyCache"},
        {202, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown202"},
        {203, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RequestUpdateDataTransferPolicyCacheAsync"},
        {204, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "ClearDataTransferPolicyCache"},
        {205, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RequestGetDataTransferPolicyAsync"},
        {206, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown206"}, //21.0.0+
        {300, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "GetUserSaveDataProperty"},
        {301, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "SetUserSaveDataProperty"},
        {302, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown302"}, //21.0.0+
        {400, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "CleanupSaveDataBackupContextForSpecificApplications"},
        {900, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteAllTransferTask"},
        {902, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteAllSeriesInfo"},
        {903, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteAllSdaInfoCache"},
        {904, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteAllApplicationSetting"},
        {905, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteAllTransferTaskErrorInfo"},
        {906, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RegisterTransferTaskErrorInfo"},
        {907, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "AddSaveDataArchiveInfoCache"},
        {908, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteSeriesInfo"},
        {909, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "GetSeriesInfo"},
        {910, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RemoveTransferTaskErrorInfo"},
        {911, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteAllSeriesInfoForSaveDataBackup"},
        {912, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "DeleteSeriesInfoForSaveDataBackup"},
        {913, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "GetSeriesInfoForSaveDataBackup"},
        {914, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown914"}, //20.2.0+
        {1000, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "UpdateIssueOld"},
        {1010, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1010"},
        {1011, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1011"},
        {1012, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1012"},
        {1013, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unkown1013"},
        {1014, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1014"},
        {1020, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1020"},
        {1021, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1021"},
        {1022, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1022"},
        {1023, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1023"},
        {1024, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "Unknown1024"},
        {1100, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairUpdateIssueInfoCacheAync"},
        {1110, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairGetIssueInfo"},
        {1111, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairListIssueInfo"},
        {1112, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairListOperationPermissionInfo"},
        {1113, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairListDataInfoForRepairedSaveDataDownload"},
        {1114, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairListDataInfoForOriginalSaveDataDownload"},
        {1120, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairUploadSaveDataAsync"},
        {1121, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairUploadSaveDataAsync1"},
        {1122, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairDownloadRepairedSaveDataAsync"},
        {1123, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairDownloadOriginalSaveDataAsync"},
        {1124, &IOlscServiceForSystemService::RejectUnavailableCloudBackend, "RepairGetOperationProgressInfo"},
        {10000, D<&IOlscServiceForSystemService::GetOlscServiceForSystemService>, "GetOlscServiceForSystemService"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IOlscServiceForSystemService::~IOlscServiceForSystemService() = default;

void IOlscServiceForSystemService::RejectUnavailableCloudBackend(HLERequestContext& ctx) {
    // OLSC backup/transfer operations require Nintendo cloud authorization and storage.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}



Result IOlscServiceForSystemService::GetTransferTaskListController(
    Out<SharedPointer<ITransferTaskListController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<ITransferTaskListController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetRemoteStorageController(
    Out<SharedPointer<IRemoteStorageController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<IRemoteStorageController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetDaemonController(
    Out<SharedPointer<IDaemonController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<IDaemonController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetDataTransferPolicy(
    Out<DataTransferPolicy> out_policy, u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    DataTransferPolicy policy{};
    policy.upload_policy = 0;
    policy.download_policy = 0;
    *out_policy = policy;
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetTransferTaskErrorInfo(Out<TransferTaskErrorInfo> out_info,
                                                             Common::UUID uuid, u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, uuid={} application_id={:016X}",
                uuid.FormattedString(), application_id);

    TransferTaskErrorInfo info{};
    info.uid = uuid;
    info.application_id = application_id;
    info.unknown_0x18 = 0;
    info.reserved_0x19.fill(0);
    info.unknown_0x20 = 0;
    info.error_code = 0;
    info.reserved_0x2C = 0;

    *out_info = info;
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetOlscServiceForSystemService(
    Out<SharedPointer<IOlscServiceForSystemService>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::static_pointer_cast<IOlscServiceForSystemService>(shared_from_this());
    R_SUCCEED();
}

} // namespace Service::OLSC
