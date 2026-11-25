// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/content_management_interface.h"
#include "core/hle/service/ns/i_async_result.h"
#include "core/hle/service/ns/language.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"

namespace Service::NS {

struct ApplicationLaunchInfoV19 {
    u64 application_id;
    u64 program_id;
    u32 version;
    u8 reserved_14[0x8];
    u8 application_storage_id;
    u8 update_storage_id;
    u8 reserved_1e[0x12];
    u64 application_instance_id;
    u8 reserved_38[0x48];
};
static_assert(sizeof(ApplicationLaunchInfoV19) == 0x80);
static_assert(offsetof(ApplicationLaunchInfoV19, program_id) == 0x08);
static_assert(offsetof(ApplicationLaunchInfoV19, version) == 0x10);
static_assert(offsetof(ApplicationLaunchInfoV19, application_storage_id) == 0x1C);
static_assert(offsetof(ApplicationLaunchInfoV19, application_instance_id) == 0x30);

struct ApplicationContentStorageRecord {
    u64 title_id;
    u32 version;
    u8 meta_type;
    u8 install_type;
    u8 key_padding[2];
    u8 storage_id;
    u8 storage_padding[7];
};
static_assert(sizeof(ApplicationContentStorageRecord) == 0x18);

class IApplicationManagerInterface final : public ServiceFramework<IApplicationManagerInterface> {
public:
    explicit IApplicationManagerInterface(Core::System& system_);
    ~IApplicationManagerInterface() override;

    Result UnregisterNetworkServiceAccountWithUserSaveDataDeletion(Common::UUID user_id);
    Result IsSystemProgramInstalled(Out<bool> out_is_installed, u64 program_id);
    Result SetApplicationTerminateResult(Result terminate_result, u32 pad, u64 application_id);
    Result ClearApplicationTerminateResult(u64 application_id);
    Result GetApplicationControlData(OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                     Out<u32> out_actual_size,
                                     ApplicationControlSource application_control_source,
                                     u64 application_id);
    Result GetGameCardAttachmentInfo(Out<bool> out_is_attached);
    Result IsGameCardInserted(Out<bool> out_is_inserted);
    Result GetApplicationDesiredLanguage(Out<ApplicationLanguage> out_desired_language,
                                         u32 supported_languages);
    Result ConvertApplicationLanguageToLanguageCode(Out<u64> out_language_code,
                                                    ApplicationLanguage application_language);
    Result ConvertLanguageCodeToApplicationLanguage(
        Out<ApplicationLanguage> out_application_language, u64 language_code);
    Result ListApplicationRecord(OutArray<ApplicationRecord, BufferAttr_HipcMapAlias> out_records,
                                 Out<s32> out_count, s32 offset);
    Result ListApplicationRecordContentMeta(
        OutArray<ApplicationContentStorageRecord, BufferAttr_HipcMapAlias> out_records,
        Out<s32> out_count, u64 offset, u64 application_id);
    Result GenerateApplicationRecordCount(Out<s32> out_count);
    Result GetApplicationRecord(Out<ApplicationRecord> out_record, u64 application_id);
    Result HasApplicationRecord(Out<bool> out_has_record, u64 application_id);
    Result GetApplicationRecordUpdateSystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetApplicationShellEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result PopApplicationShellEventInfo(Out<u32> out_event_info_type, Out<u64> out_application_id);
    Result CalculateApplicationOccupiedSize(Out<ApplicationOccupiedSize> out_size,
                                            u64 application_id);
    Result GetApplicationControlProperty(OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                         u64 application_id);
    Result GetGameCardAttachmentEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetSdCardRemovedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result ListApplicationIdOnGameCard(OutArray<u64, BufferAttr_HipcMapAlias> out_ids,
                                       Out<s32> out_count);
    Result ListApplicationContentMetaStatus(
        OutArray<ApplicationContentMetaStatus, BufferAttr_HipcMapAlias> out_statuses,
        Out<s32> out_count, s32 index, u64 application_id);
    Result CountApplicationContentMeta(Out<s32> out_count, u64 application_id);
    Result ListAvailableAddOnContent(OutArray<u32, BufferAttr_HipcMapAlias> out_addons,
                                     Out<u32> out_count, u32 offset, u64 application_id);
    Result GetGameCardMountFailureEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetGameCardWakenReadyEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result IsGameCardApplicationRunning(Out<bool> out_is_running);
    Result Unknown936(Out<u64> out_result);
    Result NeedsUpdateVulnerability(Out<bool> out_needs_update_vulnerability);
    Result IsAnyApplicationEntityInstalled(Out<bool> out_is_any_application_entity_installed);
    Result GetApplicationViewDeprecated(
        OutArray<ApplicationViewV19, BufferAttr_HipcMapAlias> out_application_views,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result GetApplicationViewWithPromotionInfo(
        OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
        Out<u32> out_count,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result GetApplicationView(
        OutArray<ApplicationViewV20, BufferAttr_HipcMapAlias> out_application_views,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result GetApplicationRightsOnClient(
        OutArray<ApplicationRightsOnClient, BufferAttr_HipcMapAlias> out_rights, Out<u32> out_count,
        u32 flags, u64 application_id, Uid account_id);
    Result CheckSdCardMountStatus();
    Result GetSdCardMountStatusChangedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetTotalSpaceSize(Out<s64> out_total_space_size, FileSys::StorageId storage_id);
    Result GetFreeSpaceSize(Out<s64> out_free_space_size, FileSys::StorageId storage_id);
    Result GetGameCardUpdateDetectionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result ResumeAll();
    Result LaunchSystemApplet(Out<u64> out_process_id);
    Result TerminateSystemApplet(u64 program_id);
    Result TerminateOverlayApplet(u64 program_id);
    Result IsQualificationTransitionSupportedByProcessId(Out<bool> out_is_supported,
                                                         u64 process_id);
    Result DeleteApplicationEntity(u64 application_id);
    Result DeleteApplicationCompletely(u64 application_id);
    Result GetStorageSize(Out<s64> out_total_space_size, Out<s64> out_free_space_size,
                          FileSys::StorageId storage_id);
    Result TouchApplication(u64 application_id);
    Result IsApplicationAutoDeleteDisabled(Out<bool> out_disabled, u64 application_id);
    Result IsPatchAutoDeletableApplication(Out<bool> out_deletable, u64 application_id);
    Result IsApplicationUpdateRequested(Out<bool> out_update_required, Out<u32> out_update_version,
                                        u64 application_id);
    Result CheckApplicationLaunchVersion(u64 application_id);
    Result LaunchApplicationOld(Out<u64> out_process_id, u64 application_id);
    Result LaunchApplication(Out<u64> out_process_id, u8 program_index,
                             ApplicationLaunchInfoV19 launch_info);
    Result LaunchApplicationByProgramId(u64* out_process_id, u8 program_index,
                                        u64 application_id, u64 program_id);
    Result GetApplicationLaunchInfo(Out<ApplicationLaunchInfoV19> out_launch_info,
                                    u64 application_id);
    Result GetRunningApplicationProgramId(Out<u64> out_program_id);
    Result GetMainApplicationProgramIndexByApplicationLaunchInfo(
        Out<u8> out_program_index, ApplicationLaunchInfoV19 launch_info);
    Result GetMainApplicationProgramIndex(Out<u8> out_program_index, u64 application_id);
    Result TerminateApplication(u64 application_id);
    Result GetApplicationTerminateResult(Out<Result> out_result, u64 application_id);
    Result GetRawApplicationTerminateResult(Out<u32> out_raw_result, u64 application_id);
    Result GetApplicationLogoData(Out<s64> out_size,
                                  OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                  u64 application_id,
                                  InBuffer<BufferAttr_HipcMapAlias> logo_path_buffer);
    Result Unknown3250(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result Unknown3251(Out<s64> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result Unknown3253(Out<s64> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                       u64 arg);
    Result Unknown3254(Out<s64> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result Unknown3255(u8 arg, u64 id);
    Result Unknown3256(std::array<u8, 0x48> args);
    Result Unknown3257(u64 id);
    Result Unknown3258(Out<s64> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                       u64 arg);
    Result Unknown3259(bool flag, u64 id);
    Result Unknown3260(bool flag1, bool flag2, u64 arg1, u64 arg2);
    Result Unknown3261(Out<bool> out_result, u64 arg);
    Result Unknown3262(Out<s64> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result Unknown4022(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result Unknown4023(Out<u64> out_result);
    Result Unknown4042(OutInterface<IAsyncResult> out_interface,
                       OutCopyHandle<Kernel::KReadableEvent> out_event,
                       u64 arg1, u64 arg2);
    Result Unknown4053();
    Result Unknown4105();

    void RejectUnavailablePlatformCommand(HLERequestContext& ctx);
    Result RejectDebugOnlyCommand();

    Result RequestDownloadApplicationControlDataInBackground(u64 control_source,
                                                             u64 application_id);

    void PushApplicationRecord(HLERequestContext& ctx);
    void ListApplicationTitle(HLERequestContext& ctx);
    void ListApplicationIcon(HLERequestContext& ctx);

private:
    KernelHelpers::ServiceContext service_context;
    Event record_update_system_event;
    Event sd_card_mount_status_event;
    Event gamecard_update_detection_event;
    Event gamecard_mount_status_event;
    Event gamecard_mount_failure_event;
    Event gamecard_waken_ready_event;
    Event unknown_event;
    Event present_event;
    Event application_shell_event;
};

} // namespace Service::NS
