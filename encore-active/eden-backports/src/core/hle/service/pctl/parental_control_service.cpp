// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <chrono>

#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/pctl/parental_control_service.h"
#include "core/hle/service/pctl/pctl_results.h"

namespace Service::PCTL {

IParentalControlService::IParentalControlService(Core::System& system_, Capability capability_,
                                                 u64 program_id_)
    : ServiceFramework{system_, "IParentalControlService"}, capability{capability_},
      program_id{program_id_},
      service_context{system_, "IParentalControlService"}, synchronization_event{service_context},
      unlinked_event{service_context}, pin_code_changed_event{service_context},
      request_suspension_event{service_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, D<&IParentalControlService::Initialize>, "Initialize"},
        {1001, D<&IParentalControlService::CheckFreeCommunicationPermission>, "CheckFreeCommunicationPermission"},
        {1002, D<&IParentalControlService::ConfirmLaunchApplicationPermission>, "ConfirmLaunchApplicationPermission"},
        {1003, D<&IParentalControlService::ConfirmResumeApplicationPermission>, "ConfirmResumeApplicationPermission"},
        {1004, D<&IParentalControlService::ConfirmSnsPostPermission>, "ConfirmSnsPostPermission"},
        {1005, D<&IParentalControlService::ConfirmSystemSettingsPermission>, "ConfirmSystemSettingsPermission"},
        {1006, D<&IParentalControlService::IsRestrictionTemporaryUnlocked>, "IsRestrictionTemporaryUnlocked"},
        {1007, D<&IParentalControlService::RevertRestrictionTemporaryUnlocked>, "RevertRestrictionTemporaryUnlocked"},
        {1008, D<&IParentalControlService::EnterRestrictedSystemSettings>, "EnterRestrictedSystemSettings"},
        {1009, D<&IParentalControlService::LeaveRestrictedSystemSettings>, "LeaveRestrictedSystemSettings"},
        {1010, D<&IParentalControlService::IsRestrictedSystemSettingsEntered>, "IsRestrictedSystemSettingsEntered"},
        {1011, D<&IParentalControlService::RevertRestrictedSystemSettingsEntered>, "RevertRestrictedSystemSettingsEntered"},
        {1012, &IParentalControlService::RejectUnavailableParentalBackend, "GetRestrictedFeatures"},
        {1013, D<&IParentalControlService::ConfirmStereoVisionPermission>, "ConfirmStereoVisionPermission"},
        {1014, &IParentalControlService::ConfirmPlayableApplicationVideoOld, "ConfirmPlayableApplicationVideoOld"},
        {1015, &IParentalControlService::ConfirmPlayableApplicationVideo, "ConfirmPlayableApplicationVideo"},
        {1016, &IParentalControlService::ConfirmShowNewsPermission, "ConfirmShowNewsPermission"},
        {1017, D<&IParentalControlService::EndFreeCommunication>, "EndFreeCommunication"},
        {1018, D<&IParentalControlService::IsFreeCommunicationAvailable>, "IsFreeCommunicationAvailable"},
        {1019, D<&IParentalControlService::ConfirmLaunchApplicationPermission>, "ConfirmLaunchApplicationPermission"},
        {1020, &IParentalControlService::RejectUnavailableParentalBackend, "ConfirmLaunchSharedApplicationPermission"}, //20.0.0+
        {1021, &IParentalControlService::RejectUnavailableParentalBackend, "TryBeginFreeCommunicationForStreamPlay"}, //21.0.0+
        {1022, &IParentalControlService::RejectUnavailableParentalBackend, "EndFreeCommunicationForStreamPlay"}, //21.0.0+
        {1031, D<&IParentalControlService::IsRestrictionEnabled>, "IsRestrictionEnabled"},
        {1032, D<&IParentalControlService::GetSafetyLevel>, "GetSafetyLevel"},
        {1033, &IParentalControlService::RejectUnavailableParentalBackend, "SetSafetyLevel"},
        {1034, &IParentalControlService::RejectUnavailableParentalBackend, "GetSafetyLevelSettings"},
        {1035, D<&IParentalControlService::GetCurrentSettings>, "GetCurrentSettings"},
        {1036, D<&IParentalControlService::SetCustomSafetyLevelSettings>, "SetCustomSafetyLevelSettings"},
        {1037, D<&IParentalControlService::GetDefaultRatingOrganization>, "GetDefaultRatingOrganization"},
        {1038, D<&IParentalControlService::SetDefaultRatingOrganization>, "SetDefaultRatingOrganization"},
        {1039, D<&IParentalControlService::GetFreeCommunicationApplicationListCount>, "GetFreeCommunicationApplicationListCount"},
        {1042, D<&IParentalControlService::AddToFreeCommunicationApplicationList>, "AddToFreeCommunicationApplicationList"},
        {1043, D<&IParentalControlService::DeleteSettings>, "DeleteSettings"},
        {1044, D<&IParentalControlService::GetFreeCommunicationApplicationList>, "GetFreeCommunicationApplicationList"},
        {1045, D<&IParentalControlService::UpdateFreeCommunicationApplicationList>, "UpdateFreeCommunicationApplicationList"},
        {1046, D<&IParentalControlService::DisableFeaturesForReset>, "DisableFeaturesForReset"},
        {1047, &IParentalControlService::NotifyApplicationDownloadStarted, "NotifyApplicationDownloadStarted"},
        {1048, &IParentalControlService::NotifyNetworkProfileCreated, "NotifyNetworkProfileCreated"},
        {1049, D<&IParentalControlService::ResetFreeCommunicationApplicationList>, "ResetFreeCommunicationApplicationList"},
        {1050, D<&IParentalControlService::AddToFreeCommunicationApplicationList>, "AddToFreeCommunicationApplicationList"}, //20.0.0+
        {1051, &IParentalControlService::NotifyApplicationDownloadStarted, "NotifyApplicationDownloadStarted"}, //20.0.0+
        {1061, D<&IParentalControlService::ConfirmStereoVisionRestrictionConfigurable>, "ConfirmStereoVisionRestrictionConfigurable"},
        {1062, D<&IParentalControlService::GetStereoVisionRestriction>, "GetStereoVisionRestriction"},
        {1063, D<&IParentalControlService::SetStereoVisionRestriction>, "SetStereoVisionRestriction"},
        {1064, D<&IParentalControlService::ResetConfirmedStereoVisionPermission>, "ResetConfirmedStereoVisionPermission"},
        {1065, D<&IParentalControlService::IsStereoVisionPermitted>, "IsStereoVisionPermitted"},
        {1201, D<&IParentalControlService::UnlockRestrictionTemporarily>, "UnlockRestrictionTemporarily"},
        {1202, D<&IParentalControlService::UnlockSystemSettingsRestriction>, "UnlockSystemSettingsRestriction"},
        {1203, D<&IParentalControlService::SetPinCode>, "SetPinCode"},
        {1204, &IParentalControlService::RejectUnavailableParentalBackend, "GenerateInquiryCode"},
        {1205, &IParentalControlService::RejectUnavailableParentalBackend, "CheckMasterKey"},
        {1206, D<&IParentalControlService::GetPinCodeLength>, "GetPinCodeLength"},
        {1207, D<&IParentalControlService::GetPinCodeChangedEvent>, "GetPinCodeChangedEvent"},
        {1208, D<&IParentalControlService::GetPinCode>, "GetPinCode"},
        {1403, D<&IParentalControlService::IsPairingActive>, "IsPairingActive"},
        {1406, &IParentalControlService::RejectUnavailableParentalBackend, "GetSettingsLastUpdated"},
        {1411, &IParentalControlService::RejectUnavailableParentalBackend, "GetPairingAccountInfo"},
        {1421, &IParentalControlService::RejectUnavailableParentalBackend, "GetAccountNickname"},
        {1424, &IParentalControlService::RejectUnavailableParentalBackend, "GetAccountState"},
        {1425, &IParentalControlService::RejectUnavailableParentalBackend, "RequestPostEvents"},
        {1426, &IParentalControlService::RejectUnavailableParentalBackend, "GetPostEventInterval"},
        {1427, &IParentalControlService::RejectUnavailableParentalBackend, "SetPostEventInterval"},
        {1432, D<&IParentalControlService::GetSynchronizationEvent>, "GetSynchronizationEvent"},
        {1451, D<&IParentalControlService::StartPlayTimer>, "StartPlayTimer"},
        {1452, D<&IParentalControlService::StopPlayTimer>, "StopPlayTimer"},
        {1453, D<&IParentalControlService::IsPlayTimerEnabled>, "IsPlayTimerEnabled"},
        {1454, D<&IParentalControlService::GetPlayTimerRemainingTime>, "GetPlayTimerRemainingTime"},
        {1455, D<&IParentalControlService::IsRestrictedByPlayTimer>, "IsRestrictedByPlayTimer"},
        {1456, D<&IParentalControlService::GetPlayTimerSettingsOld>, "GetPlayTimerSettingsOld"},
        {1457, D<&IParentalControlService::GetPlayTimerEventToRequestSuspension>, "GetPlayTimerEventToRequestSuspension"},
        {1458, D<&IParentalControlService::IsPlayTimerAlarmDisabled>, "IsPlayTimerAlarmDisabled"},
        {1459, D<&IParentalControlService::GetPlayTimerRemainingTimeDisplayInfo>, "GetPlayTimerRemainingTimeDisplayInfo"},
        {1460, D<&IParentalControlService::Unknown1460>, "Unknown1460"},
        {1471, &IParentalControlService::NotifyWrongPinCodeInputManyTimes, "NotifyWrongPinCodeInputManyTimes"},
        {1472, &IParentalControlService::CancelNetworkRequest, "CancelNetworkRequest"},
        {1473, D<&IParentalControlService::GetUnlinkedEvent>, "GetUnlinkedEvent"},
        {1474, D<&IParentalControlService::ClearUnlinkedEvent>, "ClearUnlinkedEvent"},
        {1475, &IParentalControlService::RejectUnavailableParentalBackend, "GetExtendedPlayTimerEvent"}, // 18.0.0+
        {1601, D<&IParentalControlService::DisableAllFeatures>, "DisableAllFeatures"},
        {1602, D<&IParentalControlService::PostEnableAllFeatures>, "PostEnableAllFeatures"},
        {1603, D<&IParentalControlService::IsAllFeaturesDisabled>, "IsAllFeaturesDisabled"},
        {1901, D<&IParentalControlService::DeleteFromFreeCommunicationApplicationListForDebug>, "DeleteFromFreeCommunicationApplicationListForDebug"},
        {1902, D<&IParentalControlService::ClearFreeCommunicationApplicationListForDebug>, "ClearFreeCommunicationApplicationListForDebug"},
        {1903, D<&IParentalControlService::RejectDebugOnlyCommand>, "GetExemptApplicationListCountForDebug"},
        {1904, D<&IParentalControlService::RejectDebugOnlyCommand>, "GetExemptApplicationListForDebug"},
        {1905, D<&IParentalControlService::RejectDebugOnlyCommand>, "UpdateExemptApplicationListForDebug"},
        {1906, D<&IParentalControlService::RejectDebugOnlyCommand>, "AddToExemptApplicationListForDebug"},
        {1907, D<&IParentalControlService::RejectDebugOnlyCommand>, "DeleteFromExemptApplicationListForDebug"},
        {1908, D<&IParentalControlService::RejectDebugOnlyCommand>, "ClearExemptApplicationListForDebug"},
        {1941, D<&IParentalControlService::DeletePairing>, "DeletePairing"},
        {1951, D<&IParentalControlService::RejectDebugOnlyCommand>, "SetPlayTimerSettingsForDebug"},
        {1952, D<&IParentalControlService::RejectDebugOnlyCommand>, "GetPlayTimerSpentTimeForTest"},
        {1953, D<&IParentalControlService::RejectDebugOnlyCommand>, "SetPlayTimerAlarmDisabledForDebug"},
        {1954, &IParentalControlService::RejectUnavailableParentalBackend, "IsBedtimeAlarmEnabled"}, // 18.0.0+
        {1955, &IParentalControlService::RejectUnavailableParentalBackend, "GetBedtimeAlarmTime"}, // 18.0.0+
        {1956, &IParentalControlService::RejectUnavailableParentalBackend, "GetBedtimeAlarmTimeHour"}, // 18.0.0+
        {1957, &IParentalControlService::RejectUnavailableParentalBackend, "GetBedtimeAlarmTimeMinute"}, // 18.0.0+
        {2001, &IParentalControlService::RejectUnavailableParentalBackend, "RequestPairingAsync"},
        {2002, &IParentalControlService::RejectUnavailableParentalBackend, "FinishRequestPairing"},
        {2003, &IParentalControlService::RejectUnavailableParentalBackend, "AuthorizePairingAsync"},
        {2004, &IParentalControlService::RejectUnavailableParentalBackend, "FinishAuthorizePairing"},
        {2005, &IParentalControlService::RejectUnavailableParentalBackend, "RetrievePairingInfoAsync"},
        {2006, &IParentalControlService::RejectUnavailableParentalBackend, "FinishRetrievePairingInfo"},
        {2007, &IParentalControlService::RejectUnavailableParentalBackend, "UnlinkPairingAsync"},
        {2008, &IParentalControlService::RejectUnavailableParentalBackend, "FinishUnlinkPairing"},
        {2009, &IParentalControlService::RejectUnavailableParentalBackend, "GetAccountMiiImageAsync"},
        {2010, &IParentalControlService::RejectUnavailableParentalBackend, "FinishGetAccountMiiImage"},
        {2011, &IParentalControlService::RejectUnavailableParentalBackend, "GetAccountMiiImageContentTypeAsync"},
        {2012, &IParentalControlService::RejectUnavailableParentalBackend, "FinishGetAccountMiiImageContentType"},
        {2013, &IParentalControlService::RejectUnavailableParentalBackend, "SynchronizeParentalControlSettingsAsync"},
        {2014, &IParentalControlService::RejectUnavailableParentalBackend, "FinishSynchronizeParentalControlSettings"},
        {2015, &IParentalControlService::RejectUnavailableParentalBackend, "FinishSynchronizeParentalControlSettingsWithLastUpdated"},
        {2016, &IParentalControlService::RejectUnavailableParentalBackend, "RequestUpdateExemptionListAsync"}, //5.0.0+
        {145601, D<&IParentalControlService::GetPlayTimerSettings>, "GetPlayTimerSettings"}, // 18.0.0+
        {2017, &IParentalControlService::RejectUnavailableParentalBackend, "AuthorizePairingAsync"}, //19.0.0+
        {2019, &IParentalControlService::RejectUnavailableParentalBackend, "RequestUpdateDeviceUsersBackground"}, //19.0.0+
        {2021, &IParentalControlService::RejectUnavailableParentalBackend, "RequestCopyPairingAsync"}, //20.0.0+
        {2022, &IParentalControlService::RejectUnavailableParentalBackend, "FinishRequestCopyPairing"}, //20.0.0+
        {2023, &IParentalControlService::RejectUnavailableParentalBackend, "IsFromPairingActiveDevice"}, //20.0.0+
        {2024, &IParentalControlService::RejectUnavailableParentalBackend, "RollbackCopyPairing"}, //21.0.0+
        {3001, &IParentalControlService::RejectUnavailableParentalBackend, "GetErrorContextChangedEvent"}, //20.0.0+
        {145601, D<&IParentalControlService::GetPlayTimerSettings>, "GetPlayTimerSettings"}, // 18.0.0+
        {195101, D<&IParentalControlService::SetPlayTimerSettings>, "SetPlayTimerSettingsForDebug"}, //18.0.0+
    };
    // clang-format on
    RegisterHandlers(functions);
}

IParentalControlService::~IParentalControlService() = default;

bool IParentalControlService::CheckFreeCommunicationPermissionImpl() const {
    if (states.temporary_unlocked) {
        return true;
    }
    if ((states.application_info.parental_control_flag & 1) == 0) {
        return true;
    }
    if (pin_code[0] == '\0') {
        return true;
    }
    if (!settings.is_free_communication_default_on) {
        return true;
    }
    // TODO(ogniK): Check for blacklisted/exempted applications. Return false can happen here
    // but as we don't have multiproceses support yet, we can just assume our application is
    // valid for the time being
    return true;
}

bool IParentalControlService::ConfirmStereoVisionPermissionImpl() const {
    if (states.temporary_unlocked) {
        return true;
    }
    if (pin_code[0] == '\0') {
        return true;
    }
    if (!settings.is_stero_vision_restricted) {
        return false;
    }
    return true;
}

void IParentalControlService::SetStereoVisionRestrictionImpl(bool is_restricted) {
    if (settings.disabled) {
        return;
    }

    if (pin_code[0] == '\0') {
        return;
    }
    settings.is_stero_vision_restricted = is_restricted;
}

void IParentalControlService::RejectUnavailableParentalBackend(HLERequestContext& ctx) {
    LOG_WARNING(Service_PCTL,
                "Rejected parental-control account/pairing/system-settings command: backend unavailable");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result IParentalControlService::RejectDebugOnlyCommand() {
    LOG_WARNING(Service_PCTL, "Rejected unsupported parental-control debug command");
    R_RETURN(ResultUnknown);
}

Result IParentalControlService::Initialize() {
    LOG_DEBUG(Service_PCTL, "called");

    if (False(capability & (Capability::Application | Capability::System))) {
        LOG_ERROR(Service_PCTL, "Invalid capability! capability={:X}", capability);
        R_THROW(PCTL::ResultNoCapability);
    }

    // TODO(ogniK): Recovery flag initialization for pctl:r

    if (program_id != 0) {
        const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                       system.GetContentProvider()};
        const auto control = pm.GetControlMetadata();
        if (control.first) {
            states.tid_from_event = 0;
            states.launch_time_valid = false;
            states.is_suspended = false;
            states.free_communication = false;
            states.stereo_vision = false;
            states.application_info = ApplicationInfo{
                .application_id = program_id,
                .age_rating = control.first->GetRatingAge(),
                .parental_control_flag = control.first->GetParentalControlFlag(),
                .capability = capability,
            };

            if (False(capability & (Capability::System | Capability::Recovery))) {
                // TODO(ogniK): Signal application launch event
            }
        }
    }

    R_SUCCEED();
}

Result IParentalControlService::CheckFreeCommunicationPermission() {
    LOG_DEBUG(Service_PCTL, "called");

    if (!CheckFreeCommunicationPermissionImpl()) {
        R_THROW(PCTL::ResultNoFreeCommunication);
    } else {
        states.free_communication = true;
        R_SUCCEED();
    }
}

Result IParentalControlService::ConfirmLaunchApplicationPermission(
    InBuffer<BufferAttr_HipcPointer> restriction_bitset, u64 nacp_flag, u64 application_id) {
    // Encore does not expose the Horizon parental-confirmation UI. With no configured local
    // restriction backend, allow launch rather than blocking guest startup on an unavailable UI.
    LOG_DEBUG(Service_PCTL,
              "Launch permission accepted locally, nacp_flag={:#x} application_id={:016X}",
              nacp_flag, application_id);
    R_SUCCEED();
}

Result IParentalControlService::ConfirmResumeApplicationPermission(
    InBuffer<BufferAttr_HipcPointer> restriction_bitset, u64 nacp_flag, u64 application_id) {
    // Same policy as launch: there is no interactive parental-confirmation UI to satisfy.
    LOG_DEBUG(Service_PCTL,
              "Resume permission accepted locally, nacp_flag={:#x} application_id={:016X}",
              nacp_flag, application_id);
    R_SUCCEED();
}

Result IParentalControlService::ConfirmSnsPostPermission() {
    // Do not unconditionally reject SNS posting when no parental-control backend/PIN is configured.
    // The emulator has no Horizon confirmation UI, so use the same permissive local policy as free
    // communication rather than inventing a restriction.
    LOG_DEBUG(Service_PCTL, "SNS-post permission accepted by local parental-control policy");
    R_SUCCEED();
}

Result IParentalControlService::ConfirmSystemSettingsPermission() {
    // No parental-control system-settings confirmation UI is exposed by Encore.
    LOG_DEBUG(Service_PCTL, "System-settings permission accepted locally");
    R_SUCCEED();
}

Result IParentalControlService::IsRestrictionTemporaryUnlocked(
    Out<bool> out_is_temporary_unlocked) {
    *out_is_temporary_unlocked = states.temporary_unlocked;
    LOG_DEBUG(Service_PCTL, "called, is_temporary_unlocked={}", *out_is_temporary_unlocked);
    R_SUCCEED();
}

Result IParentalControlService::RevertRestrictionTemporaryUnlocked() {
    states.temporary_unlocked = false;
    LOG_DEBUG(Service_PCTL, "Temporary restriction unlock reverted");
    R_SUCCEED();
}

Result IParentalControlService::EnterRestrictedSystemSettings() {
    states.restricted_system_settings_entered = true;
    LOG_DEBUG(Service_PCTL, "Entered restricted system-settings session");
    R_SUCCEED();
}

Result IParentalControlService::LeaveRestrictedSystemSettings() {
    states.restricted_system_settings_entered = false;
    LOG_DEBUG(Service_PCTL, "Left restricted system-settings session");
    R_SUCCEED();
}

Result IParentalControlService::IsRestrictedSystemSettingsEntered(
    Out<bool> out_is_restricted_system_settings_entered) {
    *out_is_restricted_system_settings_entered = states.restricted_system_settings_entered;
    LOG_DEBUG(Service_PCTL, "called, entered={}", *out_is_restricted_system_settings_entered);
    R_SUCCEED();
}

Result IParentalControlService::RevertRestrictedSystemSettingsEntered() {
    states.restricted_system_settings_entered = false;
    LOG_DEBUG(Service_PCTL, "Restricted system-settings state reverted");
    R_SUCCEED();
}

Result IParentalControlService::ConfirmStereoVisionPermission() {
    LOG_DEBUG(Service_PCTL, "called");
    states.stereo_vision = true;
    R_SUCCEED();
}

void IParentalControlService::ConfirmPlayableApplicationVideoOld(HLERequestContext& ctx) {
    LOG_DEBUG(Service_PCTL, "Playable-application video permission accepted by local policy");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IParentalControlService::ConfirmPlayableApplicationVideo(HLERequestContext& ctx) {
    LOG_DEBUG(Service_PCTL, "Playable-application video permission accepted by local policy");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IParentalControlService::ConfirmShowNewsPermission(HLERequestContext& ctx) {
    LOG_DEBUG(Service_PCTL, "News permission accepted by local policy");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

Result IParentalControlService::EndFreeCommunication() {
    states.free_communication = false;
    LOG_DEBUG(Service_PCTL, "Free-communication session ended");
    R_SUCCEED();
}

Result IParentalControlService::IsFreeCommunicationAvailable() {
    LOG_DEBUG(Service_PCTL, "called");

    if (!CheckFreeCommunicationPermissionImpl()) {
        R_THROW(PCTL::ResultNoFreeCommunication);
    } else {
        R_SUCCEED();
    }
}

Result IParentalControlService::IsRestrictionEnabled(Out<bool> out_restriction_enabled) {
    LOG_DEBUG(Service_PCTL, "called");

    if (False(capability & (Capability::Status | Capability::Recovery))) {
        LOG_ERROR(Service_PCTL, "Application does not have Status or Recovery capabilities!");
        *out_restriction_enabled = false;
        R_THROW(PCTL::ResultNoCapability);
    }

    *out_restriction_enabled = pin_code[0] != '\0';
    R_SUCCEED();
}

Result IParentalControlService::GetSafetyLevel(Out<u32> out_safety_level) {
    // No configured parental safety level is persisted by Encore.
    *out_safety_level = 0;
    LOG_DEBUG(Service_PCTL, "Returning unrestricted safety level={}", *out_safety_level);
    R_SUCCEED();
}

Result IParentalControlService::GetCurrentSettings(Out<RestrictionSettings> out_settings) {
    LOG_INFO(Service_PCTL, "called");
    *out_settings = restriction_settings;
    R_SUCCEED();
}

Result IParentalControlService::SetCustomSafetyLevelSettings(
    const RestrictionSettings& settings_) {
    restriction_settings = settings_;
    LOG_DEBUG(Service_PCTL,
              "Custom restrictions updated: rating_age={}, sns={}, free_communication={}",
              restriction_settings.rating_age, restriction_settings.sns_post_restriction,
              restriction_settings.free_communication_restriction);
    R_SUCCEED();
}

Result IParentalControlService::GetDefaultRatingOrganization(
    Out<u32> out_rating_organization) {
    *out_rating_organization = default_rating_organization;
    LOG_DEBUG(Service_PCTL, "called, organization={}", *out_rating_organization);
    R_SUCCEED();
}

Result IParentalControlService::SetDefaultRatingOrganization(u32 rating_organization) {
    default_rating_organization = rating_organization;
    LOG_DEBUG(Service_PCTL, "called, organization={}", default_rating_organization);
    R_SUCCEED();
}

Result IParentalControlService::GetFreeCommunicationApplicationListCount(Out<s32> out_count) {
    *out_count = static_cast<s32>(free_communication_application_list.size());
    LOG_DEBUG(Service_PCTL, "called, count={}", *out_count);
    R_SUCCEED();
}

Result IParentalControlService::AddToFreeCommunicationApplicationList(u64 application_id) {
    const auto get_application_id = [](const std::array<u8, 0x10>& info) {
        u64 id{};
        std::memcpy(&id, info.data(), sizeof(id));
        return id;
    };
    const auto it = std::find_if(free_communication_application_list.begin(),
                                 free_communication_application_list.end(),
                                 [application_id, &get_application_id](const auto& info) {
                                     return get_application_id(info) == application_id;
                                 });
    if (it == free_communication_application_list.end()) {
        std::array<u8, 0x10> info{};
        std::memcpy(info.data(), &application_id, sizeof(application_id));
        info[8] = 1;
        free_communication_application_list.push_back(info);
    } else {
        (*it)[8] = 1;
    }
    R_SUCCEED();
}

Result IParentalControlService::GetFreeCommunicationApplicationList(
    Out<u32> out_count,
    OutArray<std::array<u8, 0x10>, BufferAttr_HipcMapAlias> out_list, u32 offset) {
    if (offset >= free_communication_application_list.size()) {
        *out_count = 0;
        R_SUCCEED();
    }
    const auto count = (std::min)(out_list.size(),
                                  free_communication_application_list.size() - offset);
    std::copy_n(free_communication_application_list.begin() + offset, count, out_list.begin());
    *out_count = static_cast<u32>(count);
    R_SUCCEED();
}

Result IParentalControlService::UpdateFreeCommunicationApplicationList(
    InArray<std::array<u8, 0x10>, BufferAttr_HipcMapAlias> list) {
    const auto get_application_id = [](const std::array<u8, 0x10>& info) {
        u64 id{};
        std::memcpy(&id, info.data(), sizeof(id));
        return id;
    };
    for (const auto& incoming : list) {
        const auto application_id = get_application_id(incoming);
        const auto it = std::find_if(free_communication_application_list.begin(),
                                     free_communication_application_list.end(),
                                     [application_id, &get_application_id](const auto& info) {
                                         return get_application_id(info) == application_id;
                                     });
        if (it == free_communication_application_list.end()) {
            free_communication_application_list.push_back(incoming);
        } else {
            *it = incoming;
        }
    }
    R_SUCCEED();
}

Result IParentalControlService::ResetFreeCommunicationApplicationList() {
    free_communication_application_list.clear();
    R_SUCCEED();
}

Result IParentalControlService::DeleteFromFreeCommunicationApplicationListForDebug(
    u64 application_id) {
    free_communication_application_list.erase(
        std::remove_if(free_communication_application_list.begin(),
                       free_communication_application_list.end(),
                       [application_id](const auto& info) {
                           u64 id{};
                           std::memcpy(&id, info.data(), sizeof(id));
                           return id == application_id;
                       }),
        free_communication_application_list.end());
    R_SUCCEED();
}

Result IParentalControlService::ClearFreeCommunicationApplicationListForDebug() {
    free_communication_application_list.clear();
    R_SUCCEED();
}

Result IParentalControlService::DeleteSettings() {
    settings = {};
    restriction_settings = {};
    default_rating_organization = 0;
    free_communication_application_list.clear();
    pin_code.fill('\0');
    states.temporary_unlocked = false;
    states.restricted_system_settings_entered = false;
    pin_code_changed_event.Signal(system.Kernel());
    synchronization_event.Signal(system.Kernel());
    LOG_DEBUG(Service_PCTL, "Local parental-control settings reset");
    R_SUCCEED();
}

Result IParentalControlService::DisableFeaturesForReset() {
    settings.disabled = true;
    LOG_DEBUG(Service_PCTL, "Parental-control feature effects disabled for reset");
    R_SUCCEED();
}

void IParentalControlService::NotifyApplicationDownloadStarted(HLERequestContext& ctx) {
    LOG_DEBUG(Service_PCTL, "Application-download notification acknowledged");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IParentalControlService::NotifyNetworkProfileCreated(HLERequestContext& ctx) {
    LOG_DEBUG(Service_PCTL, "Network-profile notification acknowledged");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

Result IParentalControlService::ConfirmStereoVisionRestrictionConfigurable() {
    LOG_DEBUG(Service_PCTL, "called");

    if (False(capability & Capability::StereoVision)) {
        LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
        R_THROW(PCTL::ResultNoCapability);
    }

    if (pin_code[0] == '\0') {
        R_THROW(PCTL::ResultNoRestrictionEnabled);
    }

    R_SUCCEED();
}

Result IParentalControlService::IsStereoVisionPermitted(Out<bool> out_is_permitted) {
    LOG_DEBUG(Service_PCTL, "called");

    if (!ConfirmStereoVisionPermissionImpl()) {
        *out_is_permitted = false;
        R_THROW(PCTL::ResultStereoVisionRestricted);
    } else {
        *out_is_permitted = true;
        R_SUCCEED();
    }
}

Result IParentalControlService::UnlockRestrictionTemporarily(
    InArray<char, BufferAttr_HipcPointer> pin_code_buffer) {
    std::size_t stored_length = 0;
    while (stored_length < pin_code.size() && pin_code[stored_length] != '\0') {
        ++stored_length;
    }

    const bool matches =
        pin_code_buffer.size() == stored_length &&
        std::equal(pin_code_buffer.begin(), pin_code_buffer.end(), pin_code.begin());
    R_UNLESS(matches, PCTL::ResultInvalidPin);

    states.temporary_unlocked = true;
    LOG_DEBUG(Service_PCTL, "Temporary restriction unlock enabled");
    R_SUCCEED();
}

Result IParentalControlService::UnlockSystemSettingsRestriction() {
    states.temporary_unlocked = true;
    states.restricted_system_settings_entered = true;
    LOG_DEBUG(Service_PCTL, "System-settings restriction unlocked for current service state");
    R_SUCCEED();
}

Result IParentalControlService::SetPinCode(
    InArray<char, BufferAttr_HipcPointer> pin_code_buffer) {
    const auto copy_size = (std::min)(pin_code_buffer.size(), pin_code.size());
    if (pin_code_buffer.size() > pin_code.size()) {
        LOG_WARNING(Service_PCTL, "PIN buffer exceeds local 8-byte state; truncating {} -> {}",
                    pin_code_buffer.size(), copy_size);
    }

    pin_code.fill('\0');
    std::copy_n(pin_code_buffer.begin(), copy_size, pin_code.begin());
    pin_code_changed_event.Signal(system.Kernel());

    LOG_DEBUG(Service_PCTL, "PIN updated, stored_length={}", copy_size);
    R_SUCCEED();
}

Result IParentalControlService::GetPinCodeLength(Out<s32> out_length) {
    s32 length = 0;
    while (length < static_cast<s32>(pin_code.size()) &&
           pin_code[static_cast<std::size_t>(length)] != '\0') {
        ++length;
    }
    *out_length = length;
    LOG_DEBUG(Service_PCTL, "called, length={}", *out_length);
    R_SUCCEED();
}

Result IParentalControlService::GetPinCodeChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    *out_event = pin_code_changed_event.GetHandle();
    LOG_DEBUG(Service_PCTL, "called");
    R_SUCCEED();
}

Result IParentalControlService::GetPinCode(
    Out<s32> out_length, OutArray<char, BufferAttr_HipcPointer> out_pin_code) {
    std::size_t length = 0;
    while (length < pin_code.size() && pin_code[length] != '\0') {
        ++length;
    }

    const auto copy_size = (std::min)(length, out_pin_code.size());
    std::copy_n(pin_code.begin(), copy_size, out_pin_code.begin());
    *out_length = static_cast<s32>(copy_size);

    LOG_DEBUG(Service_PCTL, "called, stored_length={}, returned_length={}", length, copy_size);
    R_SUCCEED();
}

Result IParentalControlService::IsPairingActive(Out<bool> out_is_pairing_active) {
    // Nintendo parental-device pairing is not implemented or persisted by Encore.
    *out_is_pairing_active = false;
    LOG_DEBUG(Service_PCTL, "Parental-control pairing inactive");
    R_SUCCEED();
}

Result IParentalControlService::GetSynchronizationEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_PCTL, "called");
    *out_event = synchronization_event.GetHandle();
    R_SUCCEED();
}

Result IParentalControlService::StartPlayTimer() {
    play_timer_running = true;
    LOG_DEBUG(Service_PCTL, "Play timer started (elapsed-time accounting not modeled)");
    R_SUCCEED();
}

Result IParentalControlService::StopPlayTimer() {
    play_timer_running = false;
    LOG_DEBUG(Service_PCTL, "Play timer stopped");
    R_SUCCEED();
}

Result IParentalControlService::IsPlayTimerEnabled(Out<bool> out_is_play_timer_enabled) {
    *out_is_play_timer_enabled = play_timer_running;
    LOG_DEBUG(Service_PCTL, "called, enabled={}", *out_is_play_timer_enabled);
    R_SUCCEED();
}

Result IParentalControlService::GetPlayTimerRemainingTime(Out<s32> out_remaining_time) {
    // Encore tracks timer lifecycle but not Horizon's wall-clock accounting/configuration.
    *out_remaining_time = std::numeric_limits<s32>::max();
    LOG_DEBUG(Service_PCTL, "Returning unrestricted remaining time, running={}",
              play_timer_running);
    R_SUCCEED();
}

Result IParentalControlService::IsRestrictedByPlayTimer(Out<bool> out_is_restricted_by_play_timer) {
    // No elapsed-time budget is modeled, therefore Encore must not fabricate a timer restriction.
    *out_is_restricted_by_play_timer = false;
    LOG_DEBUG(Service_PCTL, "called, restricted={}", *out_is_restricted_by_play_timer);
    R_SUCCEED();
}

Result IParentalControlService::GetPlayTimerSettingsOld(
    Out<PlayTimerSettingsOld> out_play_timer_settings) {
    LOG_WARNING(Service_PCTL, "(STUBBED) called");
    *out_play_timer_settings = {};
    R_SUCCEED();
}

Result IParentalControlService::GetPlayTimerSettings(Out<PlayTimerSettings> out_play_timer_settings) {
    *out_play_timer_settings = raw_play_timer_settings;
    LOG_DEBUG(Service_PCTL, "Returning locally stored HOS18+ play-timer settings");
    R_SUCCEED();
}

Result IParentalControlService::SetPlayTimerSettings(PlayTimerSettings play_timer_settings) {
    raw_play_timer_settings = play_timer_settings;
    LOG_DEBUG(Service_PCTL, "Stored local HOS18+ play-timer settings");
    R_SUCCEED();
}

Result IParentalControlService::GetPlayTimerEventToRequestSuspension(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_PCTL, "called");
    *out_event = request_suspension_event.GetHandle();
    R_SUCCEED();
}

Result IParentalControlService::IsPlayTimerAlarmDisabled(Out<bool> out_play_timer_alarm_disabled) {
    *out_play_timer_alarm_disabled = false;
    LOG_INFO(Service_PCTL, "called, is_play_timer_alarm_disabled={}",
             *out_play_timer_alarm_disabled);
    R_SUCCEED();
}

Result IParentalControlService::GetPlayTimerRemainingTimeDisplayInfo(Out<PlayTimerRemainingTimeDisplayInfo> out_display_info) {
    LOG_DEBUG(Service_PCTL, "called");
    *out_display_info = {.state = PlayTimerDisplayState::NotConfigured};
    R_SUCCEED();
}

Result IParentalControlService::Unknown1460(u8 in_unk, Out<PlayTimerRemainingTimeDisplayInfo> out_display_info) {
    LOG_DEBUG(Service_PCTL, "called, in_unk={}", in_unk);
    *out_display_info = {.state = PlayTimerDisplayState::NotConfigured};
    R_SUCCEED();
}

Result IParentalControlService::GetUnlinkedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_PCTL, "called");
    *out_event = unlinked_event.GetHandle();
    R_SUCCEED();
}

Result IParentalControlService::ClearUnlinkedEvent() {
    unlinked_event.Clear(system.Kernel());
    LOG_DEBUG(Service_PCTL, "Unlinked event cleared");
    R_SUCCEED();
}

void IParentalControlService::NotifyWrongPinCodeInputManyTimes(HLERequestContext& ctx) {
    LOG_DEBUG(Service_PCTL, "Wrong-PIN repetition notification acknowledged");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IParentalControlService::CancelNetworkRequest(HLERequestContext& ctx) {
    LOG_DEBUG(Service_PCTL, "No parental-control network request is active; cancel acknowledged");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

Result IParentalControlService::DisableAllFeatures() {
    settings.disabled = true;
    LOG_DEBUG(Service_PCTL, "All parental-control feature effects disabled");
    R_SUCCEED();
}

Result IParentalControlService::PostEnableAllFeatures() {
    settings.disabled = false;
    LOG_DEBUG(Service_PCTL, "All parental-control feature effects enabled");
    R_SUCCEED();
}

Result IParentalControlService::IsAllFeaturesDisabled(Out<bool> out_is_disabled) {
    *out_is_disabled = settings.disabled;
    LOG_DEBUG(Service_PCTL, "called, disabled={}", *out_is_disabled);
    R_SUCCEED();
}

Result IParentalControlService::DeletePairing() {
    unlinked_event.Signal(system.Kernel());
    LOG_DEBUG(Service_PCTL, "Local pairing state cleared");
    R_SUCCEED();
}

Result IParentalControlService::GetStereoVisionRestriction(
    Out<bool> out_stereo_vision_restriction) {
    LOG_DEBUG(Service_PCTL, "called");

    if (False(capability & Capability::StereoVision)) {
        LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
        *out_stereo_vision_restriction = false;
        R_THROW(PCTL::ResultNoCapability);
    }

    *out_stereo_vision_restriction = settings.is_stero_vision_restricted;
    R_SUCCEED();
}

Result IParentalControlService::SetStereoVisionRestriction(bool stereo_vision_restriction) {
    LOG_DEBUG(Service_PCTL, "called, can_use={}", stereo_vision_restriction);

    if (False(capability & Capability::StereoVision)) {
        LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
        R_THROW(PCTL::ResultNoCapability);
    }

    SetStereoVisionRestrictionImpl(stereo_vision_restriction);
    R_SUCCEED();
}

Result IParentalControlService::ResetConfirmedStereoVisionPermission() {
    LOG_DEBUG(Service_PCTL, "called");

    states.stereo_vision = false;

    R_SUCCEED();
}

} // namespace Service::PCTL
