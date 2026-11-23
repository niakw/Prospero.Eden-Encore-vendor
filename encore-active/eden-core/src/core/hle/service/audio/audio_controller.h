// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>

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
    Result SetDefaultTarget(Set::AudioOutputModeTarget target, u64 fade_in_ns, u64 fade_out_ns);
    Result GetDefaultTarget(Out<Set::AudioOutputModeTarget> out_default_target);
    Result SetOutputTarget(Set::AudioOutputModeTarget target);
    Result SetInputTargetForceEnabled(bool enabled);
    Result GetAudioOutputTargetForPlayReport(Out<Set::AudioOutputModeTarget> out_target);
    Result SetSystemOutputMasterVolume(f32 volume);
    Result GetSystemOutputMasterVolume(Out<f32> out_volume);
    Result GetActiveOutputTarget(Out<Set::AudioOutputModeTarget> out_active_target);
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
    Result NotifyHeadphoneVolumeWarningDisplayedEvent();
    Result SetSpeakerAutoMuteEnabled(bool is_speaker_auto_mute_enabled);
    Result IsSpeakerAutoMuteEnabled(Out<bool> out_is_speaker_auto_mute_enabled);
    Result AcquireTargetNotification(OutCopyHandle<Kernel::KReadableEvent> out_notification_event);
    Result Unknown5000(Out<SharedPointer<IAudioController>> out_audio_controller);

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* notification_event;
    std::shared_ptr<Service::Set::ISystemSettingsServer> GetSetSys();

    std::once_flag m_set_sys_once;
    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
    std::array<s32, 6> m_target_volumes{{15, 15, 15, 15, 15, 15}};
    std::array<bool, 6> m_target_muted{{false, false, false, false, false, false}};
    Set::AudioOutputModeTarget m_active_target{Set::AudioOutputModeTarget::Speaker};
    Set::AudioOutputModeTarget m_default_target{Set::AudioOutputModeTarget::Speaker};
    u64 m_default_fade_in_ns{};
    u64 m_default_fade_out_ns{};
    bool m_input_target_force_enabled{};
    f32 m_system_output_master_volume{1.0f};
};

} // namespace Service::Audio
