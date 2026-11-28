// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/erpt/erpt.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::ERPT {

class ErrorReportContext final : public ServiceFramework<ErrorReportContext> {
public:
    explicit ErrorReportContext(Core::System& system_) : ServiceFramework{system_, "erpt:c"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ErrorReportContext::SubmitContext>, "SubmitContext"},
            {1, C<&ErrorReportContext::CreateReportV0>, "CreateReportV0"},
            {2, &ErrorReportContext::RejectUnavailableNintendoOnline, "SetInitialLaunchSettingsCompletionTime"},
            {3, &ErrorReportContext::RejectUnavailableNintendoOnline, "ClearInitialLaunchSettingsCompletionTime"},
            {4, &ErrorReportContext::RejectUnavailableNintendoOnline, "UpdatePowerOnTime"},
            {5, D<&ErrorReportContext::UpdateAwakeTime>, "UpdateAwakeTime"},
            {6, &ErrorReportContext::RejectUnavailableNintendoOnline, "SubmitMultipleCategoryContext"},
            {7, &ErrorReportContext::RejectUnavailableNintendoOnline, "UpdateApplicationLaunchTime"},
            {8, &ErrorReportContext::RejectUnavailableNintendoOnline, "ClearApplicationLaunchTime"},
            {9, &ErrorReportContext::RejectUnavailableNintendoOnline, "SubmitAttachment"},
            {10, &ErrorReportContext::RejectUnavailableNintendoOnline, "CreateReportWithAttachments"},
            {11, C<&ErrorReportContext::CreateReportV1>, "CreateReportV1"},
            {12, C<&ErrorReportContext::CreateReport>, "CreateReport"},
            {20, &ErrorReportContext::RejectUnavailableNintendoOnline, "RegisterRunningApplet"},
            {21, &ErrorReportContext::RejectUnavailableNintendoOnline, "UnregisterRunningApplet"},
            {22, &ErrorReportContext::RejectUnavailableNintendoOnline, "UpdateAppletSuspendedDuration"},
            {30, &ErrorReportContext::RejectUnavailableNintendoOnline, "InvalidateForcedShutdownDetection"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result SubmitContext(InBuffer<BufferAttr_HipcMapAlias> context_entry,
                         InBuffer<BufferAttr_HipcMapAlias> field_list) {
        // Nintendo erpt uploads are not exposed by Encore. Accept the guest context locally
        // without transmitting telemetry or fabricating a remote report identifier.
        LOG_DEBUG(Service_SET, "Error-report context accepted locally, context_entry_size={}, field_list_size={}",
                  context_entry.size(), field_list.size());
        R_SUCCEED();
    }

    Result CreateReportV0(u32 report_type, InBuffer<BufferAttr_HipcMapAlias> context_entry,
                          InBuffer<BufferAttr_HipcMapAlias> report_list,
                          InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_DEBUG(Service_SET, "Error-report V0 creation ignored locally, report_type={:#x}",
                  report_type);
        R_SUCCEED();
    }

    Result CreateReportV1(u32 report_type, u32 unknown,
                          InBuffer<BufferAttr_HipcMapAlias> context_entry,
                          InBuffer<BufferAttr_HipcMapAlias> report_list,
                          InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_DEBUG(Service_SET,
                  "Error-report V1 creation ignored locally, report_type={:#x}, unknown={:#x}",
                  report_type, unknown);
        R_SUCCEED();
    }

    Result CreateReport(u32 report_type, u32 unknown, u32 create_report_option_flag,
                        InBuffer<BufferAttr_HipcMapAlias> context_entry,
                        InBuffer<BufferAttr_HipcMapAlias> report_list,
                        InBuffer<BufferAttr_HipcMapAlias> report_meta_data) {
        LOG_DEBUG(Service_SET,
                  "Error-report creation ignored locally, report_type={:#x}, unknown={:#x}, "
                  "create_report_option_flag={:#x}",
                  report_type, unknown, create_report_option_flag);
        R_SUCCEED();
    }

    Result UpdateAwakeTime(InBuffer<BufferAttr_HipcMapAlias> data_a,
                           InBuffer<BufferAttr_HipcMapAlias> data_b, u32 flag_a, u32 flag_b) {
        // Awake-time accounting is telemetry-only in erpt and has no guest-visible runtime state.
        LOG_DEBUG(Service_SET,
                  "Error-report awake-time update ignored locally, data_a_size={}, data_b_size={}, "
                  "flag_a={}, flag_b={}",
                  data_a.size(), data_b.size(), flag_a, flag_b);
        R_SUCCEED();
    }

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class ErrorReportSession final : public ServiceFramework<ErrorReportSession> {
public:
    explicit ErrorReportSession(Core::System& system_) : ServiceFramework{system_, "erpt:r"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ErrorReportSession::RejectUnavailableNintendoOnline, "OpenReport"},
            {1, &ErrorReportSession::RejectUnavailableNintendoOnline, "OpenManager"},
            {2, &ErrorReportSession::RejectUnavailableNintendoOnline, "OpenAttachment"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("erpt:c", std::make_shared<ErrorReportContext>(system));
    server_manager->RegisterNamedService("erpt:r", std::make_shared<ErrorReportSession>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::ERPT
