// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging.h"
#include "core/hle/service/btm/btm_system_core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"

namespace Service::BTM {

IBtmSystemCore::IBtmSystemCore(Core::System& system_)
    : ServiceFramework{system_, "IBtmSystemCore"}, service_context{system_, "IBtmSystemCore"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IBtmSystemCore::StartGamepadPairing>, "StartGamepadPairing"},
        {1, C<&IBtmSystemCore::CancelGamepadPairing>, "CancelGamepadPairing"},
        {2, C<&IBtmSystemCore::ClearGamepadPairingDatabase>, "ClearGamepadPairingDatabase"},
        {3, C<&IBtmSystemCore::GetPairedGamepadCount>, "GetPairedGamepadCount"},
        {4, C<&IBtmSystemCore::EnableRadio>, "EnableRadio"},
        {5, C<&IBtmSystemCore::DisableRadio>, "DisableRadio"},
        {6, C<&IBtmSystemCore::IsRadioEnabled>, "IsRadioEnabled"},
        {7, C<&IBtmSystemCore::AcquireRadioEvent>, "AcquireRadioEvent"},
        {8, C<&IBtmSystemCore::AcquireGamepadPairingEvent>, "AcquireGamepadPairingEvent"},
        {9, C<&IBtmSystemCore::IsGamepadPairingStarted>, "IsGamepadPairingStarted"},
        {10, C<&IBtmSystemCore::StartAudioDeviceDiscovery>, "StartAudioDeviceDiscovery"},
        {11, C<&IBtmSystemCore::StopAudioDeviceDiscovery>, "StopAudioDeviceDiscovery"},
        {12, C<&IBtmSystemCore::IsDiscoveringAudioDevice>, "IsDiscoveryingAudioDevice"},
        {13, C<&IBtmSystemCore::GetDiscoveredAudioDevice>, "GetDiscoveredAudioDevice"},
        {14, C<&IBtmSystemCore::AcquireAudioDeviceConnectionEvent>, "AcquireAudioDeviceConnectionEvent"},
        {15, nullptr, "ConnectAudioDevice"},
        {16, C<&IBtmSystemCore::IsConnectingAudioDevice>, "IsConnectingAudioDevice"},
        {17, C<&IBtmSystemCore::GetConnectedAudioDevices>, "GetConnectedAudioDevices"},
        {18, nullptr, "DisconnectAudioDevice"},
        {19, C<&IBtmSystemCore::AcquirePairedAudioDeviceInfoChangedEvent>, "AcquirePairedAudioDeviceInfoChangedEvent"},
        {20, C<&IBtmSystemCore::GetPairedAudioDevices>, "GetPairedAudioDevices"},
        {21, nullptr, "RemoveAudioDevicePairing"},
        {22, C<&IBtmSystemCore::RequestAudioDeviceConnectionRejection>, "RequestAudioDeviceConnectionRejection"},
        {23, C<&IBtmSystemCore::CancelAudioDeviceConnectionRejection>, "CancelAudioDeviceConnectionRejection"}
    };
    // clang-format on

    RegisterHandlers(functions);
    radio_event = service_context.CreateEvent("IBtmSystemCore::RadioEvent");
    gamepad_pairing_event = service_context.CreateEvent("IBtmSystemCore::GamepadPairingEvent");
    audio_device_connection_event =
        service_context.CreateEvent("IBtmSystemCore::AudioDeviceConnectionEvent");

    paired_audio_info_event = service_context.CreateEvent("IBtmSystemCore::PairedAudioInfoEvent");

    m_set_sys =
        system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys", true);
}

IBtmSystemCore::~IBtmSystemCore() {
    service_context.CloseEvent(radio_event);
    service_context.CloseEvent(gamepad_pairing_event);
    service_context.CloseEvent(audio_device_connection_event);
    service_context.CloseEvent(paired_audio_info_event);
}

Result IBtmSystemCore::StartGamepadPairing() {
    LOG_DEBUG(Service_BTM, "called; tracking guest pairing lifecycle without a PS5 Bluetooth backend");

    gamepad_pairing_started = true;
    R_RETURN(gamepad_pairing_event->Signal(system.Kernel()));
}

Result IBtmSystemCore::CancelGamepadPairing() {
    LOG_DEBUG(Service_BTM, "called");

    gamepad_pairing_started = false;
    R_RETURN(gamepad_pairing_event->Signal(system.Kernel()));
}

Result IBtmSystemCore::ClearGamepadPairingDatabase() {
    LOG_DEBUG(Service_BTM, "called; no persistent Switch gamepad pairing database is modeled");
    R_SUCCEED();
}

Result IBtmSystemCore::GetPairedGamepadCount(Out<u8> out_count) {
    LOG_DEBUG(Service_BTM, "called; modeled Switch pairing database is empty");

    *out_count = 0;
    R_SUCCEED();
}

Result IBtmSystemCore::AcquireGamepadPairingEvent(
    Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_BTM, "Returning live gamepad pairing state event");

    *out_is_valid = true;
    *out_event = &gamepad_pairing_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmSystemCore::IsGamepadPairingStarted(Out<bool> out_is_started) {
    LOG_DEBUG(Service_BTM, "called");

    *out_is_started = gamepad_pairing_started;
    R_SUCCEED();
}

Result IBtmSystemCore::EnableRadio() {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->SetBluetoothEnableFlag(true));
}
Result IBtmSystemCore::DisableRadio() {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->SetBluetoothEnableFlag(false));
}

Result IBtmSystemCore::IsRadioEnabled(Out<bool> out_is_enabled) {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->GetBluetoothEnableFlag(out_is_enabled));
}

Result IBtmSystemCore::AcquireRadioEvent(Out<bool> out_is_valid,
                                         OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_BTM, "Returning live Bluetooth radio event");

    *out_is_valid = true;
    *out_event = &radio_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmSystemCore::StartAudioDeviceDiscovery() {
    // A guest discovery session can be active, but the host supplies no Switch audio devices.
    audio_discovery_active = true;
    R_SUCCEED();
}

Result IBtmSystemCore::StopAudioDeviceDiscovery() {
    audio_discovery_active = false;
    R_SUCCEED();
}

Result IBtmSystemCore::IsDiscoveringAudioDevice(Out<bool> out_active) {
    *out_active = audio_discovery_active;
    R_SUCCEED();
}

Result IBtmSystemCore::IsConnectingAudioDevice(Out<bool> out_active) {
    *out_active = false;
    R_SUCCEED();
}

Result IBtmSystemCore::AcquirePairedAudioDeviceInfoChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    *out_event = &paired_audio_info_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmSystemCore::GetDiscoveredAudioDevice(Out<s32> out_total,
    OutBuffer<BufferAttr_HipcPointer>) {
    // The array capacity belongs to the IPC buffer descriptor, not an inline s32.
    *out_total = 0;
    R_SUCCEED();
}

Result IBtmSystemCore::AcquireAudioDeviceConnectionEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_event = &audio_device_connection_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmSystemCore::GetConnectedAudioDevices(
    Out<s32> out_count, OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_count = 0;
    R_SUCCEED();
}

Result IBtmSystemCore::GetPairedAudioDevices(
    Out<s32> out_count, OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_count = 0;
    R_SUCCEED();
}

Result IBtmSystemCore::RequestAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_BTM, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IBtmSystemCore::CancelAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_BTM, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

} // namespace Service::BTM
