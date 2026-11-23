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
        {11, nullptr, "SetForceMutePolicy"},
        {12, D<&IAudioController::GetForceMutePolicy>, "GetForceMutePolicy"},
        {13, D<&IAudioController::GetOutputModeSetting>, "GetOutputModeSetting"},
        {14, D<&IAudioController::SetOutputModeSetting>, "SetOutputModeSetting"},
        {15, D<&IAudioController::SetOutputTarget>, "SetOutputTarget"},
        {16, D<&IAudioController::SetInputTargetForceEnabled>, "SetInputTargetForceEnabled"},
        {17, D<&IAudioController::SetHeadphoneOutputLevelMode>, "SetHeadphoneOutputLevelMode"},
        {18, D<&IAudioController::GetHeadphoneOutputLevelMode>, "GetHeadphoneOutputLevelMode"},
        {19, nullptr, "AcquireAudioVolumeUpdateEventForPlayReport"},
        {20, nullptr, "AcquireAudioOutputDeviceUpdateEventForPlayReport"},
        {21, D<&IAudioController::GetAudioOutputTargetForPlayReport>, "GetAudioOutputTargetForPlayReport"},
        {22, D<&IAudioController::NotifyHeadphoneVolumeWarningDisplayedEvent>, "NotifyHeadphoneVolumeWarningDisplayedEvent"},
        {23, D<&IAudioController::SetSystemOutputMasterVolume>, "SetSystemOutputMasterVolume"},
        {24, D<&IAudioController::GetSystemOutputMasterVolume>, "GetSystemOutputMasterVolume"},
        {25, nullptr, "GetAudioVolumeDataForPlayReport"},
        {26, nullptr, "UpdateHeadphoneSettings"},
        {27, nullptr, "SetVolumeMappingTableForDev"},
        {28, nullptr, "GetAudioOutputChannelCountForPlayReport"},
        {29, nullptr, "BindAudioOutputChannelCountUpdateEventForPlayReport"},
        {30, D<&IAudioController::SetSpeakerAutoMuteEnabled>, "SetSpeakerAutoMuteEnabled"},
        {31, D<&IAudioController::IsSpeakerAutoMuteEnabled>, "IsSpeakerAutoMuteEnabled"},
        {32, D<&IAudioController::GetActiveOutputTarget>, "GetActiveOutputTarget"},
        {33, nullptr, "GetTargetDeviceInfo"},
        {34, D<&IAudioController::AcquireTargetNotification>, "AcquireTargetNotification"},
        {35, nullptr, "SetHearingProtectionSafeguardTimerRemainingTimeForDebug"},
        {36, nullptr, "GetHearingProtectionSafeguardTimerRemainingTimeForDebug"},
        {37, nullptr, "SetHearingProtectionSafeguardEnabled"},
        {38, nullptr, "IsHearingProtectionSafeguardEnabled"},
        {39, nullptr, "IsHearingProtectionSafeguardMonitoringOutputForDebug"},
        {40, nullptr, "GetSystemInformationForDebug"},
        {41, nullptr, "SetVolumeButtonLongPressTime"},
        {42, nullptr, "SetNativeVolumeForDebug"},
        {43, nullptr, "Unknown43"}, //21.0.0+
        {5000, D<&IAudioController::Unknown5000>, "Unknown5000"}, //19.0.0+
        {10000, nullptr, "NotifyAudioOutputTargetForPlayReport"},
        {10001, nullptr, "NotifyAudioOutputChannelCountForPlayReport"},
        {10002, nullptr, "NotifyUnsupportedUsbOutputDeviceAttachedForPlayReport"},
        {10100, nullptr, "GetAudioVolumeDataForPlayReport"},
        {10101, nullptr, "BindAudioVolumeUpdateEventForPlayReport"},
        {10102, nullptr, "BindAudioOutputTargetUpdateEventForPlayReport"},
        {10103, nullptr, "GetAudioOutputTargetForPlayReport"},
        {10104, nullptr, "GetAudioOutputChannelCountForPlayReport"},
        {10105, nullptr, "BindAudioOutputChannelCountUpdateEventForPlayReport"}, //14.0.0-19.0.1
        {10106, nullptr, "GetDefaultAudioOutputTargetForPlayReport"}, //14.0.0-19.0.1
        {10200, nullptr, "Unknown10200"}, //20.0.0+
        {50000, nullptr, "SetAnalogInputBoostGainForPrototyping"}, //15.0.0-18.1.0
        {50001, nullptr, "OverrideDefaultTargetForDebug"}, //19.0.0-19.0.1
        {50003, nullptr, "SetForceOverrideExternalDeviceNameForDebug"}, //19.0.0+
        {50004, nullptr, "ClearForceOverrideExternalDeviceNameForDebug"} //19.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);

    // Do not block a service-construction host thread waiting for set:sys.
    // Resolve it lazily only if a game actually calls an operation that needs it.
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

std::shared_ptr<Service::Set::ISystemSettingsServer> IAudioController::GetSetSys() {
    std::call_once(m_set_sys_once, [this] {
        m_set_sys = system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>(
            "set:sys", true);
    });
    return m_set_sys;
}

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
    const auto result = GetSetSys()->GetAudioOutputMode(out_output_mode, target);

    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, *out_output_mode);
    R_RETURN(result);
}

Result IAudioController::SetAudioOutputMode(Set::AudioOutputModeTarget target,
                                            Set::AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);

    R_RETURN(GetSetSys()->SetAudioOutputMode(target, output_mode));
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

Result IAudioController::GetForceMutePolicy(Out<ForceMutePolicy> out_mute_policy) {
    LOG_WARNING(Audio, "(STUBBED) called");

    // Removed on FW 13.2.1+
    *out_mute_policy = ForceMutePolicy::Disable;
    R_SUCCEED();
}

Result IAudioController::GetOutputModeSetting(Out<Set::AudioOutputMode> out_output_mode,
                                              Set::AudioOutputModeTarget target) {
    const auto idx = static_cast<size_t>(target);
    R_UNLESS(idx < m_output_mode_settings.size(), ResultInvalidArgument);

    *out_output_mode = m_output_mode_settings[idx];
    LOG_DEBUG(Audio, "output mode setting target={}, mode={}", target, *out_output_mode);
    R_SUCCEED();
}

Result IAudioController::SetOutputModeSetting(Set::AudioOutputModeTarget target,
                                              Set::AudioOutputMode output_mode) {
    const auto idx = static_cast<size_t>(target);
    R_UNLESS(idx < m_output_mode_settings.size(), ResultInvalidArgument);

    m_output_mode_settings[idx] = output_mode;
    LOG_INFO(Service_SET, "output mode setting target={}, mode={}", target, output_mode);
    R_SUCCEED();
}

Result IAudioController::SetHeadphoneOutputLevelMode(HeadphoneOutputLevelMode output_level_mode) {
    m_headphone_output_level_mode = output_level_mode;
    LOG_INFO(Audio, "headphone output level mode={}", output_level_mode);
    R_SUCCEED();
}

Result IAudioController::GetHeadphoneOutputLevelMode(
    Out<HeadphoneOutputLevelMode> out_output_level_mode) {
    *out_output_level_mode = m_headphone_output_level_mode;
    LOG_DEBUG(Audio, "headphone output level mode={}", *out_output_level_mode);
    R_SUCCEED();
}

Result IAudioController::NotifyHeadphoneVolumeWarningDisplayedEvent() {
    // Encore does not persist a separate headphone-warning latch. There is no
    // warning state to clear, so acknowledging the notification is idempotent.
    LOG_DEBUG(Service_Audio, "headphone volume warning acknowledged; no persistent latch");
    R_SUCCEED();
}

Result IAudioController::SetSpeakerAutoMuteEnabled(bool is_speaker_auto_mute_enabled) {
    LOG_INFO(Audio, "called, is_speaker_auto_mute_enabled={}", is_speaker_auto_mute_enabled);

    R_RETURN(GetSetSys()->SetSpeakerAutoMuteFlag(is_speaker_auto_mute_enabled));
}

Result IAudioController::IsSpeakerAutoMuteEnabled(Out<bool> out_is_speaker_auto_mute_enabled) {
    const auto result = GetSetSys()->GetSpeakerAutoMuteFlag(out_is_speaker_auto_mute_enabled);

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

Result IAudioController::GetActiveOutputTarget(Out<Set::AudioOutputModeTarget> out_active_target) {
    LOG_DEBUG(Audio, "GetActiveOutputTarget called");
    *out_active_target = m_active_target;
    R_SUCCEED();
}
} // namespace Service::Audio
