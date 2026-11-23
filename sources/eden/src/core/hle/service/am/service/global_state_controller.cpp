// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/cradle_firmware_updater.h"
#include "core/hle/service/am/service/global_state_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IGlobalStateController::IGlobalStateController(Core::System& system_)
    : ServiceFramework{system_, "IGlobalStateController"},
      m_context{system_, "IGlobalStateController"}, m_hdcp_authentication_failed_event{m_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "RequestToEnterSleep"},
        {1, nullptr, "EnterSleep"},
        {2, D<&IGlobalStateController::StartSleepSequence>, "StartSleepSequence"},
        {3, D<&IGlobalStateController::StartShutdownSequence>, "StartShutdownSequence"},
        {4, D<&IGlobalStateController::StartRebootSequence>, "StartRebootSequence"},
        {9, D<&IGlobalStateController::IsAutoPowerDownRequested>, "IsAutoPowerDownRequested"},
        {10, D<&IGlobalStateController::LoadAndApplyIdlePolicySettings>, "LoadAndApplyIdlePolicySettings"},
        {11, D<&IGlobalStateController::NotifyCecSettingsChanged>, "NotifyCecSettingsChanged"},
        {12, D<&IGlobalStateController::SetDefaultHomeButtonLongPressTime>, "SetDefaultHomeButtonLongPressTime"},
        {13, D<&IGlobalStateController::UpdateDefaultDisplayResolution>, "UpdateDefaultDisplayResolution"},
        {14, D<&IGlobalStateController::ShouldSleepOnBoot>, "ShouldSleepOnBoot"},
        {15, D<&IGlobalStateController::GetHdcpAuthenticationFailedEvent>, "GetHdcpAuthenticationFailedEvent"},
        {30, D<&IGlobalStateController::OpenCradleFirmwareUpdater>, "OpenCradleFirmwareUpdater"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

Result IGlobalStateController::StartSleepSequence(bool sleep_requested) {
    // Horizon's sleep transition is guest power policy. Encore must not suspend the PS5 host;
    // titles only require the IPC transition to complete.
    LOG_DEBUG(Service_AM, "called, sleep_requested={} (guest sleep not forwarded to host)",
              sleep_requested);
    R_SUCCEED();
}

Result IGlobalStateController::IsAutoPowerDownRequested(
    Out<bool> out_is_auto_power_down_requested) {
    // Encore never turns the PS5 host power-down policy into a guest Horizon request.
    *out_is_auto_power_down_requested = false;
    LOG_DEBUG(Service_AM, "called, requested={}", *out_is_auto_power_down_requested);
    R_SUCCEED();
}

Result IGlobalStateController::NotifyCecSettingsChanged() {
    // HDMI-CEC remains owned by the PS5 system software; guest notifications have no host action.
    LOG_DEBUG(Service_AM, "called (CEC policy owned by host)");
    R_SUCCEED();
}

Result IGlobalStateController::SetDefaultHomeButtonLongPressTime(s64 time) {
    // The PS5 Home/PS-button timing is host policy and must not be changed by guest software.
    LOG_DEBUG(Service_AM, "called, time={} (host input policy unchanged)", time);
    R_SUCCEED();
}

Result IGlobalStateController::UpdateDefaultDisplayResolution() {
    // Encore derives its display resolution from the active host/frontend configuration rather
    // than Horizon's global-state service, so there is no mutable guest setting to refresh here.
    LOG_DEBUG(Service_AM, "called (display resolution owned by active host configuration)");
    R_SUCCEED();
}

Result IGlobalStateController::StartShutdownSequence() {
    LOG_INFO(Service_AM, "called");
    system.Exit();
    R_SUCCEED();
}

Result IGlobalStateController::StartRebootSequence() {
    LOG_INFO(Service_AM, "called");
    system.Exit();
    R_SUCCEED();
}

IGlobalStateController::~IGlobalStateController() = default;

Result IGlobalStateController::LoadAndApplyIdlePolicySettings() {
    // Horizon applies its own idle policy here. On PS5 the host power policy remains owned by
    // Prospero OS, and Encore exposes no guest idle:sys scheduler to update.
    LOG_DEBUG(Service_AM, "called (guest idle policy intentionally not applied to host)");
    R_SUCCEED();
}

Result IGlobalStateController::ShouldSleepOnBoot(Out<bool> out_should_sleep_on_boot) {
    LOG_INFO(Service_AM, "called");
    *out_should_sleep_on_boot = false;
    R_SUCCEED();
}

Result IGlobalStateController::GetHdcpAuthenticationFailedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_AM, "called");
    *out_event = m_hdcp_authentication_failed_event.GetHandle();
    R_SUCCEED();
}

Result IGlobalStateController::OpenCradleFirmwareUpdater(
    Out<SharedPointer<ICradleFirmwareUpdater>> out_cradle_firmware_updater) {
    LOG_INFO(Service_AM, "called");
    *out_cradle_firmware_updater = std::make_shared<ICradleFirmwareUpdater>(system);
    R_SUCCEED();
}

} // namespace Service::AM
