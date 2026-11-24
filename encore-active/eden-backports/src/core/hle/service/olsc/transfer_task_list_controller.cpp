// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/olsc/native_handle_holder.h"
#include "core/hle/service/olsc/stopper_object.h"
#include "core/hle/service/olsc/transfer_task_list_controller.h"

namespace Service::OLSC {

ITransferTaskListController::ITransferTaskListController(Core::System& system_)
    : ServiceFramework{system_, "ITransferTaskListController"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ITransferTaskListController::RejectUnavailableCloudTransfer, "GetTransferTaskCountForOcean"},
        {1, &ITransferTaskListController::RejectUnavailableCloudTransfer, "GetTransferTaskInfoForOcean"},
        {2, &ITransferTaskListController::RejectUnavailableCloudTransfer, "ListTransferTaskInfoForOcean"},
        {3, &ITransferTaskListController::RejectUnavailableCloudTransfer, "DeleteTransferTaskForOcean"},
        {4, &ITransferTaskListController::RejectUnavailableCloudTransfer, "RaiseTransferTaskPriorityForOcean"},
        {5, D<&ITransferTaskListController::GetTransferTaskEndEventNativeHandleHolder>, "GetTransferTaskEndEventNativeHandleHolder"},
        {6, &ITransferTaskListController::RejectUnavailableCloudTransfer, "GetTransferTaskProgressForOcean"},
        {7, &ITransferTaskListController::RejectUnavailableCloudTransfer, "GetTransferTaskLastResultForOcean"},
        {8, D<&ITransferTaskListController::StopNextTransferTaskExecution>, "StopNextTransferTaskExecution"},
        {9, D<&ITransferTaskListController::GetTransferTaskStartEventNativeHandleHolder>, "GetTransferTaskStartEventNativeHandleHolder"},
        {10, &ITransferTaskListController::RejectUnavailableCloudTransfer, "SuspendTransferTaskForOcean"},
        {11, &ITransferTaskListController::RejectUnavailableCloudTransfer, "GetCurrentTransferTaskInfoForOcean"},
        {12, &ITransferTaskListController::RejectUnavailableCloudTransfer, "FindTransferTaskInfoForOcean"},
        {13, &ITransferTaskListController::RejectUnavailableCloudTransfer, "CancelCurrentRepairTransferTask"},
        {14, &ITransferTaskListController::RejectUnavailableCloudTransfer, "GetRepairTransferTaskProgress"},
        {15, &ITransferTaskListController::RejectUnavailableCloudTransfer, "EnsureExecutableForRepairTransferTask"},
        {16, D<&ITransferTaskListController::GetTransferTaskCount>, "GetTransferTaskCount"},
        {17, &ITransferTaskListController::RejectUnavailableCloudTransfer, "GetTransferTaskInfo"},
        {18, &ITransferTaskListController::RejectUnavailableCloudTransfer, "ListTransferTaskInfo"},
        {19, &ITransferTaskListController::RejectUnavailableCloudTransfer, "DeleteTransferTask"},
        {20, &ITransferTaskListController::RejectUnavailableCloudTransfer, "RaiseTransferTaskPriority"},
        {21, D<&ITransferTaskListController::GetTransferTaskProgress>, "GetTransferTaskProgress"}, //10.1.0+
        {22, &ITransferTaskListController::RejectUnavailableCloudTransfer, "GetTransferTaskLastResult"},
        {23, &ITransferTaskListController::RejectUnavailableCloudTransfer, "SuspendTransferTask"},
        {24, D<&ITransferTaskListController::GetCurrentTransferTaskInfo>, "GetCurrentTransferTaskInfo"},
        {25, D<&ITransferTaskListController::FindTransferTaskInfo>, "FindTransferTaskInfo"},
        {26, &ITransferTaskListController::RejectUnavailableCloudTransfer, "Unknown26"}, //20.1.0+
        {27, &ITransferTaskListController::RejectUnavailableCloudTransfer, "Unknown27"}, //20.1.0+
        {28, &ITransferTaskListController::RejectUnavailableCloudTransfer, "Unknown28"}, //20.1.0+
        {29, &ITransferTaskListController::RejectUnavailableCloudTransfer, "Unknown29"}, //20.1.0+
        {30, &ITransferTaskListController::RejectUnavailableCloudTransfer, "Unknown30"}, //20.1.0+
    };
    // clang-format on

    RegisterHandlers(functions);
}

ITransferTaskListController::~ITransferTaskListController() = default;

void ITransferTaskListController::RejectUnavailableCloudTransfer(HLERequestContext& ctx) {
    // No Nintendo cloud-backup transfer backend or task scheduler on PS5.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result ITransferTaskListController::GetTransferTaskEndEventNativeHandleHolder(
    Out<SharedPointer<INativeHandleHolder>> out_holder) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_holder = std::make_shared<INativeHandleHolder>(system);
    R_SUCCEED();
}

Result ITransferTaskListController::StopNextTransferTaskExecution(
    Out<SharedPointer<IStopperObject>> out_stopper) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_stopper = std::make_shared<IStopperObject>(system);
    R_SUCCEED();
}

Result ITransferTaskListController::GetTransferTaskStartEventNativeHandleHolder(
    Out<SharedPointer<INativeHandleHolder>> out_holder) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_holder = std::make_shared<INativeHandleHolder>(system);
    R_SUCCEED();
}

Result ITransferTaskListController::GetCurrentTransferTaskInfo(Out<std::array<u8, 0x30>> out_info,
                                                              u8 unknown) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, unknown={:#x}", unknown);
    out_info->fill(0);
    R_SUCCEED();
}

Result ITransferTaskListController::GetTransferTaskProgress() {
    LOG_WARNING(Service_OLSC, "(STUBBED) called.");
    R_SUCCEED();
}

Result ITransferTaskListController::FindTransferTaskInfo(Out<std::array<u8, 0x30>> out_info,
                                                        InBuffer<BufferAttr_HipcAutoSelect> in) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, in_size={}", in.size());
    out_info->fill(0);
    R_SUCCEED();
}

Result ITransferTaskListController::GetTransferTaskCount(Out<u32> out_count, u8 unknown) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, unknown={:#x}", unknown);
    *out_count = 0;
    R_SUCCEED();
}

} // namespace Service::OLSC
