// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/hex_util.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/result.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/file_sys/errors.h"
#include "core/hle/service/bcat/backend/backend.h"
#include "core/hle/service/bcat/bcat_result.h"
#include "core/hle/service/bcat/bcat_service.h"
#include "core/hle/service/bcat/bcat_util.h"
#include "core/hle/service/bcat/delivery_cache_progress_service.h"
#include "core/hle/service/bcat/delivery_cache_storage_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BCAT {

static u64 GetCurrentBuildID(const Core::System::CurrentBuildProcessID& id) {
    u64 out{};
    std::memcpy(&out, id.data(), sizeof(u64));
    return out;
}

IBcatService::IBcatService(Core::System& system_, BcatBackend& backend_, u64 program_id_)
    : ServiceFramework{system_, "IBcatService"}, backend{backend_}, program_id{program_id_},
      progress{{
          ProgressServiceBackend{system_, "Normal"},
          ProgressServiceBackend{system_, "Directory"},
      }} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {10100, D<&IBcatService::RequestSyncDeliveryCache>, "RequestSyncDeliveryCache"},
            {10101, D<&IBcatService::RequestSyncDeliveryCacheWithDirectoryName>, "RequestSyncDeliveryCacheWithDirectoryName"},
            {10200, &IBcatService::RejectUnavailableDeliveryTask, "CancelSyncDeliveryCacheRequest"}},
            {20100, &IBcatService::RejectUnavailableDeliveryTask, "RequestSyncDeliveryCacheWithApplicationId"},
            {20101, &IBcatService::RejectUnavailableDeliveryTask, "RequestSyncDeliveryCacheWithApplicationIdAndDirectoryName"},
            {20300, &IBcatService::RejectUnavailableDeliveryTask, "GetDeliveryCacheStorageUpdateNotifier"}},
            {20301, &IBcatService::RejectUnavailableDeliveryTask, "RequestSuspendDeliveryTask"}},
            {20400, &IBcatService::RejectUnavailableDeliveryTask, "RegisterSystemApplicationDeliveryTask"}},
            {20401, &IBcatService::RejectUnavailableDeliveryTask, "UnregisterSystemApplicationDeliveryTask"}},
            {20410, &IBcatService::RejectUnavailableDeliveryTask, "SetSystemApplicationDeliveryTaskTimer"}},
            {30100, D<&IBcatService::SetPassphrase>, "SetPassphrase"},
            {30101, &IBcatService::RejectUnavailableDeliveryTask, "Unknown30101"}}, //2.0.0-2.3.0
            {30102, &IBcatService::RejectUnavailableDeliveryTask, "Unknown30102"}}, //2.0.0-2.3.0
            {30200, &IBcatService::RejectUnavailableDeliveryTask, "RegisterDeliveryTask"}},
            {30201, &IBcatService::RejectUnavailableDeliveryTask, "UnregisterDeliveryTask"}},
            {30202, &IBcatService::RejectUnavailableDeliveryTask, "BlockDeliveryTask"}},
            {30203, &IBcatService::RejectUnavailableDeliveryTask, "UnblockDeliveryTask"}},
            {30210, &IBcatService::RejectUnavailableDeliveryTask, "SetDeliveryTaskTimer"}},
            {30300, D<&IBcatService::RegisterSystemApplicationDeliveryTasks>, "RegisterSystemApplicationDeliveryTasks"},
            {90100, &IBcatService::RejectUnavailableDeliveryTask, "GetDeliveryTaskList"}},
            {90101, &IBcatService::RejectUnavailableDeliveryTask, "GetDeliveryTaskListForSystem"}}, //11.0.0+
            {90200, &IBcatService::RejectUnavailableDeliveryTask, "GetDeliveryList"}},
            {90201, D<&IBcatService::ClearDeliveryCacheStorage>, "ClearDeliveryCacheStorage"},
            {90202, &IBcatService::RejectUnavailableDeliveryTask, "ClearDeliveryTaskSubscriptionStatus"}},
            {90300, &IBcatService::RejectUnavailableDeliveryTask, "GetPushNotificationLog"}},
            {90301, &IBcatService::RejectUnavailableDeliveryTask, "GetDeliveryCacheStorageUsage"}}, //11.0.0+
        };
    // clang-format on
    RegisterHandlers(functions);
}

IBcatService::~IBcatService() = default;

void IBcatService::RejectUnavailableDeliveryTask(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result IBcatService::RequestSyncDeliveryCache(
    OutInterface<IDeliveryCacheProgressService> out_interface) {
    LOG_DEBUG(Service_BCAT, "called");

    auto& progress_backend{GetProgressBackend(SyncType::Normal)};
    backend.Synchronize(system.Kernel(), {program_id, GetCurrentBuildID(system.GetApplicationProcessBuildID())},
                        GetProgressBackend(SyncType::Normal));

    *out_interface = std::make_shared<IDeliveryCacheProgressService>(
        system, progress_backend.GetEvent(), progress_backend.GetImpl());
    R_SUCCEED();
}

Result IBcatService::RequestSyncDeliveryCacheWithDirectoryName(
    const DirectoryName& name_raw, OutInterface<IDeliveryCacheProgressService> out_interface) {
    const auto name = Common::StringFromFixedZeroTerminatedBuffer(name_raw.data(), name_raw.size());

    LOG_DEBUG(Service_BCAT, "called, name={}", name);

    auto& progress_backend{GetProgressBackend(SyncType::Directory)};
    backend.SynchronizeDirectory(system.Kernel(), {program_id, GetCurrentBuildID(system.GetApplicationProcessBuildID())},
        name, progress_backend);

    *out_interface = std::make_shared<IDeliveryCacheProgressService>(
        system, progress_backend.GetEvent(), progress_backend.GetImpl());
    R_SUCCEED();
}

Result IBcatService::SetPassphrase(u64 application_id,
                                   InBuffer<BufferAttr_HipcPointer> passphrase_buffer) {
    LOG_DEBUG(Service_BCAT, "called, application_id={:016X}, passphrase={}", application_id,
              Common::HexToString(passphrase_buffer));

    R_UNLESS(application_id != 0, ResultInvalidArgument);
    R_UNLESS(passphrase_buffer.size() <= 0x40, ResultInvalidArgument);

    Passphrase passphrase{};
    std::memcpy(passphrase.data(), passphrase_buffer.data(),
                (std::min)(passphrase.size(), passphrase_buffer.size()));

    backend.SetPassphrase(system.Kernel(), application_id, passphrase);
    R_SUCCEED();
}

Result IBcatService::RegisterSystemApplicationDeliveryTasks() {
    // Encore synchronizes BCAT only on explicit requests and has no Horizon background delivery
    // scheduler for system applications. Registration is therefore an intentional no-op.
    LOG_DEBUG(Service_BCAT, "System application delivery-task registration ignored");
    R_SUCCEED();
}

Result IBcatService::ClearDeliveryCacheStorage(u64 application_id) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}", application_id);

    R_UNLESS(application_id != 0, ResultInvalidArgument);
    R_UNLESS(backend.Clear(system.Kernel(), application_id), FileSys::ResultPermissionDenied);
    R_SUCCEED();
}

ProgressServiceBackend& IBcatService::GetProgressBackend(SyncType type) {
    return progress.at(static_cast<size_t>(type));
}

const ProgressServiceBackend& IBcatService::GetProgressBackend(SyncType type) const {
    return progress.at(static_cast<size_t>(type));
}

} // namespace Service::BCAT
