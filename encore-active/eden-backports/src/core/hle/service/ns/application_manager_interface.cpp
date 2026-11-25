// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/control_metadata.h"
#include "core/core.h"
#include "core/hle/api_version.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/process_creation.h"
#include "core/hle/service/am/window_system.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/application_manager_interface.h"
#include "core/hle/service/os/process.h"
#include "core/loader/loader.h"

#include "core/file_sys/content_archive.h"
#include "core/hle/service/ns/content_management_interface.h"
#include "core/hle/service/ns/read_only_application_control_data_interface.h"
#include "core/file_sys/patch_manager.h"
#include "frontend_common/firmware_manager.h"
#include "core/launch_timestamp_cache.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

namespace Service::NS {

IApplicationManagerInterface::IApplicationManagerInterface(Core::System& system_)
    : ServiceFramework{system_, "IApplicationManagerInterface"},
      service_context{system, "IApplicationManagerInterface"},
      record_update_system_event{service_context}, sd_card_mount_status_event{service_context},
      gamecard_update_detection_event{service_context},
      gamecard_mount_status_event{service_context}, gamecard_mount_failure_event{service_context},
      gamecard_waken_ready_event{service_context}, unknown_event{service_context},
      present_event{service_context}, application_shell_event{service_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IApplicationManagerInterface::ListApplicationRecord>, "ListApplicationRecord"},
        {1, D<&IApplicationManagerInterface::GenerateApplicationRecordCount>, "GenerateApplicationRecordCount"},
        {2, D<&IApplicationManagerInterface::GetApplicationRecordUpdateSystemEvent>, "GetApplicationRecordUpdateSystemEvent"},
        {3, D<&IApplicationManagerInterface::GetApplicationViewDeprecated>, "GetApplicationViewDeprecated"},
        {4, D<&IApplicationManagerInterface::DeleteApplicationEntity>, "DeleteApplicationEntity"},
        {5, D<&IApplicationManagerInterface::DeleteApplicationCompletely>, "DeleteApplicationCompletely"},
        {6, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsAnyApplicationEntityRedundant"},
        {7, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DeleteRedundantApplicationEntity"},
        {8, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsApplicationEntityMovable"},
        {9, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "MoveApplicationEntity"},
        {11, D<&IApplicationManagerInterface::CalculateApplicationOccupiedSize>, "CalculateApplicationOccupiedSize"},
        {16, &IApplicationManagerInterface::PushApplicationRecord, "PushApplicationRecord"},
        {17, D<&IApplicationManagerInterface::ListApplicationRecordContentMeta>, "ListApplicationRecordContentMeta"},
        {19, D<&IApplicationManagerInterface::LaunchApplicationOld>, "LaunchApplicationOld"},
        {21, nullptr, "GetApplicationContentPath"},
        {22, D<&IApplicationManagerInterface::TerminateApplication>, "TerminateApplication"},
        {23, nullptr, "ResolveApplicationContentPath"},
        {26, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "BeginInstallApplication"},
        {27, nullptr, "DeleteApplicationRecord"},
        {30, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestApplicationUpdateInfo"},
        {31, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestUpdateApplication"},
        {32, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CancelApplicationDownload"},
        {33, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ResumeApplicationDownload"},
        {35, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "UpdateVersionList"},
        {36, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "PushLaunchVersion"},
        {37, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListRequiredVersion"},
        {38, D<&IApplicationManagerInterface::CheckApplicationLaunchVersion>, "CheckApplicationLaunchVersion"},
        {39, nullptr, "CheckApplicationLaunchRights"},
        {40, D<&IApplicationManagerInterface::GetApplicationLogoData>, "GetApplicationLogoData"},
        {41, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CalculateApplicationDownloadRequiredSize"},
        {42, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CleanupSdCard"},
        {43, D<&IApplicationManagerInterface::CheckSdCardMountStatus>, "CheckSdCardMountStatus"},
        {44, D<&IApplicationManagerInterface::GetSdCardMountStatusChangedEvent>, "GetSdCardMountStatusChangedEvent"},
        {45, D<&IApplicationManagerInterface::GetGameCardAttachmentEvent>, "GetGameCardAttachmentEvent"},
        {46, D<&IApplicationManagerInterface::GetGameCardAttachmentInfo>, "GetGameCardAttachmentInfo"},
        {47, D<&IApplicationManagerInterface::GetTotalSpaceSize>, "GetTotalSpaceSize"},
        {48, D<&IApplicationManagerInterface::GetFreeSpaceSize>, "GetFreeSpaceSize"},
        {49, D<&IApplicationManagerInterface::GetSdCardRemovedEvent>, "GetSdCardRemovedEvent"},
        {52, D<&IApplicationManagerInterface::GetGameCardUpdateDetectionEvent>, "GetGameCardUpdateDetectionEvent"},
        {53, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DisableApplicationAutoDelete"},
        {54, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EnableApplicationAutoDelete"},
        {55, D<&IApplicationManagerInterface::GetApplicationDesiredLanguage>, "GetApplicationDesiredLanguage"},
        {56, D<&IApplicationManagerInterface::SetApplicationTerminateResult>, "SetApplicationTerminateResult"},
        {57, D<&IApplicationManagerInterface::ClearApplicationTerminateResult>, "ClearApplicationTerminateResult"},
        {58, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetLastSdCardMountUnexpectedResult"},
        {59, D<&IApplicationManagerInterface::ConvertApplicationLanguageToLanguageCode>, "ConvertApplicationLanguageToLanguageCode"},
        {60, D<&IApplicationManagerInterface::ConvertLanguageCodeToApplicationLanguage>, "ConvertLanguageCodeToApplicationLanguage"},
        {61, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetBackgroundDownloadStressTaskInfo"},
        {62, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetGameCardStopper"},
        {63, D<&IApplicationManagerInterface::IsSystemProgramInstalled>, "IsSystemProgramInstalled"},
        {64, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "StartApplyDeltaTask"},
        {65, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetRequestServerStopper"},
        {66, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetBackgroundApplyDeltaStressTaskInfo"},
        {67, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CancelApplicationApplyDelta"},
        {68, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ResumeApplicationApplyDelta"},
        {69, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CalculateApplicationApplyDeltaRequiredSize"},
        {70, D<&IApplicationManagerInterface::ResumeAll>, "ResumeAll"},
        {71, D<&IApplicationManagerInterface::GetStorageSize>, "GetStorageSize"},
        {80, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestDownloadApplication"},
        {81, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestDownloadAddOnContent"},
        {82, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DownloadApplication"},
        {83, nullptr, "CheckApplicationResumeRights"},
        {84, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetDynamicCommitEvent"},
        {85, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestUpdateApplication2"},
        {86, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EnableApplicationCrashReport"},
        {87, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsApplicationCrashReportEnabled"},
        {90, nullptr, "BoostSystemMemoryResourceLimit"},
        {91, nullptr, "DeprecatedLaunchApplication"},
        {92, D<&IApplicationManagerInterface::GetRunningApplicationProgramId>, "GetRunningApplicationProgramId"},
        {93, D<&IApplicationManagerInterface::GetMainApplicationProgramIndex>, "GetMainApplicationProgramIndex"},
        {94, D<&IApplicationManagerInterface::LaunchApplication>, "LaunchApplication"},
        {95, D<&IApplicationManagerInterface::GetApplicationLaunchInfo>, "GetApplicationLaunchInfo"},
        {96, nullptr, "AcquireApplicationLaunchInfo"},
        {97, D<&IApplicationManagerInterface::GetMainApplicationProgramIndexByApplicationLaunchInfo>, "GetMainApplicationProgramIndexByApplicationLaunchInfo"},
        {98, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EnableApplicationAllThreadDumpOnCrash"},
        {99, D<&IApplicationManagerInterface::RejectDebugOnlyCommand>, "LaunchDevMenu"},
        {100, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ResetToFactorySettings"},
        {101, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ResetToFactorySettingsWithoutUserSaveData"},
        {102, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ResetToFactorySettingsForRefurbishment"},
        {103, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ResetToFactorySettingsWithPlatformRegion"},
        {104, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ResetToFactorySettingsWithPlatformRegionAuthentication"},
        {105, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestResetToFactorySettingsSecurely"},
        {106, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestResetToFactorySettingsWithPlatformRegionAuthenticationSecurely"},
        {200, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CalculateUserSaveDataStatistics"},
        {201, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DeleteUserSaveDataAll"},
        {210, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DeleteUserSystemSaveData"},
        {211, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DeleteSaveData"},
        {220, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "UnregisterNetworkServiceAccount"},
        {221, D<&IApplicationManagerInterface::UnregisterNetworkServiceAccountWithUserSaveDataDeletion>, "UnregisterNetworkServiceAccountWithUserSaveDataDeletion"},
        {300, D<&IApplicationManagerInterface::GetApplicationShellEvent>, "GetApplicationShellEvent"},
        {301, D<&IApplicationManagerInterface::PopApplicationShellEventInfo>, "PopApplicationShellEventInfo"},
        {302, nullptr, "LaunchLibraryApplet"},
        {303, nullptr, "TerminateLibraryApplet"},
        {304, D<&IApplicationManagerInterface::LaunchSystemApplet>, "LaunchSystemApplet"},
        {305, D<&IApplicationManagerInterface::TerminateSystemApplet>, "TerminateSystemApplet"},
        {306, nullptr, "LaunchOverlayApplet"},
        {307, D<&IApplicationManagerInterface::TerminateOverlayApplet>, "TerminateOverlayApplet"},
        {400, D<&IApplicationManagerInterface::GetApplicationControlData>, "GetApplicationControlData"},
        {401, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "InvalidateAllApplicationControlCache"},
        {402, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestDownloadApplicationControlData"},
        {403, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetMaxApplicationControlCacheCount"},
        {404, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "InvalidateApplicationControlCache"},
        {405, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListApplicationControlCacheEntryInfo"},
        {406, D<&IApplicationManagerInterface::GetApplicationControlProperty>, "GetApplicationControlProperty"},
        {407, &IApplicationManagerInterface::ListApplicationTitle, "ListApplicationTitle"},
        {408, &IApplicationManagerInterface::ListApplicationIcon, "ListApplicationIcon"},
        {411, nullptr, "Unknown411"}, //19.0.0+
        {412, nullptr, "Unknown412"}, //19.0.0+
        {413, nullptr, "Unknown413"}, //19.0.0+
        {414, nullptr, "Unknown414"}, //19.0.0+
        {415, nullptr, "Unknown415"}, //19.0.0+
        {416, nullptr, "Unknown416"}, //19.0.0+
        {417, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "InvalidateAllApplicationControlCacheOfTheStage"}, //19.0.0+
        {418, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "InvalidateApplicationControlCacheOfTheStage"}, //19.0.0+
        {419, D<&IApplicationManagerInterface::RequestDownloadApplicationControlDataInBackground>, "RequestDownloadApplicationControlDataInBackground"},
        {420, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CloneApplicationControlDataCacheForDebug"},
        {421, nullptr, "Unknown421"}, //20.0.0+
        {422, nullptr, "Unknown422"}, //20.0.0+
        {423, nullptr, "Unknown423"}, //20.0.0+
        {424, nullptr, "Unknown424"}, //20.0.0+
        {425, nullptr, "Unknown425"}, //20.0.0+
        {426, nullptr, "Unknown426"}, //20.0.0+
        {427, nullptr, "Unknown427"}, //20.0.0+
        {428, nullptr, "Unknown428"}, //21.0.0+
        {429, nullptr, "Unknown429"}, //21.0.0+
        {430, nullptr, "Unknown430"}, //21.0.0+
        {502, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestCheckGameCardRegistration"},
        {503, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestGameCardRegistrationGoldPoint"},
        {504, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestRegisterGameCard"},
        {505, D<&IApplicationManagerInterface::GetGameCardMountFailureEvent>, "GetGameCardMountFailureEvent"},
        {506, D<&IApplicationManagerInterface::IsGameCardInserted>, "IsGameCardInserted"},
        {507, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EnsureGameCardAccess"},
        {508, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetLastGameCardMountFailureResult"},
        {509, D<&IApplicationManagerInterface::ListApplicationIdOnGameCard>, "ListApplicationIdOnGameCard"},
        {510, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetGameCardPlatformRegion"},
        {511, D<&IApplicationManagerInterface::GetGameCardWakenReadyEvent>, "GetGameCardWakenReadyEvent"},
        {512, D<&IApplicationManagerInterface::IsGameCardApplicationRunning>, "IsGameCardApplicationRunning"},
        {513, nullptr, "Unknown513"}, //20.0.0+
        {514, nullptr, "Unknown514"}, //20.0.0+
        {515, nullptr, "Unknown515"}, //20.0.0+
        {516, nullptr, "Unknown516"}, //21.0.0+
        {517, nullptr, "Unknown517"}, //21.0.0+
        {518, nullptr, "Unknown518"}, //21.0.0+
        {519, nullptr, "Unknown519"}, //21.0.0+
        {600, D<&IApplicationManagerInterface::CountApplicationContentMeta>, "CountApplicationContentMeta"},
        {601, D<&IApplicationManagerInterface::ListApplicationContentMetaStatus>, "ListApplicationContentMetaStatus"},
        {602, D<&IApplicationManagerInterface::ListAvailableAddOnContent>, "ListAvailableAddOnContent"},
        {603, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetOwnedApplicationContentMetaStatus"},
        {604, nullptr, "RegisterContentsExternalKey"},
        {605, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListApplicationContentMetaStatusWithRightsCheck"},
        {606, nullptr, "GetContentMetaStorage"},
        {607, D<&IApplicationManagerInterface::ListAvailableAddOnContent>, "ListAvailableAddOnContent"},
        {609, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListAvailabilityAssuredAddOnContent"},
        {610, nullptr, "GetInstalledContentMetaStorage"},
        {611, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "PrepareAddOnContent"},
        {700, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "PushDownloadTaskList"},
        {701, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ClearTaskStatusList"},
        {702, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestDownloadTaskList"},
        {703, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestEnsureDownloadTask"},
        {704, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListDownloadTaskStatus"},
        {705, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestDownloadTaskListData"},
        {800, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestVersionList"},
        {801, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListVersionList"},
        {802, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestVersionListData"},
        {900, D<&IApplicationManagerInterface::GetApplicationRecord>, "GetApplicationRecord"},
        {901, nullptr, "GetApplicationRecordProperty"},
        {902, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EnableApplicationAutoUpdate"},
        {903, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DisableApplicationAutoUpdate"},
        {904, D<&IApplicationManagerInterface::TouchApplication>, "TouchApplication"},
        {905, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestApplicationUpdate"},
        {906, D<&IApplicationManagerInterface::IsApplicationUpdateRequested>, "IsApplicationUpdateRequested"},
        {907, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "WithdrawApplicationUpdateRequest"},
        {908, nullptr, "ListApplicationRecordInstalledContentMeta"},
        {909, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "WithdrawCleanupAddOnContentsWithNoRightsRecommendation"},
        {910, D<&IApplicationManagerInterface::HasApplicationRecord>, "HasApplicationRecord"},
        {911, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "SetPreInstalledApplication"},
        {912, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ClearPreInstalledApplicationFlag"},
        {913, D<&IApplicationManagerInterface::ListApplicationRecord>, "ListAllApplicationRecord"},
        {914, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "HideApplicationRecord"},
        {915, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ShowApplicationRecord"},
        {916, D<&IApplicationManagerInterface::IsApplicationAutoDeleteDisabled>, "IsApplicationAutoDeleteDisabled"},
        {917, nullptr, "Unknown917"}, //20.0.0+
        {918, nullptr, "Unknown918"}, //20.0.0+
        {919, nullptr, "Unknown919"}, //20.0.0+
        {920, nullptr, "Unknown920"}, //20.0.0+
        {921, nullptr, "Unknown921"}, //20.0.0+
        {922, nullptr, "Unknown922"}, //20.0.0+
        {923, nullptr, "Unknown923"}, //20.0.0+
        {928, nullptr, "Unknown928"}, //20.0.0+
        {929, nullptr, "Unknown929"}, //20.0.0+
        {930, nullptr, "Unknown930"}, //20.0.0+
        {931, nullptr, "Unknown931"}, //20.0.0+
        {933, nullptr, "Unknown933"}, //20.0.0+
        {934, nullptr, "Unknown934"}, //21.0.0+
        {935, nullptr, "Unknown935"}, //21.0.0+
        {936, D<&IApplicationManagerInterface::Unknown936>, "Unknown936"}, //21.0.0+
        {1000, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestVerifyApplicationDeprecated"},
        {1001, D<&IApplicationManagerInterface::RejectDebugOnlyCommand>, "CorruptApplicationForDebug"},
        {1002, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestVerifyAddOnContentsRights"},
        {1003, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestVerifyApplication"},
        {1004, D<&IApplicationManagerInterface::RejectDebugOnlyCommand>, "CorruptContentForDebug"},
        {1200, D<&IApplicationManagerInterface::NeedsUpdateVulnerability>, "NeedsUpdateVulnerability"},
        {1300, D<&IApplicationManagerInterface::IsAnyApplicationEntityInstalled>, "IsAnyApplicationEntityInstalled"},
        {1301, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DeleteApplicationContentEntities"},
        {1302, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CleanupUnrecordedApplicationEntity"},
        {1303, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CleanupAddOnContentsWithNoRights"},
        {1304, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DeleteApplicationContentEntity"},
        {1305, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "TryDeleteRunningApplicationEntity"},
        {1306, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "TryDeleteRunningApplicationCompletely"},
        {1307, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "TryDeleteRunningApplicationContentEntities"},
        {1308, D<&IApplicationManagerInterface::RejectDebugOnlyCommand>, "DeleteApplicationCompletelyForDebug"},
        {1309, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CleanupUnavailableAddOnContents"},
        {1310, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestMoveApplicationEntity"},
        {1311, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EstimateSizeToMove"},
        {1312, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "HasMovableEntity"},
        {1313, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CleanupOrphanContents"},
        {1314, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CheckPreconditionSatisfiedToMove"},
        {1400, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "PrepareShutdown"},
        {1500, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "FormatSdCard"},
        {1501, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "NeedsSystemUpdateToFormatSdCard"},
        {1502, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetLastSdCardFormatUnexpectedResult"},
        {1504, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "InsertSdCard"},
        {1505, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RemoveSdCard"},
        {1506, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetSdCardStartupStatus"},
        {1508, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown1508"}, //20.0.0+
        {1509, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown1509"}, //20.0.0+
        {1510, nullptr, "Unknown1510"}, //20.0.0+
        {1511, nullptr, "Unknown1511"}, //20.0.0+
        {1512, nullptr, "Unknown1512"}, //20.0.0+
        {1600, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetSystemSeedForPseudoDeviceId"},
        {1601, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ResetSystemSeedForPseudoDeviceId"},
        {1700, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListApplicationDownloadingContentMeta"},
        {1701, D<&IApplicationManagerInterface::GetApplicationViewDeprecated>, "GetApplicationViewDeprecated"},
        {1702, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationDownloadTaskStatus"},
        {1703, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationViewDownloadErrorContext"},
        {1704, D<&IApplicationManagerInterface::GetApplicationViewWithPromotionInfo>, "GetApplicationViewWithPromotionInfo"},
        {1705, D<&IApplicationManagerInterface::IsPatchAutoDeletableApplication>, "IsPatchAutoDeletableApplication"},
        {1706, D<&IApplicationManagerInterface::GetApplicationView>, "GetApplicationView"},
        {1800, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsNotificationSetupCompleted"},
        {1801, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetLastNotificationInfoCount"},
        {1802, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListLastNotificationInfo"},
        {1803, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListNotificationTask"},
        {1900, nullptr, "IsActiveAccount"},
        {1901, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestDownloadApplicationPrepurchasedRights"},
        {1902, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationTicketInfo"},
        {1903, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestDownloadApplicationPrepurchasedRightsForAccount"},
        {2000, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetSystemDeliveryInfo"},
        {2001, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "SelectLatestSystemDeliveryInfo"},
        {2002, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "VerifyDeliveryProtocolVersion"},
        {2003, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationDeliveryInfo"},
        {2004, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "HasAllContentsToDeliver"},
        {2005, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CompareApplicationDeliveryInfo"},
        {2006, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CanDeliverApplication"},
        {2007, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListContentMetaKeyToDeliverApplication"},
        {2008, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "NeedsSystemUpdateToDeliverApplication"},
        {2009, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EstimateRequiredSize"},
        {2010, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestReceiveApplication"},
        {2011, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CommitReceiveApplication"},
        {2012, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetReceiveApplicationProgress"},
        {2013, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestSendApplication"},
        {2014, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetSendApplicationProgress"},
        {2015, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CompareSystemDeliveryInfo"},
        {2016, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListNotCommittedContentMeta"},
        {2017, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CreateDownloadTask"},
        {2018, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationDeliveryInfoHash"},
        {2019, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2019"}, //20.0.0+
        {2050, D<&IApplicationManagerInterface::GetApplicationRightsOnClient>, "GetApplicationRightsOnClient"},
        {2051, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "InvalidateRightsIdCache"},
        {2052, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2052"}, //20.0.0+
        {2053, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2053"}, //20.0.0+
        {2100, D<&IApplicationManagerInterface::GetApplicationTerminateResult>, "GetApplicationTerminateResult"},
        {2101, D<&IApplicationManagerInterface::GetRawApplicationTerminateResult>, "GetRawApplicationTerminateResult"},
        {2150, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CreateRightsEnvironment"},
        {2151, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DestroyRightsEnvironment"},
        {2152, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ActivateRightsEnvironment"},
        {2153, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DeactivateRightsEnvironment"},
        {2154, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ForceActivateRightsContextForExit"},
        {2155, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "UpdateRightsEnvironmentStatus"},
        {2156, nullptr, "CreateRightsEnvironmentForMicroApplication"},
        {2160, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "AddTargetApplicationToRightsEnvironment"},
        {2161, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "SetUsersToRightsEnvironment"},
        {2170, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetRightsEnvironmentStatus"},
        {2171, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetRightsEnvironmentStatusChangedEvent"},
        {2180, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestExtendRightsInRightsEnvironment"},
        {2181, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetResultOfExtendRightsInRightsEnvironment"},
        {2182, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "SetActiveRightsContextUsingStateToRightsEnvironment"},
        {2183, nullptr, "Unknown2183"}, //20.1.0+
        {2190, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetRightsEnvironmentHandleForApplication"},
        {2199, D<&IApplicationManagerInterface::RejectDebugOnlyCommand>, "GetRightsEnvironmentCountForDebug"},
        {2200, nullptr, "GetGameCardApplicationCopyIdentifier"},
        {2201, nullptr, "GetInstalledApplicationCopyIdentifier"},
        {2250, nullptr, "RequestReportActiveELicence"},
        {2300, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListEventLog"},
        {2350, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "PerformAutoUpdateByApplicationId"},
        {2351, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestNoDownloadRightsErrorResolution"},
        {2352, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestResolveNoDownloadRightsError"},
        {2353, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationDownloadTaskInfo"},
        {2354, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "PrioritizeApplicationBackgroundTask"},
        {2355, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "PreferStorageEfficientUpdate"},
        {2356, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RequestStorageEfficientUpdatePreferable"},
        {2357, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EnableMultiCoreDownload"},
        {2358, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DisableMultiCoreDownload"},
        {2359, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsMultiCoreDownloadEnabled"},
        {2360, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationDownloadTaskCount"}, //19.0.0+
        {2361, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetMaxApplicationDownloadTaskCount"}, //19.0.0+
        {2362, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2362"}, //20.0.0+
        {2363, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2363"}, //20.0.0+
        {2364, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2364"}, //20.0.0+
        {2365, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2365"}, //20.0.0+
        {2366, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2366"}, //20.0.0+
        {2367, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2367"}, //20.0.0+
        {2368, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2368"}, //20.0.0+
        {2369, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "Unknown2369"}, //21.0.0+
        {2400, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetPromotionInfo"},
        {2401, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CountPromotionInfo"},
        {2402, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListPromotionInfo"},
        {2403, D<&IApplicationManagerInterface::RejectDebugOnlyCommand>, "ImportPromotionJsonForDebug"},
        {2404, D<&IApplicationManagerInterface::RejectDebugOnlyCommand>, "ClearPromotionInfoForDebug"},
        {2500, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ConfirmAvailableTime"},
        {2510, nullptr, "CreateApplicationResource"},
        {2511, nullptr, "GetApplicationResource"},
        {2513, nullptr, "LaunchMicroApplication"},
        {2514, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ClearTaskOfAsyncTaskManager"},
        {2515, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "CleanupAllPlaceHolderAndFragmentsIfNoTask"},
        {2516, nullptr, "EnsureApplicationCertificate"},
        {2517, nullptr, "CreateApplicationInstance"},
        {2518, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "UpdateQualificationForDebug"},
        {2519, nullptr, "IsQualificationTransitionSupported"},
        {2520, D<&IApplicationManagerInterface::IsQualificationTransitionSupportedByProcessId>, "IsQualificationTransitionSupportedByProcessId"},
        {2521, nullptr, "GetRightsUserChangedEvent"},
        {2522, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsRomRedirectionAvailable"},
        {2523, nullptr, "GetProgramId"}, //17.0.0+
        {2524, nullptr, "Unknown2524"}, //19.0.0+
        {2525, nullptr, "Unknown2525"}, //20.0.0+
        {2800, nullptr, "GetApplicationIdOfPreomia"},
        {3000, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "RegisterDeviceLockKey"},
        {3001, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "UnregisterDeviceLockKey"},
        {3002, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "VerifyDeviceLockKey"},
        {3003, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "HideApplicationIcon"},
        {3004, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ShowApplicationIcon"},
        {3005, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "HideApplicationTitle"},
        {3006, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ShowApplicationTitle"},
        {3007, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EnableGameCard"},
        {3008, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DisableGameCard"},
        {3009, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "EnableLocalContentShare"},
        {3010, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "DisableLocalContentShare"},
        {3011, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsApplicationIconHidden"},
        {3012, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsApplicationTitleHidden"},
        {3013, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsGameCardEnabled"},
        {3014, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "IsLocalContentShareEnabled"},
        {3050, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "ListAssignELicenseTaskResult"},
        {3104, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationNintendoLogo"}, //18.0.0+
        {3105, &IApplicationManagerInterface::RejectUnavailablePlatformCommand, "GetApplicationStartupMovie"}, //18.0.0+
        {3250, D<&IApplicationManagerInterface::Unknown3250>, "Unknown3250"}, //23.5.0+
        {3251, D<&IApplicationManagerInterface::Unknown3251>, "Unknown3251"}, //23.5.0+
        {3252, nullptr, "Unknown3252"}, //23.5.0+
        {3253, D<&IApplicationManagerInterface::Unknown3253>, "Unknown3253"}, //23.5.0+
        {3254, D<&IApplicationManagerInterface::Unknown3254>, "Unknown3254"}, //23.5.0+
        {3255, D<&IApplicationManagerInterface::Unknown3255>, "Unknown3255"}, //23.5.0+
        {3256, D<&IApplicationManagerInterface::Unknown3256>, "Unknown3256"}, //23.5.0+
        {3257, D<&IApplicationManagerInterface::Unknown3257>, "Unknown3257"}, //23.5.0+
        {3258, D<&IApplicationManagerInterface::Unknown3258>, "Unknown3258"}, //23.5.0+
        {3259, D<&IApplicationManagerInterface::Unknown3259>, "Unknown3259"}, //23.5.0+
        {3260, D<&IApplicationManagerInterface::Unknown3260>, "Unknown3260"}, //23.5.0+
        {3261, D<&IApplicationManagerInterface::Unknown3261>, "Unknown3261"}, //23.5.0+
        {3262, D<&IApplicationManagerInterface::Unknown3262>, "Unknown3262"}, //23.5.0+
        {4000, nullptr, "Unknown4000"}, //20.0.0+
        {4004, nullptr, "Unknown4004"}, //20.0.0+
        {4006, nullptr, "Unknown4006"}, //20.0.0+
        {4007, nullptr, "Unknown4007"}, //20.0.0+
        {4008, nullptr, "Unknown4008"}, //20.0.0+
        {4009, nullptr, "Unknown4009"}, //20.0.0+
        {4010, nullptr, "Unknown4010"}, //20.0.0+
        {4011, nullptr, "Unknown4011"}, //20.0.0+
        {4012, nullptr, "Unknown4012"}, //20.0.0+
        {4013, nullptr, "Unknown4013"}, //20.0.0+
        {4015, nullptr, "Unknown4015"}, //20.0.0+
        {4017, nullptr, "Unknown4017"}, //20.0.0+
        {4019, nullptr, "Unknown4019"}, //20.0.0+
        {4020, nullptr, "Unknown4020"}, //20.0.0+
        {4021, nullptr, "Unknown4021"}, //20.0.0+
        {4022, D<&IApplicationManagerInterface::Unknown4022>, "Unknown4022"}, //20.0.0+
        {4023, D<&IApplicationManagerInterface::Unknown4023>, "Unknown4023"}, //20.0.0+
        {4024, nullptr, "Unknown4024"}, //20.0.0+
        {4025, nullptr, "Unknown4025"}, //20.0.0+
        {4026, nullptr, "Unknown4026"}, //20.0.0+
        {4027, nullptr, "Unknown4027"}, //20.0.0+
        {4028, nullptr, "Unknown4028"}, //20.0.0+
        {4029, nullptr, "Unknown4029"}, //20.0.0+
        {4030, nullptr, "Unknown4030"}, //20.0.0+
        {4031, nullptr, "Unknown4031"}, //20.0.0+
        {4032, nullptr, "Unknown4032"}, //20.0.0+
        {4033, nullptr, "Unknown4033"}, //20.0.0+
        {4034, nullptr, "Unknown4034"}, //20.0.0+
        {4035, nullptr, "Unknown4035"}, //20.0.0+
        {4037, nullptr, "Unknown4037"}, //20.0.0+
        {4038, nullptr, "Unknown4038"}, //20.0.0+
        {4039, nullptr, "Unknown4039"}, //20.0.0+
        {4040, nullptr, "Unknown4040"}, //20.0.0+
        {4041, nullptr, "Unknown4041"}, //20.0.0+
        {4042, D<&IApplicationManagerInterface::Unknown4042>, "Unknown4042"}, //20.0.0+
        {4043, nullptr, "Unknown4043"}, //20.0.0+
        {4044, nullptr, "Unknown4044"}, //20.0.0+
        {4045, nullptr, "Unknown4045"}, //20.0.0+
        {4046, nullptr, "Unknown4046"}, //20.0.0+
        {4049, nullptr, "Unknown4049"}, //20.0.0+
        {4050, nullptr, "Unknown4050"}, //20.0.0+
        {4051, nullptr, "Unknown4051"}, //20.0.0+
        {4052, nullptr, "Unknown4052"}, //20.0.0+
        {4053, D<&IApplicationManagerInterface::Unknown4053>, "Unknown4053"}, //20.0.0+
        {4054, nullptr, "Unknown4054"}, //20.0.0+
        {4055, nullptr, "Unknown4055"}, //20.0.0+
        {4056, nullptr, "Unknown4056"}, //20.0.0+
        {4057, nullptr, "Unknown4057"}, //20.0.0+
        {4058, nullptr, "Unknown4058"}, //20.0.0+
        {4059, nullptr, "Unknown4059"}, //20.0.0+
        {4060, nullptr, "Unknown4060"}, //20.0.0+
        {4061, nullptr, "Unknown4061"}, //20.0.0+
        {4062, nullptr, "Unknown4062"}, //20.0.0+
        {4063, nullptr, "Unknown4063"}, //20.0.0+
        {4064, nullptr, "Unknown4064"}, //20.0.0+
        {4065, nullptr, "Unknown4065"}, //20.0.0+
        {4066, nullptr, "Unknown4066"}, //20.0.0+
        {4067, nullptr, "Unknown4067"}, //20.0.0+
        {4068, nullptr, "Unknown4068"}, //20.0.0+
        {4069, nullptr, "Unknown4069"}, //20.0.0+
        {4070, nullptr, "Unknown4070"}, //20.0.0+
        {4071, nullptr, "Unknown4071"}, //20.0.0+
        {4072, nullptr, "Unknown4072"}, //20.0.0+
        {4073, nullptr, "Unknown4073"}, //20.0.0+
        {4074, nullptr, "Unknown4074"}, //20.0.0+
        {4075, nullptr, "Unknown4075"}, //20.0.0+
        {4076, nullptr, "Unknown4076"}, //20.0.0+
        {4077, nullptr, "Unknown4077"}, //20.0.0+
        {4078, nullptr, "Unknown4078"}, //20.0.0+
        {4079, nullptr, "Unknown4079"}, //20.0.0+
        {4080, nullptr, "Unknown4080"}, //20.0.0+
        {4081, nullptr, "Unknown4081"}, //20.0.0+
        {4083, nullptr, "Unknown4083"}, //20.0.0+
        {4084, nullptr, "Unknown4084"}, //20.0.0+
        {4085, nullptr, "Unknown4085"}, //20.0.0+
        {4086, nullptr, "Unknown4086"}, //20.0.0+
        {4087, nullptr, "Unknown4087"}, //20.0.0+
        {4088, D<&IApplicationManagerInterface::Unknown4022>, "Unknown4088"}, //20.0.0+
        {4089, nullptr, "Unknown4089"}, //20.0.0+
        {4090, nullptr, "Unknown4090"}, //20.0.0+
        {4091, nullptr, "Unknown4091"}, //20.0.0+
        {4092, nullptr, "Unknown4092"}, //20.0.0+
        {4093, nullptr, "Unknown4093"}, //20.0.0+
        {4094, nullptr, "Unknown4094"}, //20.0.0+
        {4095, nullptr, "Unknown4095"}, //20.0.0+
        {4096, nullptr, "Unknown4096"}, //20.0.0+
        {4097, nullptr, "Unknown4097"}, //20.0.0+
        {4099, nullptr, "Unknown4099"}, //21.0.0+
        {4105, D<&IApplicationManagerInterface::Unknown4105>, "Unknown4105"}, //23.0.0+
        {5000, nullptr, "Unknown5000"}, //18.0.0+
        {5001, nullptr, "Unknown5001"}, //18.0.0+
        {9999, nullptr, "GetApplicationCertificate"}, //10.0.0-10.2.0
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationManagerInterface::~IApplicationManagerInterface() = default;

Result IApplicationManagerInterface::UnregisterNetworkServiceAccountWithUserSaveDataDeletion(Common::UUID user_id) {
    LOG_DEBUG(Service_NS, "called, user_id={}", user_id.FormattedString());
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationLogoData(
    Out<s64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer, u64 application_id,
    InBuffer<BufferAttr_HipcMapAlias> logo_path_buffer) {
    const std::string path_view{reinterpret_cast<const char*>(logo_path_buffer.data()),
                                logo_path_buffer.size()};

    // Find null terminator and trim the path
    auto null_pos = path_view.find('\0');
    std::string path = (null_pos != std::string::npos) ? path_view.substr(0, null_pos) : path_view;

    LOG_DEBUG(Service_NS, "called, application_id={:016X}, logo_path={}", application_id, path);

    auto& content_provider = system.GetContentProviderUnion();

    auto program = content_provider.GetEntry(application_id, FileSys::ContentRecordType::Program);
    if (!program) {
        LOG_WARNING(Service_NS, "Application program not found for id={:016X}", application_id);
        R_RETURN(ResultUnknown);
    }

    const auto logo_dir = program->GetLogoPartition();
    if (!logo_dir) {
        LOG_WARNING(Service_NS, "Logo partition not found for id={:016X}", application_id);
        R_RETURN(ResultUnknown);
    }

    const auto file = logo_dir->GetFile(path);
    if (!file) {
        LOG_WARNING(Service_NS, "Logo path not found: {} for id={:016X}", path,
                    application_id);
        R_RETURN(ResultUnknown);
    }

    const auto data = file->ReadAllBytes();
    if (data.size() > out_buffer.size()) {
        LOG_WARNING(Service_NS, "Logo buffer too small: have={}, need={}", out_buffer.size(),
                    data.size());
        R_RETURN(ResultUnknown);
    }

    std::memcpy(out_buffer.data(), data.data(), data.size());
    *out_size = static_cast<s64>(data.size());

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationControlData(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, Out<u32> out_actual_size,
    ApplicationControlSource application_control_source, u64 application_id) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IReadOnlyApplicationControlDataInterface(system).GetApplicationControlData(
        out_buffer, out_actual_size, application_control_source, application_id));
}

Result IApplicationManagerInterface::IsSystemProgramInstalled(Out<bool> out_is_installed,
                                                               u64 program_id) {
    LOG_DEBUG(Service_NS, "called, program_id={:016X}", program_id);
    const auto* const system_cache = system.GetFileSystemController().GetSystemNANDContents();
    *out_is_installed =
        system_cache != nullptr &&
        system_cache->HasEntry(program_id, FileSys::ContentRecordType::Program);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationDesiredLanguage(
    Out<ApplicationLanguage> out_desired_language, u32 supported_languages) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IReadOnlyApplicationControlDataInterface(system).GetApplicationDesiredLanguage(
        out_desired_language, supported_languages));
}

Result IApplicationManagerInterface::ConvertApplicationLanguageToLanguageCode(
    Out<u64> out_language_code, ApplicationLanguage application_language) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(
        IReadOnlyApplicationControlDataInterface(system).ConvertApplicationLanguageToLanguageCode(
            out_language_code, application_language));
}

Result IApplicationManagerInterface::ConvertLanguageCodeToApplicationLanguage(
    Out<ApplicationLanguage> out_application_language, u64 language_code) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IReadOnlyApplicationControlDataInterface(system).ConvertLanguageCodeToApplicationLanguage(
        out_application_language, language_code));
}

Result IApplicationManagerInterface::ListApplicationRecord(
    OutArray<ApplicationRecord, BufferAttr_HipcMapAlias> out_records, Out<s32> out_count,
    s32 offset) {
    const auto limit = out_records.size();

    LOG_DEBUG(Service_NS, "called");
    const auto& cache = system.GetContentProviderUnion();
    const auto installed_games = cache.ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::Application, FileSys::ContentRecordType::Program);

    std::vector<ApplicationRecord> records;
    records.reserve(installed_games.size());

    for (const auto& [slot, game] : installed_games) {
         if (game.title_id == 0 || game.title_id < 0x0100000000001FFFull) {
             continue;
         }
         if ((game.title_id & 0xFFF) != 0) {
             continue; // skip sub-programs (e.g., 001)
         }

         ApplicationRecord record{};
         record.application_id = game.title_id;
         record.last_event = ApplicationEvent::Installed;
         record.attributes = 0;
         record.last_updated = Core::LaunchTimestampCache::GetLaunchTimestamp(game.title_id);

         records.push_back(record);
     }

     std::sort(records.begin(), records.end(), [](const ApplicationRecord& lhs, const ApplicationRecord& rhs) {
         if (lhs.last_updated == rhs.last_updated) {
             return lhs.application_id < rhs.application_id;
         }
         return lhs.last_updated > rhs.last_updated;
     });

    size_t i = 0;
    const size_t start = static_cast<size_t>(std::max(0, offset));
    for (size_t idx = start; idx < records.size() && i < limit; ++idx) {
        out_records[i++] = records[idx];
    }

    *out_count = static_cast<s32>(i);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GenerateApplicationRecordCount(Out<s32> out_count) {
    LOG_DEBUG(Service_NS, "called");
    const auto& cache = system.GetContentProviderUnion();
    const auto installed_games = cache.ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::Application, FileSys::ContentRecordType::Program);

    s32 count = 0;
    for (const auto& [slot, game] : installed_games) {
        if (game.title_id == 0 || game.title_id < 0x0100000000001FFFull) {
            continue;
        }
        if ((game.title_id & 0xFFF) != 0) {
            continue;
        }
        ++count;
    }

    *out_count = count;
    R_SUCCEED();
}

Result IApplicationManagerInterface::ListApplicationRecordContentMeta(
    OutArray<ApplicationContentStorageRecord, BufferAttr_HipcMapAlias> out_records,
    Out<s32> out_count, u64 offset, u64 application_id) {
    const u64 real_id = FileSys::GetBaseTitleID(application_id);
    auto& cp = system.GetContentProviderUnion();

    std::vector<ApplicationContentStorageRecord> records;
    const auto append = [&](u64 title_id, FileSys::TitleType meta_type,
                            FileSys::ContentRecordType record_type) {
        if (!cp.HasEntry(title_id, record_type)) {
            return;
        }
        const auto slot = cp.GetSlotForEntry(title_id, record_type);
        u8 storage_id = static_cast<u8>(FileSys::StorageId::None);
        if (slot.has_value()) {
            switch (*slot) {
            case FileSys::ContentProviderUnionSlot::UserNAND:
                storage_id = static_cast<u8>(FileSys::StorageId::NandUser);
                break;
            case FileSys::ContentProviderUnionSlot::SysNAND:
                storage_id = static_cast<u8>(FileSys::StorageId::NandSystem);
                break;
            case FileSys::ContentProviderUnionSlot::SDMC:
                storage_id = static_cast<u8>(FileSys::StorageId::SdCard);
                break;
            case FileSys::ContentProviderUnionSlot::FrontendManual:
                storage_id = static_cast<u8>(FileSys::StorageId::Host);
                break;
            default:
                break;
            }
        }
        records.push_back({
            .title_id = title_id,
            .version = cp.GetEntryVersion(title_id).value_or(0),
            .meta_type = static_cast<u8>(meta_type),
            .install_type = 0,
            .key_padding = {},
            .storage_id = storage_id,
            .storage_padding = {},
        });
    };

    append(real_id, FileSys::TitleType::Application, FileSys::ContentRecordType::Program);
    append(FileSys::GetUpdateTitleID(real_id), FileSys::TitleType::Update,
           FileSys::ContentRecordType::Program);
    const auto aocs =
        cp.ListEntriesFilter(FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);
    for (const auto& entry : aocs) {
        if (FileSys::GetBaseTitleID(entry.title_id) == real_id) {
            append(entry.title_id, FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);
        }
    }

    const std::size_t start =
        static_cast<std::size_t>((std::min)(offset, static_cast<u64>(records.size())));
    const std::size_t count = (std::min)(out_records.size(), records.size() - start);
    for (std::size_t i = 0; i < count; ++i) {
        out_records[i] = records[start + i];
    }
    *out_count = static_cast<s32>(count);
    R_SUCCEED();
}

Result IApplicationManagerInterface::TerminateApplication(u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, AM::ResultApplicationRecordNotFound);
    const auto applet = window_system->GetMainApplet();
    R_UNLESS(applet != nullptr, AM::ResultApplicationRecordNotFound);

    const u64 running_base_id = FileSys::GetBaseTitleID(applet->program_id);
    const u64 requested_base_id = FileSys::GetBaseTitleID(application_id);
    R_UNLESS(running_base_id == requested_base_id, AM::ResultApplicationRecordNotFound);

    applet->process->Terminate();
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationRecord(Out<ApplicationRecord> out_record,
                                                          u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);
    const auto& cache = system.GetContentProviderUnion();

    ApplicationRecord record{};
    record.application_id = application_id;
    record.last_event = cache.HasEntry(application_id, FileSys::ContentRecordType::Program)
                            ? ApplicationEvent::Installed
                            : ApplicationEvent::Archived;
    record.attributes = 0;
    record.last_updated = Core::LaunchTimestampCache::GetLaunchTimestamp(application_id);
    *out_record = record;
    R_SUCCEED();
}

Result IApplicationManagerInterface::HasApplicationRecord(Out<bool> out_has_record,
                                                          u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);
    *out_has_record =
        system.GetContentProviderUnion().HasEntry(application_id, FileSys::ContentRecordType::Program);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationRecordUpdateSystemEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_NS, "called");

    // The handle itself is passive. Mutating application-record operations
    // signal this event when the underlying record set actually changes.
    *out_event = record_update_system_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationShellEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_NS, "called");
    application_shell_event.Clear();
    *out_event = application_shell_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::PopApplicationShellEventInfo(
    Out<u32> out_event_info_type, Out<u64> out_application_id) {
    LOG_DEBUG(Service_NS, "called");
    *out_event_info_type = 0;
    *out_application_id = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::CalculateApplicationOccupiedSize(
    Out<ApplicationOccupiedSize> out_size, u64 application_id) {
    R_RETURN(IContentManagementInterface(system).CalculateApplicationOccupiedSize(out_size,
                                                                                  application_id));
}

Result IApplicationManagerInterface::GetApplicationControlProperty(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, u64 application_id) {
    u32 actual_size{};
    R_RETURN(IReadOnlyApplicationControlDataInterface(system).GetApplicationControlData(
        out_buffer, &actual_size, ApplicationControlSource::Storage, application_id));
}

Result IApplicationManagerInterface::GetGameCardAttachmentEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    *out_event = gamecard_mount_status_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetSdCardRemovedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    *out_event = sd_card_mount_status_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::ListApplicationIdOnGameCard(
    OutArray<u64, BufferAttr_HipcMapAlias> out_ids, Out<s32> out_count) {
    // Encore has no physical Switch game-card backend on PS5. This matches
    // IsGameCardInserted=false and reports an empty, internally consistent list.
    (void)out_ids;
    *out_count = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::ListApplicationContentMetaStatus(
    OutArray<ApplicationContentMetaStatus, BufferAttr_HipcMapAlias> out_statuses,
    Out<s32> out_count, s32 index, u64 application_id) {
    R_RETURN(IContentManagementInterface(system).ListApplicationContentMetaStatus(
        out_statuses, out_count, index, application_id));
}

Result IApplicationManagerInterface::CountApplicationContentMeta(
    Out<s32> out_count, u64 application_id) {
    const auto& disabled = Settings::values.disabled_addons[application_id];
    if (std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end()) {
        *out_count = 0;
        R_SUCCEED();
    }

    const auto installed = system.GetContentProviderUnion().ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);
    s32 count = 0;
    for (const auto& [slot, entry] : installed) {
        if (FileSys::GetBaseTitleID(entry.title_id) == application_id) {
            ++count;
        }
    }
    *out_count = count;
    R_SUCCEED();
}

Result IApplicationManagerInterface::ListAvailableAddOnContent(
    OutArray<u32, BufferAttr_HipcMapAlias> out_addons, Out<u32> out_count, u32 offset,
    u64 application_id) {
    std::vector<u32> addons;
    const auto& disabled = Settings::values.disabled_addons[application_id];
    if (std::find(disabled.begin(), disabled.end(), "DLC") == disabled.end()) {
        const auto installed = system.GetContentProviderUnion().ListEntriesFilterOrigin(
            std::nullopt, FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);
        for (const auto& [slot, entry] : installed) {
            if (FileSys::GetBaseTitleID(entry.title_id) == application_id) {
                addons.push_back(static_cast<u32>(FileSys::GetAOCID(entry.title_id)));
            }
        }
        std::sort(addons.begin(), addons.end());
        addons.erase(std::unique(addons.begin(), addons.end()), addons.end());
    }

    R_UNLESS(offset <= addons.size(), AM::ResultApplicationRecordNotFound);
    const auto count = (std::min)(out_addons.size(), addons.size() - offset);
    for (std::size_t i = 0; i < count; ++i) {
        out_addons[i] = addons[offset + i];
    }
    *out_count = static_cast<u32>(count);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetGameCardMountFailureEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_NS, "called; game-card mount-failure producer is not modeled");
    *out_event = gamecard_mount_failure_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetGameCardWakenReadyEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_NS, "called; game-card wake-ready producer is not modeled");
    *out_event = gamecard_waken_ready_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsGameCardApplicationRunning(Out<bool> out_is_running) {
    LOG_DEBUG(Service_NS, "called");
    *out_is_running = false;

    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    if (window_system == nullptr) {
        R_SUCCEED();
    }

    const auto applet = window_system->GetMainApplet();
    if (applet == nullptr) {
        R_SUCCEED();
    }

    auto* const gamecard_contents = system.GetFileSystemController().GetGameCardContents();
    if (gamecard_contents == nullptr) {
        R_SUCCEED();
    }

    *out_is_running =
        gamecard_contents->HasEntry(applet->program_id, FileSys::ContentRecordType::Program);
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown936(Out<u64> out_result) {
    LOG_WARNING(Service_NS, "(STUBBED) called.");
    *out_result = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::NeedsUpdateVulnerability(
    Out<bool> out_needs_update_vulnerability) {
    LOG_DEBUG(Service_NS, "called");
    // Encore does not model Nintendo's safe-system-version vulnerability list.
    // With no forced-vulnerability state present, no system update is required.
    *out_needs_update_vulnerability = false;
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsAnyApplicationEntityInstalled(
    Out<bool> out_is_any_application_entity_installed) {
    LOG_DEBUG(Service_NS, "called");
    const auto& cache = system.GetContentProviderUnion();
    const auto installed = cache.ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::Application, FileSys::ContentRecordType::Program);

    *out_is_any_application_entity_installed =
        std::any_of(installed.begin(), installed.end(), [](const auto& entry) {
            const auto title_id = entry.second.title_id;
            return title_id >= 0x0100000000001FFFULL && (title_id & 0xFFF) == 0;
        });
    R_SUCCEED();
}

Result IApplicationManagerInterface::DeleteApplicationEntity(u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    auto& fsc = system.GetFileSystemController();
    if (auto* const user_cache = fsc.GetUserNANDContents(); user_cache != nullptr) {
        user_cache->RemoveExistingEntry(application_id);
        user_cache->Refresh();
    }
    if (auto* const sdmc_cache = fsc.GetSDMCContents(); sdmc_cache != nullptr) {
        sdmc_cache->RemoveExistingEntry(application_id);
        sdmc_cache->Refresh();
    }

    record_update_system_event.Signal(system.Kernel());
    R_SUCCEED();
}

Result IApplicationManagerInterface::DeleteApplicationCompletely(u64 application_id) {
    R_RETURN(DeleteApplicationEntity(application_id));
}

Result IApplicationManagerInterface::GetApplicationViewDeprecated(
    OutArray<ApplicationViewV19, BufferAttr_HipcMapAlias> out_application_views,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const auto size = (std::min)(out_application_views.size(), application_ids.size());
    LOG_WARNING(Service_NS, "(STUBBED) called, size={}", application_ids.size());

    for (size_t i = 0; i < size; i++) {
        ApplicationViewV19 view{};
        view.application_id = application_ids[i];
        view.version = 0x70000;
        view.flags = 0x401f17;

        out_application_views[i] = view;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationViewWithPromotionInfo(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
    Out<u32> out_count,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const auto requested = application_ids.size();
    LOG_WARNING(Service_NS, "called, size={}", requested);

    const auto fw_pair = FirmwareManager::GetFirmwareVersion(system);
    const bool is_fw20 = fw_pair.first.major >= 20;

    const size_t per_entry_size = is_fw20 ? (sizeof(ApplicationViewV20) + sizeof(PromotionInfo))
                                             : (sizeof(ApplicationViewV19) + sizeof(PromotionInfo));
    const size_t capacity_entries = out_buffer.size() / per_entry_size;
    const size_t to_write_entries = (std::min)(requested, capacity_entries);

    u8* dst = out_buffer.data();
    for (size_t i = 0; i < to_write_entries; ++i) {
        ApplicationViewWithPromotionData data{};
        data.view.application_id = application_ids[i];
        data.view.version = 0x70000;
        data.view.unk = 0;
        data.view.flags = 0x401f17;
        data.view.download_state = {};
        data.view.download_progress = {};
        data.promotion = {};

        const size_t written = WriteApplicationViewWithPromotion(dst, out_buffer.size() - (dst - out_buffer.data()), data, is_fw20);
        if (written == 0) {
            break;
        }
        dst += written;
    }

    *out_count = static_cast<u32>(dst - out_buffer.data()) / static_cast<u32>(per_entry_size);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationView(
    OutArray<ApplicationViewV20, BufferAttr_HipcMapAlias> out_application_views,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const auto size = (std::min)(out_application_views.size(), application_ids.size());
    LOG_WARNING(Service_NS, "(STUBBED) called, size={}", application_ids.size());

    for (size_t i = 0; i < size; i++) {
        ApplicationViewV20 view{};
        view.application_id = application_ids[i];
        view.version = 0x70000;
        view.unk = 0;
        view.flags = 0x401f17;

        out_application_views[i] = view;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationRightsOnClient(
    OutArray<ApplicationRightsOnClient, BufferAttr_HipcMapAlias> out_rights, Out<u32> out_count,
    u32 flags, u64 application_id, Uid account_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, flags={}, application_id={:016X}, account_id={}",
                flags, application_id, account_id.uuid.FormattedString());

    if (!out_rights.empty()) {
        ApplicationRightsOnClient rights{};
        rights.application_id = application_id;
        rights.uid = account_id.uuid;
        rights.flags = 0;
        rights.flags2 = 0;

        out_rights[0] = rights;
        *out_count = 1;
    } else {
        *out_count = 0;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::CheckSdCardMountStatus() {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IContentManagementInterface(system).CheckSdCardMountStatus());
}

Result IApplicationManagerInterface::GetSdCardMountStatusChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_NS, "called; SD mount-change producer is not modeled");
    *out_event = sd_card_mount_status_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetGameCardAttachmentInfo(Out<bool> out_is_attached) {
    LOG_DEBUG(Service_NS, "called");
    *out_is_attached = system.GetFileSystemController().GetGameCard() != nullptr;
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsGameCardInserted(Out<bool> out_is_inserted) {
    LOG_DEBUG(Service_NS, "called");
    *out_is_inserted = system.GetFileSystemController().GetGameCard() != nullptr;
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetTotalSpaceSize(Out<s64> out_total_space_size, FileSys::StorageId storage_id) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IContentManagementInterface(system).GetTotalSpaceSize(out_total_space_size, storage_id));
}

Result IApplicationManagerInterface::GetFreeSpaceSize(Out<s64> out_free_space_size, FileSys::StorageId storage_id) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IContentManagementInterface(system).GetFreeSpaceSize(out_free_space_size, storage_id));
}

Result IApplicationManagerInterface::GetGameCardUpdateDetectionEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_NS, "called; game-card update producer is not modeled");
    *out_event = gamecard_update_detection_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::LaunchSystemApplet(Out<u64> out_process_id) {
    constexpr u64 QLaunchProgramId = static_cast<u64>(AM::AppletProgramId::QLaunch);
    LOG_INFO(Service_NS, "called, program_id={:016X}", QLaunchProgramId);

    for (const auto& process : system.Kernel().GetProcessList()) {
        R_UNLESS(process->GetProgramId() != QLaunchProgramId, ResultUnknown);
    }

    auto* window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, ResultUnknown);

    auto process =
        AM::CreateProcess(system, QLaunchProgramId, 1, HLE::ApiVersion::HOS_VERSION_MAJOR);
    R_UNLESS(process != nullptr, ResultUnknown);

    const u64 process_id = process->GetProcessId();
    auto applet = std::make_shared<AM::Applet>(system, std::move(process), false);
    applet->program_id = QLaunchProgramId;
    applet->applet_id = AM::AppletId::QLaunch;
    applet->type = AM::AppletType::SystemApplet;
    applet->library_applet_mode = AM::LibraryAppletMode::AllForeground;
    applet->lifecycle_manager.SetFocusHandlingMode(false);
    applet->lifecycle_manager.SetOutOfFocusSuspendingEnabled(false);

    window_system->TrackApplet(applet, false);
    window_system->RequestHomeMenuToGetForeground();
    applet->process->Run();

    *out_process_id = process_id;
    R_SUCCEED();
}

Result IApplicationManagerInterface::TerminateSystemApplet(u64 program_id) {
    constexpr u64 QLaunchProgramId = static_cast<u64>(AM::AppletProgramId::QLaunch);
    LOG_DEBUG(Service_NS, "called, program_id={:016X}", program_id);
    R_UNLESS(program_id == QLaunchProgramId, ResultUnknown);

    for (const auto& process : system.Kernel().GetProcessList()) {
        if (process->GetProgramId() == program_id) {
            R_RETURN(process->Terminate(system.Kernel()));
        }
    }
    R_THROW(ResultUnknown);
}

Result IApplicationManagerInterface::TerminateOverlayApplet(u64 program_id) {
    LOG_DEBUG(Service_NS, "called, program_id={:016X}", program_id);

    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, ResultUnknown);
    const auto applet = window_system->GetOverlayDisplayApplet();
    R_UNLESS(applet != nullptr && applet->program_id == program_id, ResultUnknown);

    applet->process->Terminate();
    R_SUCCEED();
}

Result IApplicationManagerInterface::ResumeAll() {
    LOG_DEBUG(Service_NS, "called; no NS-owned suspended-application set is modeled");
    // Application suspension/resume is owned by the AM lifecycle/process layer.
    // NS has no separate suspended set in Encore to resume here.
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsQualificationTransitionSupportedByProcessId(
    Out<bool> out_is_supported, u64 process_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, process_id={}", process_id);
    *out_is_supported = true;
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetStorageSize(Out<s64> out_total_space_size,
                                                    Out<s64> out_free_space_size,
                                                    FileSys::StorageId storage_id) {
    LOG_INFO(Service_NS, "called, storage_id={}", storage_id);
    *out_total_space_size = system.GetFileSystemController().GetTotalSpaceSize(storage_id);
    *out_free_space_size = system.GetFileSystemController().GetFreeSpaceSize(storage_id);
    R_SUCCEED();
}

Result IApplicationManagerInterface::TouchApplication(u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    const auto& cache = system.GetContentProviderUnion();
    const auto installed = cache.ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::Application, FileSys::ContentRecordType::Program,
        application_id);
    R_UNLESS(!installed.empty(), AM::ResultApplicationRecordNotFound);

    // Encore does not persist NS "last touched" record metadata yet. Validate
    // the application record instead of returning success for arbitrary IDs.
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsApplicationAutoDeleteDisabled(
    Out<bool> out_disabled, u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    // Encore has no background application auto-delete backend on PS5. Report
    // the effective platform state instead of pretending that auto-delete is active.
    *out_disabled = true;
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsPatchAutoDeletableApplication(
    Out<bool> out_deletable, u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    // No NS auto-delete worker exists on Encore/PS5, so installed patches are
    // conservatively non-auto-deletable.
    *out_deletable = false;
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsApplicationUpdateRequested(Out<bool> out_update_required,
                                                                  Out<u32> out_update_version,
                                                                  u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    // Encore has no NS network/version-list request state. Match the emulator
    // contract used upstream: no outstanding application-update request.
    *out_update_required = false;
    *out_update_version = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::CheckApplicationLaunchVersion(u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    // Encore does not emulate Nintendo's pushed launch-version list; titles are
    // allowed to launch with the locally installed content version.
    R_SUCCEED();
}

Result IApplicationManagerInterface::SetApplicationTerminateResult(Result terminate_result, u32 pad,
                                                                   u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}, result={:#x}", application_id,
              terminate_result.GetInnerValue());
    static_cast<void>(pad);

    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, AM::ResultApplicationRecordNotFound);
    const auto applet = window_system->GetMainApplet();
    R_UNLESS(applet != nullptr && applet->program_id == application_id,
             AM::ResultApplicationRecordNotFound);

    std::scoped_lock lk{applet->lock};
    applet->terminate_result = terminate_result;
    R_SUCCEED();
}

Result IApplicationManagerInterface::ClearApplicationTerminateResult(u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, AM::ResultApplicationRecordNotFound);
    const auto applet = window_system->GetMainApplet();
    R_UNLESS(applet != nullptr && applet->program_id == application_id,
             AM::ResultApplicationRecordNotFound);

    std::scoped_lock lk{applet->lock};
    applet->terminate_result = ResultSuccess;
    R_SUCCEED();
}

Result IApplicationManagerInterface::LaunchApplicationByProgramId(
    u64* out_process_id, u8 program_index, u64 application_id, u64 program_id) {
    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, AM::ResultApplicationRecordNotFound);

    // WindowSystem::TrackApplet requires a single live application owner.
    R_UNLESS(window_system->GetMainApplet() == nullptr, ResultUnknown);

    application_id = FileSys::GetBaseTitleID(application_id);
    R_UNLESS(application_id != 0, AM::ResultApplicationRecordNotFound);
    R_UNLESS(FileSys::GetBaseTitleID(program_id) == application_id,
             AM::ResultApplicationRecordNotFound);

    const u64 derived_program_index = program_id - application_id;
    R_UNLESS(derived_program_index <= std::numeric_limits<u8>::max() &&
                 static_cast<u8>(derived_program_index) == program_index,
             AM::ResultApplicationRecordNotFound);

    auto& storage = system.GetContentProviderUnion();
    auto program_nca = storage.GetEntryRaw(program_id, FileSys::ContentRecordType::Program);
    R_UNLESS(program_nca != nullptr, AM::ResultApplicationRecordNotFound);

    std::vector<u8> control;
    std::unique_ptr<Loader::AppLoader> loader;
    Loader::ResultStatus load_result{};
    auto process = AM::CreateApplicationProcess(control, loader, load_result, system,
                                                std::move(program_nca), program_id,
                                                program_index);
    R_UNLESS(process != nullptr, ResultUnknown);

    const u64 process_id = process->GetProcessId();
    auto applet = std::make_shared<AM::Applet>(system, std::move(process), true);
    applet->program_id = program_id;
    applet->applet_id = AM::AppletId::Application;
    applet->type = AM::AppletType::Application;
    applet->library_applet_mode = AM::LibraryAppletMode::AllForeground;
    applet->lifecycle_manager.SetFocusState(system.Kernel(), AM::FocusState::InFocus);

    Core::LaunchTimestampCache::SaveLaunchTimestamp(program_id);
    window_system->TrackApplet(applet, true);
    window_system->RequestApplicationToGetForeground();
    applet->process->Run();

    *out_process_id = process_id;
    R_SUCCEED();
}

Result IApplicationManagerInterface::LaunchApplicationOld(
    Out<u64> out_process_id, u64 application_id) {
    LOG_INFO(Service_NS, "legacy launch called, application_id={:016X}", application_id);
    const u64 program_id = FileSys::GetBaseTitleID(application_id);
    R_RETURN(LaunchApplicationByProgramId(out_process_id.Get(), 0, program_id, program_id));
}

Result IApplicationManagerInterface::LaunchApplication(
    Out<u64> out_process_id, u8 program_index, ApplicationLaunchInfoV19 launch_info) {
    LOG_INFO(Service_NS,
             "called, application_id={:016X}, program_id={:016X}, program_index={}",
             launch_info.application_id, launch_info.program_id, program_index);

    const u64 application_id = FileSys::GetBaseTitleID(launch_info.application_id);
    const u64 program_id =
        launch_info.program_id != 0 ? launch_info.program_id : application_id + program_index;
    R_RETURN(LaunchApplicationByProgramId(out_process_id.Get(), program_index, application_id,
                                         program_id));
}

Result IApplicationManagerInterface::GetApplicationLaunchInfo(
    Out<ApplicationLaunchInfoV19> out_launch_info, u64 application_id) {
    auto& storage = system.GetContentProviderUnion();
    const u64 base_application_id = FileSys::GetBaseTitleID(application_id);
    const u64 program_id =
        storage.HasEntry(application_id, FileSys::ContentRecordType::Program)
            ? application_id
            : base_application_id;

    R_UNLESS(storage.HasEntry(program_id, FileSys::ContentRecordType::Program),
             AM::ResultApplicationRecordNotFound);

    const auto storage_id_for_slot = [](std::optional<FileSys::ContentProviderUnionSlot> slot) {
        if (!slot.has_value()) {
            return FileSys::StorageId::None;
        }
        switch (*slot) {
        case FileSys::ContentProviderUnionSlot::UserNAND:
            return FileSys::StorageId::NandUser;
        case FileSys::ContentProviderUnionSlot::SysNAND:
            return FileSys::StorageId::NandSystem;
        case FileSys::ContentProviderUnionSlot::SDMC:
            return FileSys::StorageId::SdCard;
        case FileSys::ContentProviderUnionSlot::FrontendManual:
            return FileSys::StorageId::Host;
        default:
            return FileSys::StorageId::None;
        }
    };

    FileSys::PatchManager patch_manager{program_id, system.GetFileSystemController(), storage};

    *out_launch_info = {};
    out_launch_info->application_id = base_application_id;
    out_launch_info->program_id = program_id;
    out_launch_info->version = patch_manager.GetGameVersion().value_or(0);
    out_launch_info->application_storage_id = static_cast<u8>(storage_id_for_slot(
        storage.GetSlotForEntry(program_id, FileSys::ContentRecordType::Program)));
    out_launch_info->update_storage_id = static_cast<u8>(storage_id_for_slot(
        storage.GetSlotForEntry(FileSys::GetUpdateTitleID(base_application_id),
                                FileSys::ContentRecordType::Program)));
    // GetApplicationLaunchInfo is the pre-launch form. Horizon fills the
    // application-instance id at 0x30 later, during CreateApplicationInstance.
    out_launch_info->application_instance_id = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetRunningApplicationProgramId(
    Out<u64> out_program_id) {
    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, AM::ResultApplicationRecordNotFound);
    const auto applet = window_system->GetMainApplet();
    R_UNLESS(applet != nullptr, AM::ResultApplicationRecordNotFound);

    *out_program_id = applet->program_id;
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetMainApplicationProgramIndexByApplicationLaunchInfo(
    Out<u8> out_program_index, ApplicationLaunchInfoV19 launch_info) {
    R_RETURN(GetMainApplicationProgramIndex(out_program_index, launch_info.application_id));
}

Result IApplicationManagerInterface::GetMainApplicationProgramIndex(
    Out<u8> out_program_index, u64 application_id) {
    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, AM::ResultApplicationRecordNotFound);
    const auto applet = window_system->GetMainApplet();
    R_UNLESS(applet != nullptr, AM::ResultApplicationRecordNotFound);

    const u64 running_program_id = applet->program_id;
    const u64 running_base_id = FileSys::GetBaseTitleID(running_program_id);
    const u64 requested_base_id = FileSys::GetBaseTitleID(application_id);
    R_UNLESS(running_base_id == requested_base_id, AM::ResultApplicationRecordNotFound);

    const u64 program_index = running_program_id - running_base_id;
    R_UNLESS(program_index <= std::numeric_limits<u8>::max(), AM::ResultApplicationRecordNotFound);
    *out_program_index = static_cast<u8>(program_index);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationTerminateResult(Out<Result> out_result,
                                                                   u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, AM::ResultApplicationRecordNotFound);
    const auto applet = window_system->GetMainApplet();
    R_UNLESS(applet != nullptr && applet->program_id == application_id,
             AM::ResultApplicationRecordNotFound);

    std::scoped_lock lk{applet->lock};
    *out_result = applet->terminate_result;
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetRawApplicationTerminateResult(
    Out<u32> out_raw_result, u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, AM::ResultApplicationRecordNotFound);
    const auto applet = window_system->GetMainApplet();
    R_UNLESS(applet != nullptr && applet->program_id == application_id,
             AM::ResultApplicationRecordNotFound);

    std::scoped_lock lk{applet->lock};
    *out_raw_result = applet->terminate_result.GetInnerValue();
    R_SUCCEED();
}

void IApplicationManagerInterface::RejectUnavailablePlatformCommand(HLERequestContext& ctx) {
    LOG_WARNING(Service_NS,
                "Rejected NS platform/network command: required Nintendo/update backend is unavailable");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result IApplicationManagerInterface::RejectDebugOnlyCommand() {
    LOG_WARNING(Service_NS, "Rejected unsupported debug-only NS command on release runtime");
    R_RETURN(ResultUnknown);
}

Result IApplicationManagerInterface::RequestDownloadApplicationControlDataInBackground(
    u64 control_source, u64 application_id) {
    LOG_INFO(Service_NS, "called, control_source={} app={:016X}",
             control_source, application_id);

    unknown_event.Signal(system.Kernel());
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3250(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = present_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3251(Out<s64> out_count,
                                                 OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_count = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3253(Out<s64> out_count,
                                                 OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                                 u64 arg) {
    LOG_WARNING(Service_NS, "(STUBBED) called, arg={:016X}", arg);
    *out_count = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3254(Out<s64> out_count,
                                                 OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_count = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3255(u8 arg, u64 id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, id={:016X}, arg={}", id, arg);
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3256(std::array<u8, 0x48> args) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3257(u64 id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, id={:016X}", id);
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3258(Out<s64> out_count,
                                                 OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                                 u64 arg) {
    LOG_WARNING(Service_NS, "(STUBBED) called, arg={:016X}", arg);
    *out_count = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3259(bool flag, u64 id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, id={:016X}, flag={}", id, flag);
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3260(bool flag1, bool flag2, u64 arg1, u64 arg2) {
    LOG_WARNING(Service_NS, "(STUBBED) called, arg1={:016X}, arg2={:016X}, flag1={}, flag2={}",
                arg1, arg2, flag1, flag2);
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3261(Out<bool> out_result, u64 arg) {
    LOG_WARNING(Service_NS, "(STUBBED) called, arg={:016X}", arg);
    *out_result = false;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown3262(Out<s64> out_count,
                                                 OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_count = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4022(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    unknown_event.Signal(system.Kernel());
    *out_event = unknown_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4023(Out<u64> out_result) {
    LOG_WARNING(Service_NS, "(STUBBED) called.");
    *out_result = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4042(OutInterface<IAsyncResult> out_interface,
                                                 OutCopyHandle<Kernel::KReadableEvent> out_event,
                                                 u64 arg1, u64 arg2) {
    LOG_WARNING(Service_NS, "(STUBBED) called, arg1={:016X}, arg2={:016X}", arg1, arg2);
    *out_event = unknown_event.GetHandle();
    *out_interface = std::make_shared<IAsyncResult>(system, &unknown_event);
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4053() {
    LOG_WARNING(Service_NS, "(STUBBED) called.");
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4105() {
    LOG_WARNING(Service_NS, "(STUBBED) called.");
    R_SUCCEED();
}

void IApplicationManagerInterface::PushApplicationRecord(HLERequestContext& ctx) {
    const auto record = ctx.ReadBuffer();
    u64 application_id{};
    if (record.size() >= sizeof(application_id)) {
        std::memcpy(&application_id, record.data(), sizeof(application_id));
    }

    LOG_DEBUG(Service_NS, "called, application_id={:016X}, size={}", application_id, record.size());

    auto& fsc = system.GetFileSystemController();
    if (auto* const user_cache = fsc.GetUserNANDContents(); user_cache != nullptr) {
        user_cache->Refresh();
    }
    if (auto* const sdmc_cache = fsc.GetSDMCContents(); sdmc_cache != nullptr) {
        sdmc_cache->Refresh();
    }

    record_update_system_event.Signal(system.Kernel());

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IApplicationManagerInterface::ListApplicationTitle(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "called");
    IReadOnlyApplicationControlDataInterface(system).ListApplicationTitle(ctx);
}

void IApplicationManagerInterface::ListApplicationIcon(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "called");
    IReadOnlyApplicationControlDataInterface(system).ListApplicationIcon(ctx);
}

} // namespace Service::NS
