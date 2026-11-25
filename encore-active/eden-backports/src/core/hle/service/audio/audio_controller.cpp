// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_core.h"
#include "core/hle/service/audio/audio_controller.h"
#include "core/hle/service/audio/audio_out_manager.h"
#include "core/core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"
#include "common/settings.h"
#include <algorithm>

namespace Service::Audio {

IAudioController::IAudioController(Core::System& system_)
    : ServiceFramework{system_, "audctl"}, service_context{system, "audctl"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioController::GetTargetVolume>, "GetTargetVolume"},
        {1, D<&IAudioController::SetTargetVolume>, "SetTargetVolume"},
        {2, D<&IAudioController::GetTargetVolumeMin>, "GetTargetVolumeMin"},
        {3, D<&IAudioController::GetTargetVolumeMax>, "GetTargetVolumeMax"},
        {4, D<&IAudioController::IsTargetMute>, "IsTargetMute"},
        {5, D<&IAudioController::SetTargetMute>, "SetTargetMute"},
        {6, nullptr, "IsTargetConnected"}, //20.0.0+
        {7, D<&IAudioController::SetDefaultTarget>, "SetDefaultTarget"},
        {8, D<&IAudioController::GetDefaultTarget>, "GetDefaultTarget"},
        {9, D<&IAudioController::GetAudioOutputMode>, "GetAudioOutputMode"},
        {10, D<&IAudioController::SetAudioOutputMode>, "SetAudioOutputMode"},
        {11, D<&IAudioController::SetForceMutePolicy>, "SetForceMutePolicy"},
        {12, D<&IAudioController::GetForceMutePolicy>, "GetForceMutePolicy"},
        {13, D<&IAudioController::GetOutputModeSetting>, "GetOutputModeSetting"},
        {14, D<&IAudioController::SetOutputModeSetting>, "SetOutputModeSetting"},
        {15, D<&IAudioController::SetOutputTarget>, "SetOutputTarget"},
        {16, D<&IAudioController::SetInputTargetForceEnabled>, "SetInputTargetForceEnabled"},
        {17, D<&IAudioController::SetHeadphoneOutputLevelMode>, "SetHeadphoneOutputLevelMode"},
        {18, D<&IAudioController::GetHeadphoneOutputLevelMode>, "GetHeadphoneOutputLevelMode"},
        {19, D<&IAudioController::SetForceMonauralOutputModeEnabled>, "SetForceMonauralOutputModeEnabled"},
        {20, D<&IAudioController::IsForceMonauralOutputModeEnabled>, "IsForceMonauralOutputModeEnabled"},
        {21, D<&IAudioController::GetAudioOutputTargetForPlayReport>, "GetAudioOutputTargetForPlayReport"},
        {22, D<&IAudioController::NotifyHeadphoneVolumeWarningDisplayedEvent>, "NotifyHeadphoneVolumeWarningDisplayedEvent"},
        {23, D<&IAudioController::SetSystemOutputMasterVolume>, "SetSystemOutputMasterVolume"},
        {24, D<&IAudioController::GetSystemOutputMasterVolume>, "GetSystemOutputMasterVolume"},
        {25, D<&IAudioController::GetAudioVolumeDataForPlayReport>, "GetAudioVolumeDataForPlayReport"},
        {26, nullptr, "UpdateHeadphoneSettings"},
        {27, &IAudioController::RejectUnavailableReportingOrDebugCommand, "SetVolumeMappingTableForDev"},
        {28, &IAudioController::RejectUnavailableReportingOrDebugCommand, "GetAudioOutputChannelCountForPlayReport"},
        {29, &IAudioController::RejectUnavailableReportingOrDebugCommand, "BindAudioOutputChannelCountUpdateEventForPlayReport"},
        {30, D<&IAudioController::SetSpeakerAutoMuteEnabled>, "SetSpeakerAutoMuteEnabled"},
        {31, D<&IAudioController::IsSpeakerAutoMuteEnabled>, "IsSpeakerAutoMuteEnabled"},
        {32, D<&IAudioController::GetActiveOutputTarget>, "GetActiveOutputTarget"},
        {33, nullptr, "GetTargetDeviceInfo"},
        {34, D<&IAudioController::AcquireTargetNotification>, "AcquireTargetNotification"},
        {35, &IAudioController::RejectUnavailableReportingOrDebugCommand, "SetHearingProtectionSafeguardTimerRemainingTimeForDebug"},
        {36, &IAudioController::RejectUnavailableReportingOrDebugCommand, "GetHearingProtectionSafeguardTimerRemainingTimeForDebug"},
        {37, D<&IAudioController::SetHearingProtectionSafeguardEnabled>, "SetHearingProtectionSafeguardEnabled"},
        {38, D<&IAudioController::IsHearingProtectionSafeguardEnabled>, "IsHearingProtectionSafeguardEnabled"},
        {39, &IAudioController::RejectUnavailableReportingOrDebugCommand, "IsHearingProtectionSafeguardMonitoringOutputForDebug"},
        {40, &IAudioController::RejectUnavailableReportingOrDebugCommand, "GetSystemInformationForDebug"},
        {41, D<&IAudioController::SetVolumeButtonLongPressTime>, "SetVolumeButtonLongPressTime"},
        {42, &IAudioController::RejectUnavailableReportingOrDebugCommand, "SetNativeVolumeForDebug"},
        {43, nullptr, "Unknown43"}, //21.0.0+
        {5000, D<&IAudioController::Unknown5000>, "Unknown5000"}, //19.0.0+
        {10000, &IAudioController::RejectUnavailableReportingOrDebugCommand, "NotifyAudioOutputTargetForPlayReport"},
        {10001, &IAudioController::RejectUnavailableReportingOrDebugCommand, "NotifyAudioOutputChannelCountForPlayReport"},
        {10002, &IAudioController::RejectUnavailableReportingOrDebugCommand, "NotifyUnsupportedUsbOutputDeviceAttachedForPlayReport"},
        {10100, &IAudioController::RejectUnavailableReportingOrDebugCommand, "GetAudioVolumeDataForPlayReport"},
        {10101, &IAudioController::RejectUnavailableReportingOrDebugCommand, "BindAudioVolumeUpdateEventForPlayReport"},
        {10102, &IAudioController::RejectUnavailableReportingOrDebugCommand, "BindAudioOutputTargetUpdateEventForPlayReport"},
        {10103, &IAudioController::RejectUnavailableReportingOrDebugCommand, "GetAudioOutputTargetForPlayReport"},
        {10104, &IAudioController::RejectUnavailableReportingOrDebugCommand, "GetAudioOutputChannelCountForPlayReport"},
        {10105, &IAudioController::RejectUnavailableReportingOrDebugCommand, "BindAudioOutputChannelCountUpdateEventForPlayReport"}, //14.0.0-19.0.1
        {10106, &IAudioController::RejectUnavailableReportingOrDebugCommand, "GetDefaultAudioOutputTargetForPlayReport"}, //14.0.0-19.0.1
        {10200, nullptr, "Unknown10200"}, //20.0.0+
        {50000, &IAudioController::RejectUnavailableReportingOrDebugCommand, "SetAnalogInputBoostGainForPrototyping"}, //15.0.0-18.1.0
        {50001, &IAudioController::RejectUnavailableReportingOrDebugCommand, "OverrideDefaultTargetForDebug"}, //19.0.0-19.0.1
        {50003, &IAudioController::RejectUnavailableReportingOrDebugCommand, "SetForceOverrideExternalDeviceNameForDebug"}, //19.0.0+
        {50004, &IAudioController::RejectUnavailableReportingOrDebugCommand, "ClearForceOverrideExternalDeviceNameForDebug"} //19.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);

    m_set_sys =
        system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys", true);
    notification_event = service_context.CreateEvent("IAudioController:NotificationEvent");

    // Probably shouldn't do this in constructor?
    try {
        const int ui_volume = Settings::values.volume.GetValue();
        const int mapped = static_cast<int>(std::lround((static_cast<double>(ui_volume) / 100.0) * 15.0));
        const auto active_idx = static_cast<size_t>(m_active_target);
        if (active_idx < m_target_volumes.size()) {
            m_target_volumes[active_idx] = std::clamp(mapped, 0, 15);
        }

        const bool muted = Settings::values.audio_muted.GetValue();
        for (auto& m : m_target_muted) {
            m = muted;
        }

        if (!muted && active_idx < m_target_volumes.size()) {
            const float vol_f = static_cast<float>(m_target_volumes[active_idx]) / 15.0f;
            try {
                auto& sink = system.AudioCore().GetOutputSink();
                sink.SetSystemVolume(vol_f);
                sink.SetDeviceVolume(vol_f);
            } catch (...) {
                LOG_WARNING(Audio, "Failed to apply initial sink volume from settings");
            }

            if (auto audout_mgr = system.ServiceManager().GetService<Service::Audio::IAudioOutManager>("audout:u")) {
                audout_mgr->SetAllAudioOutVolume(static_cast<float>(m_target_volumes[active_idx]) / 15.0f);
            }
        }
    } catch (...) {
        // Catch if something fails, since this is constructor
    }
}

IAudioController::~IAudioController() {
    service_context.CloseEvent(notification_event);
};

Result IAudioController::GetTargetVolumeMin(Out<s32> out_target_min_volume) {
    LOG_DEBUG(Audio, "called.");

    // This service function is currently hardcoded on the
    // actual console to this value (as of 8.0.0).
    *out_target_min_volume = 0;
    R_SUCCEED();
}

Result IAudioController::GetTargetVolumeMax(Out<s32> out_target_max_volume) {
    LOG_DEBUG(Audio, "called.");

    // This service function is currently hardcoded on the
    // actual console to this value (as of 8.0.0).
    *out_target_max_volume = 15;
    R_SUCCEED();
}

Result IAudioController::GetAudioOutputMode(Out<Set::AudioOutputMode> out_output_mode,
                                            Set::AudioOutputModeTarget target) {
    const auto result = m_set_sys->GetAudioOutputMode(out_output_mode, target);

    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, *out_output_mode);
    R_RETURN(result);
}

Result IAudioController::SetAudioOutputMode(Set::AudioOutputModeTarget target,
                                            Set::AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);

    R_RETURN(m_set_sys->SetAudioOutputMode(target, output_mode));
}

Result IAudioController::GetForceMutePolicy(Out<ForceMutePolicy> out_mute_policy) {
    *out_mute_policy = m_force_mute_policy;
    R_SUCCEED();
}

Result IAudioController::GetOutputModeSetting(Out<Set::AudioOutputMode> out_output_mode,
                                              Set::AudioOutputModeTarget target) {
    LOG_WARNING(Audio, "(STUBBED) called, target={}", target);

    *out_output_mode = Set::AudioOutputMode::ch_7_1;
    R_SUCCEED();
}

Result IAudioController::SetOutputModeSetting(Set::AudioOutputModeTarget target,
                                              Set::AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);
    R_SUCCEED();
}

Result IAudioController::SetHeadphoneOutputLevelMode(HeadphoneOutputLevelMode output_level_mode) {
    LOG_WARNING(Audio, "(STUBBED) called, output_level_mode={}", output_level_mode);
    R_SUCCEED();
}

Result IAudioController::GetHeadphoneOutputLevelMode(
    Out<HeadphoneOutputLevelMode> out_output_level_mode) {
    LOG_INFO(Audio, "called");

    *out_output_level_mode = HeadphoneOutputLevelMode::Normal;
    R_SUCCEED();
}

Result IAudioController::SetForceMonauralOutputModeEnabled(bool enabled) {
    m_force_monaural_output_mode_enabled = enabled;
    LOG_INFO(Audio, "force monaural output mode enabled={}", enabled);
    R_SUCCEED();
}

Result IAudioController::IsForceMonauralOutputModeEnabled(Out<bool> out_enabled) {
    *out_enabled = m_force_monaural_output_mode_enabled;
    R_SUCCEED();
}

Result IAudioController::NotifyHeadphoneVolumeWarningDisplayedEvent() {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioController::SetSpeakerAutoMuteEnabled(bool is_speaker_auto_mute_enabled) {
    LOG_INFO(Audio, "called, is_speaker_auto_mute_enabled={}", is_speaker_auto_mute_enabled);

    R_RETURN(m_set_sys->SetSpeakerAutoMuteFlag(is_speaker_auto_mute_enabled));
}

Result IAudioController::IsSpeakerAutoMuteEnabled(Out<bool> out_is_speaker_auto_mute_enabled) {
    const auto result = m_set_sys->GetSpeakerAutoMuteFlag(out_is_speaker_auto_mute_enabled);

    LOG_INFO(Audio, "called, is_speaker_auto_mute_enabled={}", *out_is_speaker_auto_mute_enabled);
    R_RETURN(result);
}

Result IAudioController::AcquireTargetNotification(
    OutCopyHandle<Kernel::KReadableEvent> out_notification_event) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    *out_notification_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}
Result IAudioController::Unknown5000(Out<SharedPointer<IAudioController>> out_audio_controller) {
    LOG_DEBUG(Audio, "Creating duplicate audio controller interface");

    // Return a new reference to this controller instance
    *out_audio_controller = std::static_pointer_cast<IAudioController>(shared_from_this());

    R_SUCCEED();
}

Result IAudioController::GetTargetVolume(Out<s32> out_target_volume, Set::AudioOutputModeTarget target) {
    LOG_DEBUG(Audio, "GetTargetVolume called, target={}", target);

    const auto idx = static_cast<size_t>(target);
    if (idx >= m_target_volumes.size()) {
        LOG_ERROR(Audio, "GetTargetVolume invalid target {}", idx);
        R_THROW(ResultInvalidArgument);
    }

    *out_target_volume = m_target_volumes[idx];
    R_SUCCEED();
}

Result IAudioController::SetTargetVolume(Set::AudioOutputModeTarget target, s32 target_volume) {
    LOG_INFO(Audio, "SetTargetVolume called, target={}, volume={}", target, target_volume);

    const auto idx = static_cast<size_t>(target);
    if (idx >= m_target_volumes.size()) {
        LOG_ERROR(Audio, "SetTargetVolume invalid target {}", idx);
        R_THROW(ResultInvalidArgument);
    }

    if (target_volume < 0) {
        target_volume = 0;
    } else if (target_volume > 15) {
        target_volume = 15;
    }

    m_target_volumes[idx] = target_volume;

    if (m_active_target == target && !m_target_muted[idx]) {
        const float vol = static_cast<float>(target_volume) / 15.0f;
        // try catch this, as we don't know how it handles it when no output is set.
        // we already have audio issues when no output is set, so catch.
        try {
            auto& sink = system.AudioCore().GetOutputSink();
            sink.SetSystemVolume(vol);
            sink.SetDeviceVolume(vol);
        } catch (...) {
            LOG_WARNING(Audio, "Failed to set sink system volume");
        }

        if (auto audout_mgr = system.ServiceManager().GetService<IAudioOutManager>("audout:u")) {
            audout_mgr->SetAllAudioOutVolume(vol);
        }
    }

    if (m_active_target == target) {
        const int ui_volume = static_cast<int>(std::lround((static_cast<double>(target_volume) / 15.0) * 100.0));
        Settings::values.volume.SetValue(static_cast<u8>(std::clamp(ui_volume, 0, 100)));
    }

    R_SUCCEED();
}

Result IAudioController::IsTargetMute(Out<bool> out_is_target_muted, Set::AudioOutputModeTarget target) {
    LOG_DEBUG(Audio, "called, target={}", target);

    const auto idx = static_cast<size_t>(target);
    if (idx >= m_target_muted.size()) {
        LOG_ERROR(Audio, "invalid target {}", idx);
        R_THROW(ResultInvalidArgument);
    }

    *out_is_target_muted = m_target_muted[idx];
    R_SUCCEED();
}

Result IAudioController::SetTargetMute(bool is_muted, Set::AudioOutputModeTarget target) {
    LOG_INFO(Audio, "called, target={}, muted={}", target, is_muted);

    const auto idx = static_cast<size_t>(target);
    if (idx >= m_target_muted.size()) {
        LOG_ERROR(Audio, "invalid target {}", idx);
        R_THROW(ResultInvalidArgument);
    }

    m_target_muted[idx] = is_muted;

    if (m_active_target == target) {
        try {
            auto& sink = system.AudioCore().GetOutputSink();
            if (is_muted) {
                sink.SetSystemVolume(0.0f);
                sink.SetDeviceVolume(0.0f);
            } else {
                const float vol = static_cast<float>(m_target_volumes[idx]) / 15.0f;
                sink.SetSystemVolume(vol);
                sink.SetDeviceVolume(vol);
            }
        } catch (...) {
            LOG_WARNING(Audio, "Failed to set sink system volume on mute change");
        }

        // Also update any open audout sessions via the audout:u service.
        if (auto audout_mgr = system.ServiceManager().GetService<IAudioOutManager>("audout:u")) {
            if (is_muted) {
                audout_mgr->SetAllAudioOutVolume(0.0f);
            } else {
                const float vol = static_cast<float>(m_target_volumes[idx]) / 15.0f;
                audout_mgr->SetAllAudioOutVolume(vol);
            }
        }
    }

    Settings::values.audio_muted.SetValue(is_muted);

    R_SUCCEED();
}

Result IAudioController::SetDefaultTarget(Set::AudioOutputModeTarget target, u64 fade_in_ns,
                                                u64 fade_out_ns) {
    const auto idx = static_cast<size_t>(target);
    R_UNLESS(idx < m_target_volumes.size(), ResultInvalidArgument);

    m_default_target = target;
    m_default_fade_in_ns = fade_in_ns;
    m_default_fade_out_ns = fade_out_ns;
    LOG_INFO(Audio, "default target={}, fade_in_ns={}, fade_out_ns={}", target, fade_in_ns,
             fade_out_ns);
    R_SUCCEED();
}

Result IAudioController::GetDefaultTarget(Out<Set::AudioOutputModeTarget> out_default_target) {
    *out_default_target = m_default_target;
    LOG_DEBUG(Audio, "default target={}", m_default_target);
    R_SUCCEED();
}

Result IAudioController::SetOutputTarget(Set::AudioOutputModeTarget target) {
    const auto idx = static_cast<size_t>(target);
    R_UNLESS(idx < m_target_volumes.size(), ResultInvalidArgument);

    const bool changed = m_active_target != target;
    m_active_target = target;

    const float volume =
        m_target_muted[idx] ? 0.0f : static_cast<float>(m_target_volumes[idx]) / 15.0f;
    try {
        auto& sink = system.AudioCore().GetOutputSink();
        sink.SetSystemVolume(volume);
        sink.SetDeviceVolume(volume);
    } catch (...) {
        LOG_WARNING(Audio, "Failed to apply output-target volume");
    }
    if (auto audout_mgr = system.ServiceManager().GetService<IAudioOutManager>("audout:u")) {
        audout_mgr->SetAllAudioOutVolume(volume);
    }

    const int ui_volume =
        static_cast<int>(std::lround((static_cast<double>(m_target_volumes[idx]) / 15.0) * 100.0));
    Settings::values.volume.SetValue(static_cast<u8>(std::clamp(ui_volume, 0, 100)));
    Settings::values.audio_muted.SetValue(m_target_muted[idx]);

    if (changed) {
        notification_event->Signal(system.Kernel());
    }
    LOG_INFO(Audio, "active output target={}", target);
    R_SUCCEED();
}

Result IAudioController::SetInputTargetForceEnabled(bool enabled) {
    m_input_target_force_enabled = enabled;
    LOG_DEBUG(Audio, "input target force enabled={}", enabled);
    R_SUCCEED();
}

Result IAudioController::GetAudioOutputTargetForPlayReport(
    Out<Set::AudioOutputModeTarget> out_target) {
    *out_target = m_active_target;
    R_SUCCEED();
}

Result IAudioController::SetSystemOutputMasterVolume(f32 volume) {
    m_system_output_master_volume = std::clamp(volume, 0.0f, 1.0f);
    LOG_DEBUG(Audio, "stored system output master volume={}", m_system_output_master_volume);
    R_SUCCEED();
}

Result IAudioController::GetSystemOutputMasterVolume(Out<f32> out_volume) {
    *out_volume = m_system_output_master_volume;
    R_SUCCEED();
}


Result IAudioController::SetForceMutePolicy(ForceMutePolicy force_mute_policy) {
    m_force_mute_policy = force_mute_policy;
    LOG_INFO(Audio, "force mute policy={}", static_cast<u32>(force_mute_policy));
    R_SUCCEED();
}

Result IAudioController::SetHearingProtectionSafeguardEnabled(bool is_enabled) {
    m_hearing_protection_safeguard_enabled = is_enabled;
    LOG_INFO(Audio, "hearing protection safeguard enabled={}", is_enabled);
    R_SUCCEED();
}

Result IAudioController::IsHearingProtectionSafeguardEnabled(Out<bool> out_is_enabled) {
    *out_is_enabled = m_hearing_protection_safeguard_enabled;
    R_SUCCEED();
}

Result IAudioController::SetVolumeButtonLongPressTime(u64 long_press_time) {
    m_volume_button_long_press_time = long_press_time;
    LOG_DEBUG(Audio, "volume-button long-press time={}", long_press_time);
    R_SUCCEED();
}

Result IAudioController::GetAudioVolumeDataForPlayReport(
    Out<AudioVolumeReportData> out_report_data) {
    const auto idx = static_cast<size_t>(m_active_target);
    if (idx >= m_target_volumes.size()) {
        R_THROW(ResultInvalidArgument);
    }
    out_report_data->volume = static_cast<u32>(m_target_volumes[idx]);
    R_SUCCEED();
}

void IAudioController::RejectUnavailableReportingOrDebugCommand(HLERequestContext& ctx) {
    LOG_WARNING(Audio, "Rejected unavailable audctl reporting/debug command {}", ctx.GetCommand());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result IAudioController::GetActiveOutputTarget(Out<Set::AudioOutputModeTarget> out_active_target) {
    LOG_DEBUG(Audio, "GetActiveOutputTarget called");
    *out_active_target = m_active_target;
    R_SUCCEED();
}
} // namespace Service::Audio
