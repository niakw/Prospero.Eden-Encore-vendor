// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nim/nim.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NIM {
class IShopServiceAsync final : public ServiceFramework<IShopServiceAsync> {
public:
    explicit IShopServiceAsync(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAsync"},
          service_context{system_, "IShopServiceAsync"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IShopServiceAsync::Cancel>, "Cancel"},
            {1, D<&IShopServiceAsync::GetSize>, "GetSize"},
            {2, D<&IShopServiceAsync::Read>, "Read"},
            {3, D<&IShopServiceAsync::GetErrorCode>, "GetErrorCode"},
            {4, D<&IShopServiceAsync::Request>, "Request"},
            {5, D<&IShopServiceAsync::Prepare>, "Prepare"},
        };
        // clang-format on

        RegisterHandlers(functions);

        completion_event = service_context.CreateEvent("IShopServiceAsync:Completion");
    }

    ~IShopServiceAsync() override {
        CancelImpl();
        service_context.CloseEvent(completion_event);
    }

    Kernel::KReadableEvent* GetEvent() const {
        return &completion_event->GetReadableEvent();
    }

private:
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* completion_event;

    std::jthread worker;
    std::atomic<u32> error_code{0};

    std::mutex data_mutex;
    std::vector<u8> download_data;

    void CancelImpl() {
        worker.request_stop();
        if (worker.joinable()) {
            worker.join();
        }
    }

    Result Cancel() {
        LOG_DEBUG(Service_NIM, "called");
        CancelImpl();
        R_SUCCEED();
    }

    Result GetSize(Out<u64> out_size) {
        LOG_DEBUG(Service_NIM, "called");
        std::scoped_lock lock{data_mutex};
        *out_size = download_data.size();
        R_SUCCEED();
    }

    Result Read(Out<u64> out_size, u64 offset, OutBuffer<BufferAttr_HipcAutoSelect> out_buffer) {
        std::scoped_lock lock{data_mutex};

        u64 actual_read = 0;
        if (offset < download_data.size()) {
            actual_read = std::min<u64>(out_buffer.size(), download_data.size() - offset);
            std::memcpy(out_buffer.data(), download_data.data() + offset, actual_read);
        }

        *out_size = actual_read;
        R_SUCCEED();
    }

    Result GetErrorCode(Out<u32> out_error_code) {
        LOG_DEBUG(Service_NIM, "called");
        *out_error_code = error_code.load();
        R_SUCCEED();
    }

    Result Request() {
        LOG_DEBUG(Service_NIM, "(STUBBED) called");
        CancelImpl();

        error_code.store(0);
        completion_event->Clear(system.Kernel());

        {
            std::scoped_lock lock{data_mutex};
            download_data.clear();
        }

        worker = std::jthread([this](const std::stop_token& stop_token) {
            if (stop_token.stop_requested()) {
                error_code.store(1);
            } else {
                std::scoped_lock lock{data_mutex};
                // Dummy JSON response, else it fails...
                const std::string dummy_response = "{}";
                download_data.assign(dummy_response.begin(), dummy_response.end());
                error_code.store(0);
            }
            completion_event->Signal(system.Kernel());
        });

        R_SUCCEED();
    }

    Result Prepare(InArray<char, BufferAttr_HipcMapAlias> in_path, InArray<char, BufferAttr_HipcMapAlias> in_post) {
        LOG_DEBUG(Service_NIM, "called");
        if (!in_path.empty()) {
            std::string url(in_path.data(), in_path.size());
            LOG_INFO(Service_NIM, "Preparing request for URL: {}", url);
        }
        R_SUCCEED();
    }
};

class IShopServiceAccessor final : public ServiceFramework<IShopServiceAccessor> {
public:
    explicit IShopServiceAccessor(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAccessor"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IShopServiceAccessor::CreateAsyncInterface, "CreateAsyncInterface"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateAsyncInterface(HLERequestContext& ctx) {LOG_DEBUG(Service_NIM, "called");
        auto async_interface = std::make_shared<IShopServiceAsync>(system);

        IPC::ResponseBuilder rb{ctx, 2, 1, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, async_interface->GetEvent());
        rb.PushIpcInterface<IShopServiceAsync>(ctx, std::move(async_interface));
    }
};

class IShopServiceAccessServer final : public ServiceFramework<IShopServiceAccessServer> {
public:
    explicit IShopServiceAccessServer(Core::System& system_)
        : ServiceFramework{system_, "IShopServiceAccessServer"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IShopServiceAccessServer::CreateAccessorInterface, "CreateAccessorInterface"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateAccessorInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessor>(ctx, system);
    }
};

class NIM final : public ServiceFramework<NIM> {
public:
    explicit NIM(Core::System& system_) : ServiceFramework{system_, "nim"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NIM::RejectUnavailableNintendoOnline, "CreateSystemUpdateTask"},
            {1, &NIM::RejectUnavailableNintendoOnline, "DestroySystemUpdateTask"},
            {2, &NIM::RejectUnavailableNintendoOnline, "ListSystemUpdateTask"},
            {3, &NIM::RejectUnavailableNintendoOnline, "RequestSystemUpdateTaskRun"},
            {4, &NIM::RejectUnavailableNintendoOnline, "GetSystemUpdateTaskInfo"},
            {5, &NIM::RejectUnavailableNintendoOnline, "CommitSystemUpdateTask"},
            {6, &NIM::RejectUnavailableNintendoOnline, "CreateNetworkInstallTask"},
            {7, &NIM::RejectUnavailableNintendoOnline, "DestroyNetworkInstallTask"},
            {8, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTask"},
            {9, &NIM::RejectUnavailableNintendoOnline, "RequestNetworkInstallTaskRun"},
            {10, &NIM::RejectUnavailableNintendoOnline, "GetNetworkInstallTaskInfo"},
            {11, &NIM::RejectUnavailableNintendoOnline, "CommitNetworkInstallTask"},
            {12, &NIM::RejectUnavailableNintendoOnline, "RequestLatestSystemUpdateMeta"},
            {14, &NIM::RejectUnavailableNintendoOnline, "ListApplicationNetworkInstallTask"},
            {15, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTaskContentMeta"},
            {16, &NIM::RejectUnavailableNintendoOnline, "RequestLatestVersion"},
            {17, &NIM::RejectUnavailableNintendoOnline, "SetNetworkInstallTaskAttribute"},
            {18, &NIM::RejectUnavailableNintendoOnline, "AddNetworkInstallTaskContentMeta"},
            {19, &NIM::RejectUnavailableNintendoOnline, "GetDownloadedSystemDataPath"},
            {20, &NIM::RejectUnavailableNintendoOnline, "CalculateNetworkInstallTaskRequiredSize"},
            {21, &NIM::RejectUnavailableNintendoOnline, "IsExFatDriverIncluded"},
            {22, &NIM::RejectUnavailableNintendoOnline, "GetBackgroundDownloadStressTaskInfo"},
            {23, &NIM::RejectUnavailableNintendoOnline, "RequestDeviceAuthenticationToken"},
            {24, &NIM::RejectUnavailableNintendoOnline, "RequestGameCardRegistrationStatus"},
            {25, &NIM::RejectUnavailableNintendoOnline, "RequestRegisterGameCard"},
            {26, &NIM::RejectUnavailableNintendoOnline, "RequestRegisterNotificationToken"},
            {27, &NIM::RejectUnavailableNintendoOnline, "RequestDownloadTaskList"},
            {28, &NIM::RejectUnavailableNintendoOnline, "RequestApplicationControl"},
            {29, &NIM::RejectUnavailableNintendoOnline, "RequestLatestApplicationControl"},
            {30, &NIM::RejectUnavailableNintendoOnline, "RequestVersionList"},
            {31, &NIM::RejectUnavailableNintendoOnline, "CreateApplyDeltaTask"},
            {32, &NIM::RejectUnavailableNintendoOnline, "DestroyApplyDeltaTask"},
            {33, &NIM::RejectUnavailableNintendoOnline, "ListApplicationApplyDeltaTask"},
            {34, &NIM::RejectUnavailableNintendoOnline, "RequestApplyDeltaTaskRun"},
            {35, &NIM::RejectUnavailableNintendoOnline, "GetApplyDeltaTaskInfo"},
            {36, &NIM::RejectUnavailableNintendoOnline, "ListApplyDeltaTask"},
            {37, &NIM::RejectUnavailableNintendoOnline, "CommitApplyDeltaTask"},
            {38, &NIM::RejectUnavailableNintendoOnline, "CalculateApplyDeltaTaskRequiredSize"},
            {39, &NIM::RejectUnavailableNintendoOnline, "PrepareShutdown"},
            {40, &NIM::RejectUnavailableNintendoOnline, "ListApplyDeltaTask"},
            {41, &NIM::RejectUnavailableNintendoOnline, "ClearNotEnoughSpaceStateOfApplyDeltaTask"},
            {42, &NIM::RejectUnavailableNintendoOnline, "CreateApplyDeltaTaskFromDownloadTask"},
            {43, &NIM::RejectUnavailableNintendoOnline, "GetBackgroundApplyDeltaStressTaskInfo"},
            {44, &NIM::RejectUnavailableNintendoOnline, "GetApplyDeltaTaskRequiredStorage"},
            {45, &NIM::RejectUnavailableNintendoOnline, "CalculateNetworkInstallTaskContentsSize"},
            {46, &NIM::RejectUnavailableNintendoOnline, "PrepareShutdownForSystemUpdate"},
            {47, &NIM::RejectUnavailableNintendoOnline, "FindMaxRequiredApplicationVersionOfTask"},
            {48, &NIM::RejectUnavailableNintendoOnline, "CommitNetworkInstallTaskPartially"},
            {49, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTaskCommittedContentMeta"},
            {50, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTaskNotCommittedContentMeta"},
            {51, &NIM::RejectUnavailableNintendoOnline, "FindMaxRequiredSystemVersionOfTask"},
            {52, &NIM::RejectUnavailableNintendoOnline, "GetNetworkInstallTaskErrorContext"},
            {53, &NIM::RejectUnavailableNintendoOnline, "CreateLocalCommunicationReceiveApplicationTask"},
            {54, &NIM::RejectUnavailableNintendoOnline, "DestroyLocalCommunicationReceiveApplicationTask"},
            {55, &NIM::RejectUnavailableNintendoOnline, "ListLocalCommunicationReceiveApplicationTask"},
            {56, &NIM::RejectUnavailableNintendoOnline, "RequestLocalCommunicationReceiveApplicationTaskRun"},
            {57, &NIM::RejectUnavailableNintendoOnline, "GetLocalCommunicationReceiveApplicationTaskInfo"},
            {58, &NIM::RejectUnavailableNintendoOnline, "CommitLocalCommunicationReceiveApplicationTask"},
            {59, &NIM::RejectUnavailableNintendoOnline, "ListLocalCommunicationReceiveApplicationTaskContentMeta"},
            {60, &NIM::RejectUnavailableNintendoOnline, "CreateLocalCommunicationSendApplicationTask"},
            {61, &NIM::RejectUnavailableNintendoOnline, "RequestLocalCommunicationSendApplicationTaskRun"},
            {62, &NIM::RejectUnavailableNintendoOnline, "GetLocalCommunicationReceiveApplicationTaskErrorContext"},
            {63, &NIM::RejectUnavailableNintendoOnline, "GetLocalCommunicationSendApplicationTaskInfo"},
            {64, &NIM::RejectUnavailableNintendoOnline, "DestroyLocalCommunicationSendApplicationTask"},
            {65, &NIM::RejectUnavailableNintendoOnline, "GetLocalCommunicationSendApplicationTaskErrorContext"},
            {66, &NIM::RejectUnavailableNintendoOnline, "CalculateLocalCommunicationReceiveApplicationTaskRequiredSize"},
            {67, &NIM::RejectUnavailableNintendoOnline, "ListApplicationLocalCommunicationReceiveApplicationTask"},
            {68, &NIM::RejectUnavailableNintendoOnline, "ListApplicationLocalCommunicationSendApplicationTask"},
            {69, &NIM::RejectUnavailableNintendoOnline, "CreateLocalCommunicationReceiveSystemUpdateTask"},
            {70, &NIM::RejectUnavailableNintendoOnline, "DestroyLocalCommunicationReceiveSystemUpdateTask"},
            {71, &NIM::RejectUnavailableNintendoOnline, "ListLocalCommunicationReceiveSystemUpdateTask"},
            {72, &NIM::RejectUnavailableNintendoOnline, "RequestLocalCommunicationReceiveSystemUpdateTaskRun"},
            {73, &NIM::RejectUnavailableNintendoOnline, "GetLocalCommunicationReceiveSystemUpdateTaskInfo"},
            {74, &NIM::RejectUnavailableNintendoOnline, "CommitLocalCommunicationReceiveSystemUpdateTask"},
            {75, &NIM::RejectUnavailableNintendoOnline, "GetLocalCommunicationReceiveSystemUpdateTaskErrorContext"},
            {76, &NIM::RejectUnavailableNintendoOnline, "CreateLocalCommunicationSendSystemUpdateTask"},
            {77, &NIM::RejectUnavailableNintendoOnline, "RequestLocalCommunicationSendSystemUpdateTaskRun"},
            {78, &NIM::RejectUnavailableNintendoOnline, "GetLocalCommunicationSendSystemUpdateTaskInfo"},
            {79, &NIM::RejectUnavailableNintendoOnline, "DestroyLocalCommunicationSendSystemUpdateTask"},
            {80, &NIM::RejectUnavailableNintendoOnline, "GetLocalCommunicationSendSystemUpdateTaskErrorContext"},
            {81, &NIM::RejectUnavailableNintendoOnline, "ListLocalCommunicationSendSystemUpdateTask"},
            {82, &NIM::RejectUnavailableNintendoOnline, "GetReceivedSystemDataPath"},
            {83, &NIM::RejectUnavailableNintendoOnline, "CalculateApplyDeltaTaskOccupiedSize"},
            {84, &NIM::RejectUnavailableNintendoOnline, "ReloadErrorSimulation"},
            {85, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTaskContentMetaFromInstallMeta"},
            {86, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTaskOccupiedSize"},
            {87, &NIM::RejectUnavailableNintendoOnline, "RequestQueryAvailableELicenses"},
            {88, &NIM::RejectUnavailableNintendoOnline, "RequestAssignELicenses"},
            {89, &NIM::RejectUnavailableNintendoOnline, "RequestExtendELicenses"},
            {90, &NIM::RejectUnavailableNintendoOnline, "RequestSyncELicenses"},
            {91, &NIM::RejectUnavailableNintendoOnline, "Unknown91"}, //6.0.0-14.1.2
            {92, &NIM::RejectUnavailableNintendoOnline, "Unknown92"}, //21.0.0+
            {93, &NIM::RejectUnavailableNintendoOnline, "RequestReportActiveELicenses"},
            {94, &NIM::RejectUnavailableNintendoOnline, "RequestReportActiveELicensesPassively"},
            {95, &NIM::RejectUnavailableNintendoOnline, "RequestRegisterDynamicRightsNotificationToken"},
            {96, &NIM::RejectUnavailableNintendoOnline, "RequestAssignAllDeviceLinkedELicenses"},
            {97, &NIM::RejectUnavailableNintendoOnline, "RequestRevokeAllELicenses"},
            {98, &NIM::RejectUnavailableNintendoOnline, "RequestPrefetchForDynamicRights"},
            {99, &NIM::RejectUnavailableNintendoOnline, "CreateNetworkInstallTask"},
            {100, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTaskRightsIds"},
            {101, &NIM::RejectUnavailableNintendoOnline, "RequestDownloadETickets"},
            {102, &NIM::RejectUnavailableNintendoOnline, "RequestQueryDownloadableContents"},
            {103, &NIM::RejectUnavailableNintendoOnline, "DeleteNetworkInstallTaskContentMeta"},
            {104, &NIM::RejectUnavailableNintendoOnline, "RequestIssueEdgeTokenForDebug"},
            {105, &NIM::RejectUnavailableNintendoOnline, "RequestQueryAvailableELicenses2"},
            {106, &NIM::RejectUnavailableNintendoOnline, "RequestAssignELicenses2"},
            {107, &NIM::RejectUnavailableNintendoOnline, "GetNetworkInstallTaskStateCounter"},
            {108, &NIM::RejectUnavailableNintendoOnline, "InvalidateDynamicRightsNaIdTokenCacheForDebug"},
            {109, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTaskPartialInstallContentMeta"},
            {110, &NIM::RejectUnavailableNintendoOnline, "ListNetworkInstallTaskRightsIdsFromIndex"},
            {111, &NIM::RejectUnavailableNintendoOnline, "AddNetworkInstallTaskContentMetaForUser"},
            {112, &NIM::RejectUnavailableNintendoOnline, "RequestAssignELicensesAndDownloadETickets"},
            {113, &NIM::RejectUnavailableNintendoOnline, "RequestQueryAvailableCommonELicenses"},
            {114, &NIM::RejectUnavailableNintendoOnline, "SetNetworkInstallTaskExtendedAttribute"},
            {115, &NIM::RejectUnavailableNintendoOnline, "GetNetworkInstallTaskExtendedAttribute"},
            {116, &NIM::RejectUnavailableNintendoOnline, "GetAllocatorInfo"},
            {117, &NIM::RejectUnavailableNintendoOnline, "RequestQueryDownloadableContentsByApplicationId"},
            {118, &NIM::RejectUnavailableNintendoOnline, "MarkNoDownloadRightsErrorResolved"},
            {119, &NIM::RejectUnavailableNintendoOnline, "GetApplyDeltaTaskAllAppliedContentMeta"},
            {120, &NIM::RejectUnavailableNintendoOnline, "PrioritizeNetworkInstallTask"},
            {121, &NIM::RejectUnavailableNintendoOnline, "RequestQueryAvailableCommonELicenses2"},
            {122, &NIM::RejectUnavailableNintendoOnline, "RequestAssignCommonELicenses"},
            {123, &NIM::RejectUnavailableNintendoOnline, "RequestAssignCommonELicenses2"},
            {124, &NIM::RejectUnavailableNintendoOnline, "IsNetworkInstallTaskFrontOfQueue"},
            {125, &NIM::RejectUnavailableNintendoOnline, "PrioritizeApplyDeltaTask"},
            {126, &NIM::RejectUnavailableNintendoOnline, "RerouteDownloadingPatch"},
            {127, &NIM::RejectUnavailableNintendoOnline, "UnmarkNoDownloadRightsErrorResolved"},
            {128, &NIM::RejectUnavailableNintendoOnline, "RequestContentsSize"},
            {129, &NIM::RejectUnavailableNintendoOnline, "RequestContentsAuthorizationToken"},
            {130, &NIM::RejectUnavailableNintendoOnline, "RequestCdnVendorDiscovery"},
            {131, &NIM::RejectUnavailableNintendoOnline, "RefreshDebugAvailability"},
            {132, &NIM::RejectUnavailableNintendoOnline, "ClearResponseSimulationEntry"},
            {133, &NIM::RejectUnavailableNintendoOnline, "RegisterResponseSimulationEntry"},
            {134, &NIM::RejectUnavailableNintendoOnline, "GetProcessedCdnVendors"},
            {135, &NIM::RejectUnavailableNintendoOnline, "RefreshRuntimeBehaviorsForDebug"},
            {136, &NIM::RejectUnavailableNintendoOnline, "RequestOnlineSubscriptionFreeTrialAvailability"},
            {137, &NIM::RejectUnavailableNintendoOnline, "GetNetworkInstallTaskContentMetaCount"},
            {138, &NIM::RejectUnavailableNintendoOnline, "RequestRevokeELicenses"},
            {139, &NIM::RejectUnavailableNintendoOnline, "EnableNetworkConnectionToUseApplicationCore"},
            {140, &NIM::RejectUnavailableNintendoOnline, "DisableNetworkConnectionToUseApplicationCore"},
            {141, &NIM::RejectUnavailableNintendoOnline, "IsNetworkConnectionEnabledToUseApplicationCore"},
            {142, &NIM::RejectUnavailableNintendoOnline, "RequestCheckSafeSystemVersion"},
            {143, &NIM::RejectUnavailableNintendoOnline, "RequestApplicationIcon"},
            {144, &NIM::RejectUnavailableNintendoOnline, "RequestDownloadIdbeIconFile"},
            {147, &NIM::RejectUnavailableNintendoOnline, "Unknown147"}, //18.0.0+
            {148, &NIM::RejectUnavailableNintendoOnline, "Unknown148"}, //18.0.0+
            {150, &NIM::RejectUnavailableNintendoOnline, "Unknown150"}, //19.0.0+
            {151, &NIM::RejectUnavailableNintendoOnline, "Unknown151"}, //20.0.0+
            {152, &NIM::RejectUnavailableNintendoOnline, "Unknown152"}, //20.0.0+
            {153, &NIM::RejectUnavailableNintendoOnline, "Unknown153"}, //20.0.0+
            {154, &NIM::RejectUnavailableNintendoOnline, "Unknown154"}, //20.0.0+
            {155, &NIM::RejectUnavailableNintendoOnline, "Unknown155"}, //20.0.0+
            {156, &NIM::RejectUnavailableNintendoOnline, "Unknown156"}, //20.0.0+
            {157, &NIM::RejectUnavailableNintendoOnline, "Unknown157"}, //20.0.0+
            {158, &NIM::RejectUnavailableNintendoOnline, "Unknown158"}, //20.0.0+
            {159, &NIM::RejectUnavailableNintendoOnline, "Unknown159"}, //20.0.0+
            {160, &NIM::RejectUnavailableNintendoOnline, "Unknown160"}, //20.0.0+
            {161, &NIM::RejectUnavailableNintendoOnline, "Unknown161"}, //20.0.0+
            {162, &NIM::RejectUnavailableNintendoOnline, "Unknown162"}, //20.0.0+
            {163, &NIM::RejectUnavailableNintendoOnline, "Unknown163"}, //20.0.0+
            {164, &NIM::RejectUnavailableNintendoOnline, "Unknown164"}, //20.0.0+
            {165, &NIM::RejectUnavailableNintendoOnline, "Unknown165"}, //20.0.0+
            {166, &NIM::RejectUnavailableNintendoOnline, "Unknown166"}, //20.0.0+
            {167, &NIM::RejectUnavailableNintendoOnline, "Unknown167"}, //20.0.0+
            {168, &NIM::RejectUnavailableNintendoOnline, "Unknown168"}, //20.0.0+
            {169, &NIM::RejectUnavailableNintendoOnline, "Unknown169"}, //20.0.0+
            {170, &NIM::RejectUnavailableNintendoOnline, "Unknown170"}, //20.0.0+
            {171, &NIM::RejectUnavailableNintendoOnline, "Unknown171"}, //20.0.0+
            {172, &NIM::RejectUnavailableNintendoOnline, "Unknown172"}, //20.0.0+
            {173, &NIM::RejectUnavailableNintendoOnline, "Unknown173"}, //20.0.0+
            {174, &NIM::RejectUnavailableNintendoOnline, "Unknown174"}, //20.0.0+
            {175, &NIM::RejectUnavailableNintendoOnline, "Unknown175"}, //20.0.0+
            {176, &NIM::RejectUnavailableNintendoOnline, "Unknown176"}, //20.0.0+
            {177, &NIM::RejectUnavailableNintendoOnline, "Unknown177"}, //20.0.0+
            {2000, &NIM::RejectUnavailableNintendoOnline, "Unknown2000"}, //20.0.0+
            {2001, &NIM::RejectUnavailableNintendoOnline, "Unknown2001"}, //20.0.0+
            {2002, &NIM::RejectUnavailableNintendoOnline, "Unknown2002"}, //20.0.0+
            {2003, &NIM::RejectUnavailableNintendoOnline, "Unknown2003"}, //20.0.0+
            {2004, &NIM::RejectUnavailableNintendoOnline, "Unknown2004"}, //20.0.0+
            {2007, &NIM::RejectUnavailableNintendoOnline, "Unknown2007"}, //20.0.0+
            {2011, &NIM::RejectUnavailableNintendoOnline, "Unknown2011"}, //20.0.0+
            {2012, &NIM::RejectUnavailableNintendoOnline, "Unknown2012"}, //20.0.0+
            {2013, &NIM::RejectUnavailableNintendoOnline, "Unknown2013"}, //20.0.0+
            {2014, &NIM::RejectUnavailableNintendoOnline, "Unknown2014"}, //20.0.0+
            {2015, &NIM::RejectUnavailableNintendoOnline, "Unknown2015"}, //20.0.0+
            {2016, &NIM::RejectUnavailableNintendoOnline, "Unknown2016"}, //20.0.0+
            {2017, &NIM::RejectUnavailableNintendoOnline, "Unknown2017"}, //20.0.0+
            {2018, &NIM::RejectUnavailableNintendoOnline, "Unknown2018"}, //20.0.0+
            {2019, &NIM::RejectUnavailableNintendoOnline, "Unknown2019"}, //20.0.0+
            {2020, &NIM::RejectUnavailableNintendoOnline, "Unknown2020"}, //20.0.0+
            {2021, &NIM::RejectUnavailableNintendoOnline, "Unknown2021"}, //20.0.0+
            {2022, &NIM::RejectUnavailableNintendoOnline, "Unknown2022"}, //20.0.0+
            {2023, &NIM::RejectUnavailableNintendoOnline, "Unknown2023"}, //20.0.0+
            {2024, &NIM::RejectUnavailableNintendoOnline, "Unknown2024"}, //20.0.0+
            {2025, &NIM::RejectUnavailableNintendoOnline, "Unknown2025"}, //20.0.0+
            {2026, &NIM::RejectUnavailableNintendoOnline, "Unknown2026"}, //20.0.0+
            {2027, &NIM::RejectUnavailableNintendoOnline, "Unknown2027"}, //20.0.0+
            {2028, &NIM::RejectUnavailableNintendoOnline, "Unknown2028"}, //20.0.0+
            {2029, &NIM::RejectUnavailableNintendoOnline, "Unknown2029"}, //20.0.0+
            {2030, &NIM::RejectUnavailableNintendoOnline, "Unknown2030"}, //20.0.0+
            {2031, &NIM::RejectUnavailableNintendoOnline, "Unknown2031"}, //20.0.0+
            {2032, &NIM::RejectUnavailableNintendoOnline, "Unknown2032"}, //20.0.0+
            {2033, &NIM::RejectUnavailableNintendoOnline, "Unknown2033"}, //20.0.0+
            {2034, &NIM::RejectUnavailableNintendoOnline, "Unknown2034"}, //20.0.0+
            {2035, &NIM::RejectUnavailableNintendoOnline, "Unknown2035"}, //20.0.0+
            {2036, &NIM::RejectUnavailableNintendoOnline, "Unknown2036"}, //20.0.0+
            {2037, &NIM::RejectUnavailableNintendoOnline, "Unknown2037"}, //20.0.0+
            {2038, &NIM::RejectUnavailableNintendoOnline, "Unknown2038"}, //20.0.0+
            {2039, &NIM::RejectUnavailableNintendoOnline, "Unknown2039"}, //20.0.0+
            {2040, &NIM::RejectUnavailableNintendoOnline, "Unknown2040"}, //20.0.0+
            {2041, &NIM::RejectUnavailableNintendoOnline, "Unknown2041"}, //20.0.0+
            {2042, &NIM::RejectUnavailableNintendoOnline, "Unknown2042"}, //20.0.0+
            {2043, &NIM::RejectUnavailableNintendoOnline, "Unknown2043"}, //20.0.0+
            {2044, &NIM::RejectUnavailableNintendoOnline, "Unknown2044"}, //20.0.0+
            {2045, &NIM::RejectUnavailableNintendoOnline, "Unknown2045"}, //20.0.0+
            {2046, &NIM::RejectUnavailableNintendoOnline, "Unknown2046"}, //20.0.0+
            {2047, &NIM::RejectUnavailableNintendoOnline, "Unknown2047"}, //20.0.0+
            {2048, &NIM::RejectUnavailableNintendoOnline, "Unknown2048"}, //20.0.0+
            {2049, &NIM::RejectUnavailableNintendoOnline, "Unknown2049"}, //20.0.0+
            {2050, &NIM::RejectUnavailableNintendoOnline, "Unknown2050"}, //20.0.0+
            {2051, &NIM::RejectUnavailableNintendoOnline, "Unknown2051"}, //20.0.0+
            {3000, &NIM::RejectUnavailableNintendoOnline, "RequestLatestApplicationIcon"}, //17.0.0+
            {3001, &NIM::RejectUnavailableNintendoOnline, "RequestDownloadIdbeLatestIconFile"}, //17.0.0+
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

class NIM_ECA final : public ServiceFramework<NIM_ECA> {
public:
    explicit NIM_ECA(Core::System& system_) : ServiceFramework{system_, "nim:eca"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NIM_ECA::CreateServerInterface, "CreateServerInterface"},
            {1, &NIM_ECA::UnsupportedDebugCommand, "RefreshDebugAvailability"},
            {2, &NIM_ECA::UnsupportedDebugCommand, "ClearDebugResponse"},
            {3, &NIM_ECA::UnsupportedDebugCommand, "RegisterDebugResponse"},
            {4, &NIM_ECA::IsLargeResourceAvailable, "IsLargeResourceAvailable"},
            {5, &NIM_ECA::CreateServerInterface2, "CreateServerInterface2"} // 17.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void UnsupportedDebugCommand(HLERequestContext& ctx) {
        LOG_WARNING(Service_NIM, "nim:eca debug-response command is not supported");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

    void CreateServerInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessServer>(ctx, system);
    }

    void IsLargeResourceAvailable(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        const auto unknown{rp.Pop<u64>()};

        LOG_INFO(Service_NIM, "(STUBBED) called, unknown={}", unknown);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(false);
    }

    void CreateServerInterface2(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "(STUBBED) called.");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IShopServiceAccessServer>(ctx, system);
    }
};

class NIM_SHP final : public ServiceFramework<NIM_SHP> {
public:
    explicit NIM_SHP(Core::System& system_) : ServiceFramework{system_, "nim:shp"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestDeviceAuthenticationToken"},
            {1, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestCachedDeviceAuthenticationToken"},
            {2, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestEdgeToken"},
            {3, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestCachedEdgeToken"},
            {100, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestRegisterDeviceAccount"},
            {101, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestUnregisterDeviceAccount"},
            {102, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestDeviceAccountStatus"},
            {103, &NIM_SHP::RejectUnavailableNintendoOnline, "GetDeviceAccountInfo"},
            {104, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestDeviceRegistrationInfo"},
            {105, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestTransferDeviceAccount"},
            {106, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestSyncRegistration"},
            {107, &NIM_SHP::RejectUnavailableNintendoOnline, "IsOwnDeviceId"},
            {200, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestRegisterNotificationToken"},
            {300, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestUnlinkDevice"},
            {301, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestUnlinkDeviceIntegrated"},
            {302, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestLinkDevice"},
            {303, &NIM_SHP::RejectUnavailableNintendoOnline, "HasDeviceLink"},
            {304, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestUnlinkDeviceAll"},
            {305, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestCreateVirtualAccount"},
            {306, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestDeviceLinkStatus"},
            {400, &NIM_SHP::RejectUnavailableNintendoOnline, "GetAccountByVirtualAccount"},
            {401, &NIM_SHP::RejectUnavailableNintendoOnline, "GetVirtualAccount"},
            {500, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestSyncTicketLegacy"},
            {501, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestDownloadTicket"},
            {502, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestDownloadTicketForPrepurchasedContents"},
            {503, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestSyncTicket"},
            {504, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestDownloadTicketForPrepurchasedContents2"},
            {505, &NIM_SHP::RejectUnavailableNintendoOnline, "RequestDownloadTicketForPrepurchasedContentsForAccount"},
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

class IEnsureNetworkClockAvailabilityService final
    : public ServiceFramework<IEnsureNetworkClockAvailabilityService> {
public:
    explicit IEnsureNetworkClockAvailabilityService(Core::System& system_)
        : ServiceFramework{system_, "IEnsureNetworkClockAvailabilityService"},
          service_context{system_, "IEnsureNetworkClockAvailabilityService"} {
        static const FunctionInfo functions[] = {
            {0, &IEnsureNetworkClockAvailabilityService::StartTask, "StartTask"},
            {1, &IEnsureNetworkClockAvailabilityService::GetFinishNotificationEvent,
             "GetFinishNotificationEvent"},
            {2, &IEnsureNetworkClockAvailabilityService::GetResult, "GetResult"},
            {3, &IEnsureNetworkClockAvailabilityService::Cancel, "Cancel"},
            {4, &IEnsureNetworkClockAvailabilityService::IsProcessing, "IsProcessing"},
            {5, &IEnsureNetworkClockAvailabilityService::GetServerTime, "GetServerTime"},
        };
        RegisterHandlers(functions);

        finished_event =
            service_context.CreateEvent("IEnsureNetworkClockAvailabilityService:FinishEvent");
    }

    ~IEnsureNetworkClockAvailabilityService() override {
        service_context.CloseEvent(finished_event);
    }

private:
    void StartTask(HLERequestContext& ctx) {
        // No need to connect to the internet, just finish the task straight away.
        LOG_DEBUG(Service_NIM, "called");
        finished_event->Signal(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFinishNotificationEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, finished_event->GetReadableEvent());
    }

    void GetResult(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Cancel(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");
        finished_event->Clear(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsProcessing(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.PushRaw<u32>(0); // We instantly process the request
    }

    void GetServerTime(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        const s64 server_time{std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count()};
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<s64>(server_time);
    }

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* finished_event;
};

class NTC final : public ServiceFramework<NTC> {
public:
    explicit NTC(Core::System& system_) : ServiceFramework{system_, "ntc"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NTC::OpenEnsureNetworkClockAvailabilityService, "OpenEnsureNetworkClockAvailabilityService"},
            {100, &NTC::SuspendAutonomicTimeCorrection, "SuspendAutonomicTimeCorrection"},
            {101, &NTC::ResumeAutonomicTimeCorrection, "ResumeAutonomicTimeCorrection"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void OpenEnsureNetworkClockAvailabilityService(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IEnsureNetworkClockAvailabilityService>(ctx, system);
    }

    void SuspendAutonomicTimeCorrection(HLERequestContext& ctx) {
        // Network-clock availability tasks complete synchronously in Encore, so there is no
        // autonomous correction worker to suspend.
        LOG_DEBUG(Service_NIM, "Autonomic time-correction suspend acknowledged (no worker)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ResumeAutonomicTimeCorrection(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIM, "Autonomic time-correction resume acknowledged (no worker)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

class NIM_ECAS final : public ServiceFramework<NIM_ECAS> {
public:
    explicit NIM_ECAS(Core::System& system_) : ServiceFramework{system_, "nim:ecas"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NIM_ECAS::RejectUnavailableNintendoOnline, "RegisterSpecialClient"},
            {1, &NIM_ECAS::RejectUnavailableNintendoOnline, "UnregisterSpecialClient"},
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

    server_manager->RegisterNamedService("nim", std::make_shared<NIM>(system));
    server_manager->RegisterNamedService("nim:eca", std::make_shared<NIM_ECA>(system));
    server_manager->RegisterNamedService("nim:ecas", std::make_shared<NIM_ECAS>(system));
    server_manager->RegisterNamedService("nim:shp", std::make_shared<NIM_SHP>(system));
    server_manager->RegisterNamedService("ntc", std::make_shared<NTC>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NIM
