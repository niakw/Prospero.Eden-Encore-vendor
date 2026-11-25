// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/hex_util.h"
#include "common/logging.h"
#include "common/uuid.h"
#include <cstring>

#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/prepo/prepo.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/reporter.h"

namespace Service::PlayReport {

class PlayReport final : public ServiceFramework<PlayReport> {
public:
    explicit PlayReport(const char* name, Core::System& system_) : ServiceFramework{system_, name} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {10100, &PlayReport::SaveReport<Core::Reporter::PlayReportType::Old>, "SaveReportOld"},
            {10101, &PlayReport::SaveReportWithUser<Core::Reporter::PlayReportType::Old>, "SaveReportWithUserOld"},
            {10102, &PlayReport::SaveReport<Core::Reporter::PlayReportType::Old2>, "SaveReportOld2"},
            {10103, &PlayReport::SaveReportWithUser<Core::Reporter::PlayReportType::Old2>, "SaveReportWithUserOld2"},
            {10104, &PlayReport::SaveReport<Core::Reporter::PlayReportType::Old3>, "SaveReportOld3"},
            {10105, &PlayReport::SaveReportWithUser<Core::Reporter::PlayReportType::Old3>, "SaveReportWithUserOld3"},
            {10106, &PlayReport::SaveReport<Core::Reporter::PlayReportType::New>, "SaveReport"},
            {10107, &PlayReport::SaveReportWithUser<Core::Reporter::PlayReportType::New>, "SaveReportWithUser"},
            {10200, &PlayReport::RequestImmediateTransmission, "RequestImmediateTransmission"},
            {10300, &PlayReport::GetTransmissionStatus, "GetTransmissionStatus"},
            {10400, &PlayReport::GetSystemSessionId, "GetSystemSessionId"},
            {20100, &PlayReport::SaveSystemReportOld, "SaveSystemReport"},
            {20101, &PlayReport::SaveSystemReportWithUserOld, "SaveSystemReportWithUser"},
            {20102, &PlayReport::SaveSystemReport, "SaveSystemReport"},
            {20103, &PlayReport::SaveSystemReportWithUser, "SaveSystemReportWithUser"},
            {20200, &PlayReport::SetOperationMode, "SetOperationMode"},
            {30100, &PlayReport::ClearStorage, "ClearStorage"},
            {30200, &PlayReport::ClearStatistics, "ClearStatistics"},
            {30300, &PlayReport::GetStorageUsage, "GetStorageUsage"},
            {30400, &PlayReport::GetStatistics, "GetStatistics"},
            {30401, &PlayReport::GetThroughputHistory, "GetThroughputHistory"},
            {30500, &PlayReport::GetLastUploadError, "GetLastUploadError"},
            {30600, &PlayReport::GetApplicationUploadSummary, "GetApplicationUploadSummary"},
            {40100, &PlayReport::IsUserAgreementCheckEnabled, "IsUserAgreementCheckEnabled"},
            {40101, &PlayReport::SetUserAgreementCheckEnabled, "SetUserAgreementCheckEnabled"},
            {50100, &PlayReport::ReadAllApplicationReportFiles, "ReadAllApplicationReportFiles"},
            {90100, &PlayReport::ReadAllReportFiles, "ReadAllReportFiles"},
            {90101, &PlayReport::RejectUnknownModernCommand, "Unknown90101"},
            {90102, &PlayReport::RejectUnknownModernCommand, "Unknown90102"},
            {90200, &PlayReport::GetStatistics, "GetStatistics"},
            {90201, &PlayReport::GetThroughputHistory, "GetThroughputHistory"},
            {90300, &PlayReport::GetLastUploadError, "GetLastUploadError"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void RejectUnknownModernCommand(HLERequestContext& ctx) {
        LOG_WARNING(Service_PREPO, "Rejecting unqualified modern PREPO command {}", ctx.GetCommand());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

    template <Core::Reporter::PlayReportType Type>
    void SaveReport(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto process_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBufferA(0);
        const auto data2 = ctx.ReadBufferX(0);

        LOG_DEBUG(Service_PREPO,
                  "called, type={:02X}, process_id={:016X}, data1_size={:016X}, data2_size={:016X}",
                  Type, process_id, data1.size(), data2.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Type, system.ResolveCallerProgramId(process_id), {data1, data2},
                                process_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    template <Core::Reporter::PlayReportType Type>
    void SaveReportWithUser(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto user_id = rp.PopRaw<u128>();
        const auto process_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBufferA(0);
        const auto data2 = ctx.ReadBufferX(0);

        LOG_DEBUG(Service_PREPO,
                  "called, type={:02X}, user_id={:016X}{:016X}, process_id={:016X}, "
                  "data1_size={:016X}, data2_size={:016X}",
                  Type, user_id[1], user_id[0], process_id, data1.size(), data2.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Type, system.ResolveCallerProgramId(process_id), {data1, data2},
                                process_id, user_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void RequestImmediateTransmission(HLERequestContext& ctx) {
        // Encore stores play reports locally and has no Nintendo telemetry transport. Treat an
        // immediate-transmission request as an acknowledged no-op instead of pretending to upload.
        LOG_DEBUG(Service_PREPO, "Immediate telemetry transmission ignored (local reporter only)");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetTransmissionStatus(HLERequestContext& ctx) {
        // No remote transport exists in Encore; the neutral idle/not-transmitting state is stable.
        constexpr s32 status = 0;
        LOG_DEBUG(Service_PREPO, "Returning local-only transmission status={}", status);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(status);
    }

    void GetSystemSessionId(HLERequestContext& ctx) {
        // A Nintendo telemetry session is never created; zero is the stable "no session" value.
        constexpr u64 system_session_id = 0;
        LOG_DEBUG(Service_PREPO, "Returning local-only system session id={}", system_session_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(system_session_id);
    }

    void SaveSystemReportOld(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto title_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBufferA(0);
        const auto data2 = ctx.ReadBufferX(0);

        LOG_DEBUG(Service_PREPO, "called, title_id={:016X}, data1_size={:016X}, data2_size={:016X}",
                  title_id, data1.size(), data2.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Core::Reporter::PlayReportType::System, title_id, {data1, data2});

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SaveSystemReportWithUserOld(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto user_id = rp.PopRaw<u128>();
        const auto title_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBufferA(0);
        const auto data2 = ctx.ReadBufferX(0);

        Common::UUID uuid{};
        std::memcpy(uuid.uuid.data(), user_id.data(), sizeof(Common::UUID));

        LOG_DEBUG(Service_PREPO,
                  "called, user_id={}, title_id={:016X}, data1_size={:016X}, data2_size={:016X}",
                  uuid.FormattedString(), title_id, data1.size(), data2.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Core::Reporter::PlayReportType::System, title_id, {data1, data2},
                                std::nullopt, user_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetOperationMode(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        operation_mode = rp.Pop<u64>();
        LOG_DEBUG(Service_PREPO, "Set local play-report operation mode={}", operation_mode);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ClearStorage(HLERequestContext& ctx) {
        // Encore's Reporter files are emulator diagnostics, not Horizon PREPO storage. The guest
        // PREPO store is intentionally empty and therefore already clear.
        LOG_DEBUG(Service_PREPO, "Guest PREPO storage already empty");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ClearStatistics(HLERequestContext& ctx) {
        // No Nintendo telemetry transport is active, so there are no guest upload statistics.
        LOG_DEBUG(Service_PREPO, "Guest PREPO statistics already empty");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetStorageUsage(HLERequestContext& ctx) {
        const u128 usage{};
        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw(usage);
    }

    void GetStatistics(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> statistics(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(statistics);
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetThroughputHistory(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> history(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(history);
        }
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetLastUploadError(HLERequestContext& ctx) {
        // With no upload transport there is no pending upload error to report.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetApplicationUploadSummary(HLERequestContext& ctx) {
        const u128 summary{};
        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw(summary);
    }

    void IsUserAgreementCheckEnabled(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(user_agreement_check_enabled);
    }

    void SetUserAgreementCheckEnabled(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        user_agreement_check_enabled = rp.Pop<u8>() != 0;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ReadAllApplicationReportFiles(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> reports(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(reports);
        }
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(0);
    }

    void ReadAllReportFiles(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> reports(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(reports);
        }
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(0);
    }

    // (21.0.0+) buffers: [0x9 (X), 0x5 (A)], inbytes: 0x10
    void SaveSystemReport(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto field0 = rp.PopRaw<u64>();
        const auto title_id = rp.PopRaw<u64>();

        const auto data_x = ctx.ReadBufferX(0);
        const auto data_a = ctx.ReadBufferA(0);

        LOG_DEBUG(Service_PREPO,
                  "called, field0={}, title_id={:016X}, data_a_size={}, data_x_size={}",
                  field0, title_id, data_a.size(), data_x.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Core::Reporter::PlayReportType::System, title_id, {data_a, data_x});

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    // (21.0.0+) buffers: [0x9 (X), 0x5 (A)], inbytes: 0x20
    void SaveSystemReportWithUser(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        // 21.0.0+: field0 (u64), user_id (u128), title_id (u64)
        const auto field0 = rp.PopRaw<u64>();
        const auto user_id = rp.PopRaw<u128>();
        const auto title_id = rp.PopRaw<u64>();

        const auto data_x = ctx.ReadBufferX(0);
        const auto data_a = ctx.ReadBufferA(0);

        Common::UUID uuid{};
        std::memcpy(uuid.uuid.data(), user_id.data(), sizeof(Common::UUID));

        LOG_DEBUG(Service_PREPO,
                  "called, user_id={}, field0={:016X}, title_id={:016X}, data_a_size={}, data_x_size={}",
                  uuid.FormattedString(), field0, title_id, data_a.size(), data_x.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Core::Reporter::PlayReportType::System, title_id, {data_a, data_x},
                                std::nullopt, user_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    u64 operation_mode{};
    bool user_agreement_check_enabled{};
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("prepo:a",
                                         std::make_shared<PlayReport>("prepo:a", system));
    server_manager->RegisterNamedService("prepo:a2",
                                         std::make_shared<PlayReport>("prepo:a2", system));
    server_manager->RegisterNamedService("prepo:m",
                                         std::make_shared<PlayReport>("prepo:m", system));
    server_manager->RegisterNamedService("prepo:s",
                                         std::make_shared<PlayReport>("prepo:s", system));
    server_manager->RegisterNamedService("prepo:u",
                                         std::make_shared<PlayReport>("prepo:u", system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PlayReport
