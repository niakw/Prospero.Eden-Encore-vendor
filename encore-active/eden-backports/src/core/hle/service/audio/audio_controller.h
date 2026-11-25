// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/settings_types.h"

namespace Core {
class System;
}

namespace Service::Set {
class ISystemSettingsServer;
}

namespace Service::Audio {

struct AudioVolumeReportData {
    u32 volume{};
};
static_assert(sizeof(AudioVolumeReportData) == sizeof(u32));

class IAudioController final : public ServiceFramework<IAudioController> {
public:
    explicit IAudioController(Core::System& system_);
    ~IAudioController() override;

private:
    enum class ForceMutePolicy {
        Disable,
        SpeakerMuteOnHeadphoneUnplugged,
    };

    enum class HeadphoneOutputLevelMode {
        Normal,
        HighPower,
    };

    Result GetTargetVolumeMin(Out<s32> out_target_min_volume);
    Result GetTargetVolumeMax(Out<s32> out_target_max_volume);
    Result GetTargetVolume(Out<s32> out_target_volume, Set::AudioOutputModeTarget target);
    Result SetTargetVolume(Set::AudioOutputModeTarget target, s32 target_volume);
    Result IsTargetMute(Out<bool> out_is_target_muted, Set::AudioOutputModeTarget target);
    Result SetTargetMute(bool is_muted, Set::AudioOutputModeTarget target);
    Result GetActiveOutputTarget(Out<Set::AudioOutputModeTarget> out_active_target);
    Result GetAudioVolumeDataForPlayReport(Out<AudioVolumeReportData> out_report_data);
    Result GetAudioOutputMode(Out<Set::AudioOutputMode> out_output_mode,
                              Set::AudioOutputModeTarget target);
    Result SetAudioOutputMode(Set::AudioOutputModeTarget target, Set::AudioOutputMode output_mode);
    Result GetForceMutePolicy(Out<ForceMutePolicy> out_mute_policy);
    Result GetOutputModeSetting(Out<Set::AudioOutputMode> out_output_mode,
                                Set::AudioOutputModeTarget target);
    Result SetOutputModeSetting(Set::AudioOutputModeTarget target,
                                Set::AudioOutputMode output_mode);
    Result SetHeadphoneOutputLevelMode(HeadphoneOutputLevelMode output_level_mode);
    Result GetHeadphoneOutputLevelMode(Out<HeadphoneOutputLevelMode> out_output_level_mode);
    Result SetForceMonauralOutputModeEnabled(bool enabled);
    Result IsForceMonauralOutputModeEnabled(Out<bool> out_enabled);
    Result NotifyHeadphoneVolumeWarningDisplayedEvent();
    Result SetSpeakerAutoMuteEnabled(bool is_speaker_auto_mute_enabled);
    Result IsSpeakerAutoMuteEnabled(Out<bool> out_is_speaker_auto_mute_enabled);
    Result AcquireTargetNotification(OutCopyHandle<Kernel::KReadableEvent> out_notification_event);
    void GetTargetDeviceInfo(HLERequestContext& ctx);
    void RejectUnknownHOS23AudioControl(HLERequestContext& ctx);
    void RejectLegacyAudioControl(HLERequestContext& ctx);
    Result Unknown5000(Out<SharedPointer<IAudioController>> out_audio_controller);
    Result SetDefaultTarget(Set::AudioOutputModeTarget target, u64 fade_in_ns, u64 fade_out_ns);
    Result GetDefaultTarget(Out<Set::AudioOutputModeTarget> out_default_target);
    Result SetOutputTarget(Set::AudioOutputModeTarget target);
    Result SetInputTargetForceEnabled(bool enabled);
    Result GetAudioOutputTargetForPlayReport(Out<Set::AudioOutputModeTarget> out_target);
    Result SetSystemOutputMasterVolume(f32 volume);
    Result GetSystemOutputMasterVolume(Out<f32> out_volume);
    Result SetForceMutePolicy(ForceMutePolicy force_mute_policy);
    Result SetHearingProtectionSafeguardEnabled(bool is_enabled);
    Result IsHearingProtectionSafeguardEnabled(Out<bool> out_is_enabled);
    Result SetVolumeButtonLongPressTime(u64 long_press_time);
    void RejectUnavailableReportingOrDebugCommand(HLERequestContext& ctx);

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* notification_event;
    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
    std::array<s32, 6> m_target_volumes{{15, 15, 15, 15, 15, 15}};
    std::array<bool, 6> m_target_muted{{false, false, false, false, false, false}};
    Set::AudioOutputModeTarget m_active_target{Set::AudioOutputModeTarget::Speaker};
    Set::AudioOutputModeTarget m_default_target{Set::AudioOutputModeTarget::Speaker};
    u64 m_default_fade_in_ns{};
    u64 m_default_fade_out_ns{};
    bool m_input_target_force_enabled{};
    bool m_force_monaural_output_mode_enabled{};
    HeadphoneOutputLevelMode m_headphone_output_level_mode{HeadphoneOutputLevelMode::Normal};
    f32 m_system_output_master_volume{1.0f};
    ForceMutePolicy m_force_mute_policy{ForceMutePolicy::Disable};
    bool m_hearing_protection_safeguard_enabled{};
    u64 m_volume_button_long_press_time{};
};

} // namespace Service::Audio
