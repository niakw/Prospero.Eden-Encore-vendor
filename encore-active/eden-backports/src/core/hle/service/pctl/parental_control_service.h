// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator
// Project// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/pctl/pctl_types.h"
#include "core/hle/service/service.h"

namespace Service::PCTL {

class IParentalControlService final : public ServiceFramework<IParentalControlService> {
public:
    explicit IParentalControlService(Core::System& system_, Capability capability_,
                                     u64 program_id_);
    ~IParentalControlService() override;

private:
    bool CheckFreeCommunicationPermissionImpl() const;
    bool ConfirmStereoVisionPermissionImpl() const;
    void SetStereoVisionRestrictionImpl(bool is_restricted);

    Result Initialize();
    Result CheckFreeCommunicationPermission();
    Result ConfirmLaunchApplicationPermission(InBuffer<BufferAttr_HipcPointer> restriction_bitset,
                                              u64 nacp_flag, u64 application_id);
    Result ConfirmResumeApplicationPermission(InBuffer<BufferAttr_HipcPointer> restriction_bitset,
                                              u64 nacp_flag, u64 application_id);
    Result ConfirmSnsPostPermission();
    Result ConfirmSystemSettingsPermission();
    Result IsRestrictionTemporaryUnlocked(Out<bool> out_is_temporary_unlocked);
    Result RevertRestrictionTemporaryUnlocked();
    Result EnterRestrictedSystemSettings();
    Result LeaveRestrictedSystemSettings();
    Result RevertRestrictedSystemSettingsEntered();
    Result IsRestrictedSystemSettingsEntered(Out<bool> out_is_restricted_system_settings_entered);
    Result ConfirmStereoVisionPermission();
    Result EndFreeCommunication();
    Result IsFreeCommunicationAvailable();
    Result IsRestrictionEnabled(Out<bool> out_restriction_enabled);
    Result GetSafetyLevel(Out<u32> out_safety_level);
    Result GetCurrentSettings(Out<RestrictionSettings> out_settings);
    Result SetCustomSafetyLevelSettings(const RestrictionSettings& settings);
    Result GetDefaultRatingOrganization(Out<u32> out_rating_organization);
    Result SetDefaultRatingOrganization(u32 rating_organization);
    Result GetFreeCommunicationApplicationListCount(Out<s32> out_count);
    Result GetFreeCommunicationApplicationList(
        Out<u32> out_count,
        OutArray<std::array<u8, 0x10>, BufferAttr_HipcMapAlias> out_list, u32 offset);
    Result ConfirmStereoVisionRestrictionConfigurable();
    Result IsStereoVisionPermitted(Out<bool> out_is_permitted);
    Result UnlockRestrictionTemporarily(InArray<char, BufferAttr_HipcPointer> pin_code_buffer);
    Result SetPinCode(InArray<char, BufferAttr_HipcPointer> pin_code_buffer);
    Result GetPinCodeLength(Out<s32> out_length);
    Result GetPinCodeChangedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetPinCode(Out<s32> out_length,
                      OutArray<char, BufferAttr_HipcPointer> out_pin_code);
    Result IsPairingActive(Out<bool> out_is_pairing_active);
    Result GetSynchronizationEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result StartPlayTimer();
    Result StopPlayTimer();
    Result IsPlayTimerEnabled(Out<bool> out_is_play_timer_enabled);
    Result GetPlayTimerRemainingTime(Out<s32> out_remaining_time);
    Result IsRestrictedByPlayTimer(Out<bool> out_is_restricted_by_play_timer);
    Result GetPlayTimerSettingsOld(Out<PlayTimerSettingsOld> out_play_timer_settings);
    Result GetPlayTimerEventToRequestSuspension(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result IsPlayTimerAlarmDisabled(Out<bool> out_play_timer_alarm_disabled);
    Result GetPlayTimerRemainingTimeDisplayInfo(Out<PlayTimerRemainingTimeDisplayInfo> out_display_info);
    Result Unknown1460(u8 in_unk, Out<PlayTimerRemainingTimeDisplayInfo> out_display_info);
    Result GetUnlinkedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result ClearUnlinkedEvent();
    Result GetStereoVisionRestriction(Out<bool> out_stereo_vision_restriction);
    Result SetStereoVisionRestriction(bool stereo_vision_restriction);
    Result ResetConfirmedStereoVisionPermission();
    Result GetPlayTimerSettings(Out<PlayTimerSettings> out_play_timer_settings);
    Result SetPlayTimerSettings(PlayTimerSettings out_play_timer_settings);

    struct States {
        u64 current_tid{};
        ApplicationInfo application_info{};
        u64 tid_from_event{};
        bool launch_time_valid{};
        bool is_suspended{};
        bool temporary_unlocked{};
        bool restricted_system_settings_entered{};
        bool free_communication{};
        bool stereo_vision{};
    };

    struct ParentalControlSettings {
        bool is_stero_vision_restricted{};
        bool is_free_communication_default_on{};
        bool disabled{};
    };

    States states{};
    ParentalControlSettings settings{};
    RestrictionSettings restriction_settings{};
    u32 default_rating_organization{};
    std::array<char, 8> pin_code{};
    Capability capability{};
    u64 program_id{};
    // TODO: this is raw
    PlayTimerSettings raw_play_timer_settings{};
    bool play_timer_running{};

    KernelHelpers::ServiceContext service_context;
    Event synchronization_event;
    Event unlinked_event;
    Event pin_code_changed_event;
    Event request_suspension_event;
};

} // namespace Service::PCTL
