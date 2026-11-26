// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <limits>
#include <string_view>

#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/common_state_getter.h"
#include "core/hle/service/am/service/lock_accessor.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/apm/apm_interface.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::AM {

ICommonStateGetter::ICommonStateGetter(Core::System& system_, std::shared_ptr<Applet> applet)
    : ServiceFramework{system_, "ICommonStateGetter"}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ICommonStateGetter::GetEventHandle>, "GetEventHandle"},
        {1, D<&ICommonStateGetter::ReceiveMessage>, "ReceiveMessage"},
        {2, D<&ICommonStateGetter::RejectUnsupportedLegacyCommand>, "GetThisAppletKind"},
        {3, D<&ICommonStateGetter::RejectUnsupportedLegacyCommand>, "AllowToEnterSleep"},
        {4, D<&ICommonStateGetter::RejectUnsupportedLegacyCommand>, "DisallowToEnterSleep"},
        {5, D<&ICommonStateGetter::GetOperationMode>, "GetOperationMode"},
        {6, D<&ICommonStateGetter::GetPerformanceMode>, "GetPerformanceMode"},
        {7, D<&ICommonStateGetter::GetCradleStatus>, "GetCradleStatus"},
        {8, D<&ICommonStateGetter::GetBootMode>, "GetBootMode"},
        {9, D<&ICommonStateGetter::GetCurrentFocusState>, "GetCurrentFocusState"},
        {10, D<&ICommonStateGetter::RequestToAcquireSleepLock>, "RequestToAcquireSleepLock"},
        {11, D<&ICommonStateGetter::ReleaseSleepLock>, "ReleaseSleepLock"},
        {12, D<&ICommonStateGetter::ReleaseSleepLockTransiently>, "ReleaseSleepLockTransiently"},
        {13, D<&ICommonStateGetter::GetAcquiredSleepLockEvent>, "GetAcquiredSleepLockEvent"},
        {14, D<&ICommonStateGetter::GetWakeupCount>, "GetWakeupCount"}, //11.0.0+
        {15, D<&ICommonStateGetter::Unknown15>, "Unknown15"}, //19.0.0+
        {20, D<&ICommonStateGetter::PushToGeneralChannel>, "PushToGeneralChannel"},
        {30, D<&ICommonStateGetter::GetHomeButtonReaderLockAccessor>, "GetHomeButtonReaderLockAccessor"},
        {31, D<&ICommonStateGetter::GetReaderLockAccessorEx>, "GetReaderLockAccessorEx"}, //2.0.0+
        {32, D<&ICommonStateGetter::GetWriterLockAccessorEx>, "GetWriterLockAccessorEx"}, //7.0.0+
        {40, D<&ICommonStateGetter::GetCradleFwVersion>, "GetCradleFwVersion"}, //2.0.0+
        {50, D<&ICommonStateGetter::IsVrModeEnabled>, "IsVrModeEnabled"}, //3.0.0+
        {51, D<&ICommonStateGetter::SetVrModeEnabled>, "SetVrModeEnabled"}, //3.0.0+
        {52, D<&ICommonStateGetter::SetLcdBacklighOffEnabled>, "SetLcdBacklighOffEnabled"}, //4.0.0+
        {53, D<&ICommonStateGetter::BeginVrModeEx>, "BeginVrModeEx"}, //7.0.0+
        {54, D<&ICommonStateGetter::EndVrModeEx>, "EndVrModeEx"}, //7.0.0+
        {55, D<&ICommonStateGetter::IsInControllerFirmwareUpdateSection>, "IsInControllerFirmwareUpdateSection"}, //3.0.0+
        {59, &ICommonStateGetter::RejectUnavailableFeature, "SetVrPositionForDebug"}, //1.0.0+
        {60, D<&ICommonStateGetter::GetDefaultDisplayResolution>, "GetDefaultDisplayResolution"},
        {61, D<&ICommonStateGetter::GetDefaultDisplayResolutionChangeEvent>, "GetDefaultDisplayResolutionChangeEvent"},
        {62, D<&ICommonStateGetter::GetHdcpAuthenticationState>, "GetHdcpAuthenticationState"},
        {63, D<&ICommonStateGetter::GetHdcpAuthenticationStateChangeEvent>, "GetHdcpAuthenticationStateChangeEvent"},
        {64, D<&ICommonStateGetter::SetTvPowerStateMatchingMode>, "SetTvPowerStateMatchingMode"},
        {65, D<&ICommonStateGetter::GetApplicationIdByContentActionName>, "GetApplicationIdByContentActionName"},
        {66, &ICommonStateGetter::SetCpuBoostMode, "SetCpuBoostMode"},
        {67, D<&ICommonStateGetter::CancelCpuBoostMode>, "CancelCpuBoostMode"},
        {68, D<&ICommonStateGetter::GetBuiltInDisplayType>, "GetBuiltInDisplayType"},
        {80, D<&ICommonStateGetter::PerformSystemButtonPressingIfInFocus>, "PerformSystemButtonPressingIfInFocus"},
        {90, D<&ICommonStateGetter::SetPerformanceConfigurationChangedNotification>, "SetPerformanceConfigurationChangedNotification"},
        {91, D<&ICommonStateGetter::GetCurrentPerformanceConfiguration>, "GetCurrentPerformanceConfiguration"},
        {100, D<&ICommonStateGetter::SetHandlingHomeButtonShortPressedEnabled>, "SetHandlingHomeButtonShortPressedEnabled"},
        {110, &ICommonStateGetter::RejectUnavailableFeature, "OpenMyGpuErrorHandler"},
        {120, D<&ICommonStateGetter::GetAppletLaunchedHistory>, "GetAppletLaunchedHistory"}, //13.0.0+
        {130, D<&ICommonStateGetter::EnableStartupLogoDisappearedMessage>, "EnableStartupLogoDisappearedMessage"}, //21.0.0+
        {200, D<&ICommonStateGetter::GetOperationModeSystemInfo>, "GetOperationModeSystemInfo"},
        {300, D<&ICommonStateGetter::GetSettingsPlatformRegion>, "GetSettingsPlatformRegion"},
        {400, &ICommonStateGetter::RejectUnavailableFeature, "ActivateMigrationService"},
        {401, &ICommonStateGetter::RejectUnavailableFeature, "DeactivateMigrationService"},
        {500, D<&ICommonStateGetter::DisableSleepTillShutdown>, "DisableSleepTillShutdown"},
        {501, D<&ICommonStateGetter::SuppressDisablingSleepTemporarily>, "SuppressDisablingSleepTemporarily"},
        {502, D<&ICommonStateGetter::IsSleepEnabled>, "IsSleepEnabled"},
        {503, D<&ICommonStateGetter::IsDisablingSleepSuppressed>, "IsDisablingSleepSuppressed"},
        {600, &ICommonStateGetter::RejectUnavailableFeature, "SetHidInputMagnificationForApplication"}, //20.0.0+
        {610, D<&ICommonStateGetter::Unknown610>, "Unknown610"}, //21.0.0+
        {611, D<&ICommonStateGetter::Unknown611>, "Unknown611"}, //22.0.0+
        {900, D<&ICommonStateGetter::SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled>, "SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled"}, //11.0.0+
        {910, &ICommonStateGetter::RejectUnavailableFeature, "GetLaunchRequiredTick"}, //17.0.0+
        {1000, D<&ICommonStateGetter::BeginVrMode3d>, "BeginVrMode3d"}, //19.0.0+
        {1001, D<&ICommonStateGetter::EndVrMode3d>, "EndVrMode3d"}, //19.0.0+
        {1002, D<&ICommonStateGetter::IsVrModeEnabled3d>, "IsVrModeEnabled3d"}, //19.0.0+
        {1003, D<&ICommonStateGetter::GetVrLaboGoggleViewport>, "GetVrLaboGoggleViewport"}, //21.0.0+
        {1004, D<&ICommonStateGetter::GetPanelPhysicalSizeForSpecificTitle>, "GetPanelPhysicalSizeForSpecificTitle"}, //21.0.0+
        {1005, D<&ICommonStateGetter::GetPanelResolutionForSpecificTitle>, "GetPanelResolutionForSpecificTitle"}, //21.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);
}

ICommonStateGetter::~ICommonStateGetter() = default;

void ICommonStateGetter::RejectUnavailableFeature(HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "AM feature unavailable in Encore runtime");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result ICommonStateGetter::Unknown15(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called, HOS19+ event semantics not yet identified");
    *out_event = m_applet->unknown_event.GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::GetApplicationIdByContentActionName(
    Out<u64> out_application_id, InBuffer<BufferAttr_HipcMapAlias> name_buffer) {
    constexpr u64 ContentActionTableId = 0x0100000000000827ull;

    R_UNLESS(m_applet->applet_id == AppletId::Starter, ResultUnknown);

    const std::string action_name = Common::StringFromBuffer(name_buffer);
    R_UNLESS(!action_name.empty() && action_name.size() <= 0x100, ResultUnknown);
    R_UNLESS(action_name != "." && action_name != ".." &&
                 action_name.find('/') == std::string::npos &&
                 action_name.find('\\') == std::string::npos,
             ResultUnknown);

    auto* system_nand = system.GetFileSystemController().GetSystemNANDContents();
    R_UNLESS(system_nand != nullptr, ResultUnknown);

    auto nca = system_nand->GetEntry(ContentActionTableId, FileSys::ContentRecordType::Data);
    R_UNLESS(nca != nullptr, ResultUnknown);

    auto romfs = FileSys::ExtractRomFS(nca->GetRomFS());
    R_UNLESS(romfs != nullptr, ResultUnknown);

    const auto application_id_file =
        romfs->GetFileRelative("table/" + action_name + "/ApplicationId");
    R_UNLESS(application_id_file != nullptr, ResultUnknown);

    const auto data = application_id_file->ReadAllBytes();
    R_UNLESS(!data.empty(), ResultUnknown);

    std::string_view value{reinterpret_cast<const char*>(data.data()), data.size()};
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t' || value.front() == '\r' ||
            value.front() == '\n' || value.front() == '\0')) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ||
            value.back() == '\n' || value.back() == '\0')) {
        value.remove_suffix(1);
    }
    if (value.size() >= 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        value.remove_prefix(2);
    }
    R_UNLESS(!value.empty() && value.size() <= 16, ResultUnknown);

    u64 application_id = 0;
    for (const char c : value) {
        u64 digit{};
        if (c >= '0' && c <= '9') {
            digit = static_cast<u64>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<u64>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<u64>(c - 'A' + 10);
        } else {
            R_THROW(ResultUnknown);
        }
        R_UNLESS(application_id <= (std::numeric_limits<u64>::max() - digit) / 16, ResultUnknown);
        application_id = application_id * 16 + digit;
    }

    R_UNLESS(application_id != 0, ResultUnknown);
    *out_application_id = application_id;
    LOG_DEBUG(Service_AM, "ContentActionName {} -> application_id={:016X}", action_name,
              application_id);
    R_SUCCEED();
}

Result ICommonStateGetter::GetEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->lifecycle_manager.GetSystemEvent().GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::ReceiveMessage(Out<AppletMessage> out_applet_message) {
    LOG_DEBUG(Service_AM, "called");

    if (!m_applet->lifecycle_manager.PopMessage(system.Kernel(), out_applet_message)) {
        LOG_ERROR(Service_AM, "Tried to pop message but none was available!");
        R_THROW(AM::ResultNoMessages);
    }

    LOG_DEBUG(Service_AM, "called, returning message={} to applet_id={}",
             static_cast<u32>(*out_applet_message), static_cast<u32>(m_applet->applet_id));

    R_SUCCEED();
}

Result ICommonStateGetter::GetWakeupCount(Out<u64> out_wakeup_count) {
    // Encore does not currently model a guest sleep/resume cycle. Report a stable
    // zero count rather than exposing PS5 host suspend state to Horizon.
    *out_wakeup_count = 0;
    R_SUCCEED();
}

Result ICommonStateGetter::GetCurrentFocusState(Out<FocusState> out_focus_state) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    *out_focus_state = m_applet->lifecycle_manager.GetAndClearFocusState();

    R_SUCCEED();
}

Result ICommonStateGetter::RequestToAcquireSleepLock() {
    LOG_DEBUG(Service_AM, "called; sleep lock is acquired immediately");

    // Encore has no separate host sleep arbiter; acquisition is immediate.
    m_applet->sleep_lock_event.Signal(system.Kernel());
    R_SUCCEED();
}

Result ICommonStateGetter::ReleaseSleepLock() {
    LOG_DEBUG(Service_AM, "called");

    m_applet->sleep_lock_event.Clear(system.Kernel());
    R_SUCCEED();
}

Result ICommonStateGetter::ReleaseSleepLockTransiently() {
    LOG_DEBUG(Service_AM, "called; virtual sleep arbiter reacquires immediately");

    // Encore has no asynchronous host sleep arbiter. Model the transient release
    // followed by the immediate reacquisition that RequestToAcquireSleepLock
    // already exposes to the guest.
    m_applet->sleep_lock_event.Clear(system.Kernel());
    m_applet->sleep_lock_event.Signal(system.Kernel());
    R_SUCCEED();
}

Result ICommonStateGetter::GetAcquiredSleepLockEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AM, "called");
    *out_event = m_applet->sleep_lock_event.GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::GetReaderLockAccessorEx(
    Out<SharedPointer<ILockAccessor>> out_lock_accessor, u32 button_type) {
    LOG_INFO(Service_AM, "called, button_type={}", button_type);
    *out_lock_accessor = std::make_shared<ILockAccessor>(system);
    R_SUCCEED();
}

Result ICommonStateGetter::GetWriterLockAccessorEx(
    Out<SharedPointer<ILockAccessor>> out_lock_accessor, u32 button_type) {
    LOG_INFO(Service_AM, "called, button_type={}", button_type);
    *out_lock_accessor = std::make_shared<ILockAccessor>(system);
    R_SUCCEED();
}

Result ICommonStateGetter::GetDefaultDisplayResolutionChangeEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->lifecycle_manager.GetOperationModeChangedSystemEvent().GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::GetHdcpAuthenticationState(Out<s32> out_state) {
    std::scoped_lock lk{m_applet->lock};
    *out_state = m_applet->hdcp_authentication_activated ? 1 : 0;
    LOG_DEBUG(Service_AM, "called, state={}", *out_state);
    R_SUCCEED();
}

Result ICommonStateGetter::GetHdcpAuthenticationStateChangeEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->lifecycle_manager.GetHDCPStateChangedEvent().GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::GetOperationMode(Out<OperationMode> out_operation_mode) {
    const bool use_docked_mode{Settings::IsDockedMode()};
    LOG_DEBUG(Service_AM, "called, use_docked_mode={}", use_docked_mode);
    *out_operation_mode = use_docked_mode ? OperationMode::Docked : OperationMode::Handheld;
    R_SUCCEED();
}

Result ICommonStateGetter::GetPerformanceMode(Out<APM::PerformanceMode> out_performance_mode) {
    LOG_DEBUG(Service_AM, "called");
    *out_performance_mode = system.GetAPMController().GetCurrentPerformanceMode();
    R_SUCCEED();
}

Result ICommonStateGetter::SetPerformanceConfigurationChangedNotification(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->lifecycle_manager.SetPerformanceModeChangedNotificationEnabled(system.Kernel(),
                                                                             enabled);
    R_SUCCEED();
}

Result ICommonStateGetter::GetCurrentPerformanceConfiguration(
    Out<APM::PerformanceConfiguration> out_performance_configuration) {
    LOG_DEBUG(Service_AM, "called");
    auto& controller = system.GetAPMController();
    *out_performance_configuration =
        controller.GetCurrentPerformanceConfiguration(controller.GetCurrentPerformanceMode());
    R_SUCCEED();
}

Result ICommonStateGetter::GetCradleStatus(Out<u8> out_status) {
    // Horizon exposes the dock/cradle state as a u8. Encore already models this through its
    // docked/handheld operation-mode setting, so return that same source of truth.
    *out_status = Settings::IsDockedMode() ? 1 : 0;
    LOG_DEBUG(Service_AM, "called, status={}", *out_status);
    R_SUCCEED();
}

Result ICommonStateGetter::GetCradleFwVersion(Out<u32> out_version0, Out<u32> out_version1,
                                              Out<u32> out_version2, Out<u32> out_version3) {
    // PS5 has no Horizon cradle firmware. Report the neutral all-zero version used by upstream
    // compatibility implementations rather than fabricating dock firmware.
    *out_version0 = 0;
    *out_version1 = 0;
    *out_version2 = 0;
    *out_version3 = 0;
    LOG_DEBUG(Service_AM, "called, cradle firmware unavailable");
    R_SUCCEED();
}

Result ICommonStateGetter::SetTvPowerStateMatchingMode(u32 mode) {
    // HDMI/CEC power matching is owned by Prospero OS. Guest software may set the Horizon policy,
    // but Encore intentionally does not propagate it to the host television or PS5 power state.
    LOG_DEBUG(Service_AM, "called, mode={} (host TV power policy unchanged)", mode);
    R_SUCCEED();
}

Result ICommonStateGetter::GetBootMode(Out<PM::SystemBootMode> out_boot_mode) {
    LOG_DEBUG(Service_AM, "called");
    *out_boot_mode = Service::PM::SystemBootMode::Normal;
    R_SUCCEED();
}

Result ICommonStateGetter::IsVrModeEnabled(Out<bool> out_is_vr_mode_enabled) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    *out_is_vr_mode_enabled = m_applet->vr_mode_enabled;
    R_SUCCEED();
}

Result ICommonStateGetter::SetVrModeEnabled(bool is_vr_mode_enabled) {
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled = is_vr_mode_enabled;
    LOG_WARNING(Service_AM, "VR Mode is {}", m_applet->vr_mode_enabled ? "on" : "off");
    R_SUCCEED();
}

Result ICommonStateGetter::SetLcdBacklighOffEnabled(bool is_lcd_backlight_off_enabled) {
    LOG_DEBUG(Service_AM, "called. is_lcd_backlight_off_enabled={}",
              is_lcd_backlight_off_enabled);

    // PS5 has no Switch LCD backlight to drive. Preserve the guest-visible request so the
    // emulated AM state remains coherent while intentionally leaving the host display unchanged.
    std::scoped_lock lk{m_applet->lock};
    m_applet->lcd_backlight_off_enabled = is_lcd_backlight_off_enabled;

    R_SUCCEED();
}

Result ICommonStateGetter::BeginVrModeEx() {
    LOG_DEBUG(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled = true;
    R_SUCCEED();
}

Result ICommonStateGetter::EndVrModeEx() {
    LOG_DEBUG(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled = false;
    R_SUCCEED();
}

Result ICommonStateGetter::IsInControllerFirmwareUpdateSection(
    Out<bool> out_is_in_controller_firmware_update_section) {
    LOG_DEBUG(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    *out_is_in_controller_firmware_update_section =
        m_applet->controller_firmware_update_section;
    R_SUCCEED();
}

Result ICommonStateGetter::GetDefaultDisplayResolution(Out<s32> out_width, Out<s32> out_height) {
    LOG_DEBUG(Service_AM, "called");

    if (Settings::IsDockedMode()) {
        *out_width = static_cast<u32>(Service::VI::DisplayResolution::DockedWidth);
        *out_height = static_cast<u32>(Service::VI::DisplayResolution::DockedHeight);
    } else {
        *out_width = static_cast<u32>(Service::VI::DisplayResolution::UndockedWidth);
        *out_height = static_cast<u32>(Service::VI::DisplayResolution::UndockedHeight);
    }

    R_SUCCEED();
}

Result ICommonStateGetter::RejectUnsupportedLegacyCommand() {
    // These legacy commands return an error on Horizon.
    R_RETURN(ResultUnknown);
}

Result ICommonStateGetter::CancelCpuBoostMode() {
    system.GetAPMController().SetFromCpuBoostMode(APM::CpuBoostMode::Normal);
    R_SUCCEED();
}

void ICommonStateGetter::SetCpuBoostMode(HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called, forwarding to APM:SYS");

    const auto& sm = system.ServiceManager();
    const auto apm_sys = sm.GetService<APM::APM_Sys>("apm:sys");
    ASSERT(apm_sys != nullptr);

    apm_sys->SetCpuBoostMode(ctx);
}

Result ICommonStateGetter::GetBuiltInDisplayType(Out<s32> out_display_type) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_display_type = 0;
    R_SUCCEED();
}

Result ICommonStateGetter::PerformSystemButtonPressingIfInFocus(SystemButtonType type) {
    LOG_DEBUG(Service_AM, "called, type={}", type);

    std::scoped_lock lk{m_applet->lock};

    switch (type) {
    case SystemButtonType::HomeButtonShortPressing:
        if (!m_applet->home_button_short_pressed_blocked) {
            m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(),
                AppletMessage::DetectShortPressingHomeButton);
        }
        break;
    case SystemButtonType::HomeButtonLongPressing:
        if (!m_applet->home_button_long_pressed_blocked) {
            m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(),
                AppletMessage::DetectLongPressingHomeButton);
        }
        break;
    case SystemButtonType::CaptureButtonShortPressing:
        if (m_applet->handling_capture_button_short_pressed_message_enabled_for_applet) {
            m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(),
                AppletMessage::DetectShortPressingCaptureButton);
        }
        break;
    case SystemButtonType::CaptureButtonLongPressing:
        if (m_applet->handling_capture_button_long_pressed_message_enabled_for_applet) {
            m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(),
                AppletMessage::DetectLongPressingCaptureButton);
        }
        break;
    default:
        // Other buttons ignored for now
        break;
    }

    R_SUCCEED();
}

Result ICommonStateGetter::EnableStartupLogoDisappearedMessage() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(), AppletMessage::StartupLogoDisappeared);
    R_SUCCEED();
}

namespace {
u64 GetSteadyClockNanoseconds() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}
} // namespace

Result ICommonStateGetter::DisableSleepTillShutdown() {
    LOG_INFO(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    m_applet->sleep_disabled_till_shutdown = true;
    R_SUCCEED();
}

Result ICommonStateGetter::SuppressDisablingSleepTemporarily(u64 duration_ns) {
    const u64 now_ns = GetSteadyClockNanoseconds();
    const u64 max_value = std::numeric_limits<u64>::max();
    const u64 deadline_ns = duration_ns > max_value - now_ns ? max_value : now_ns + duration_ns;
    LOG_INFO(Service_AM, "called, duration_ns={}", duration_ns);
    std::scoped_lock lk{m_applet->lock};
    m_applet->sleep_disable_suppressed_until_ns = deadline_ns;
    R_SUCCEED();
}

Result ICommonStateGetter::IsSleepEnabled(Out<bool> out_is_sleep_enabled) {
    const u64 now_ns = GetSteadyClockNanoseconds();
    std::scoped_lock lk{m_applet->lock};
    const bool suppression_active = now_ns < m_applet->sleep_disable_suppressed_until_ns;
    *out_is_sleep_enabled = !m_applet->sleep_disabled_till_shutdown || suppression_active;
    R_SUCCEED();
}

Result ICommonStateGetter::IsDisablingSleepSuppressed(Out<bool> out_is_suppressed) {
    const u64 now_ns = GetSteadyClockNanoseconds();
    std::scoped_lock lk{m_applet->lock};
    *out_is_suppressed = now_ns < m_applet->sleep_disable_suppressed_until_ns;
    R_SUCCEED();
}

Result ICommonStateGetter::GetOperationModeSystemInfo(Out<u32> out_operation_mode_system_info) {
    // Compatibility fallback used by Eden/Lichii/Eglantine. The OMM cmd26
    // bit semantics are not qualified yet, so keep the neutral value rather
    // than failing a recognized AM query with ResultUnknown.
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_operation_mode_system_info = 0;
    R_SUCCEED();
}

Result ICommonStateGetter::GetAppletLaunchedHistory(
    Out<s32> out_count, OutArray<AppletId, BufferAttr_HipcMapAlias> out_applet_ids) {
    LOG_INFO(Service_AM, "called");

    std::shared_ptr<Applet> current_applet = m_applet;

    for (*out_count = 0;
         *out_count < static_cast<s32>(out_applet_ids.size()) && current_applet != nullptr;
         /* ... */) {
        out_applet_ids[(*out_count)++] = current_applet->applet_id;
        current_applet = current_applet->caller_applet.lock();
    }

    R_SUCCEED();
}

Result ICommonStateGetter::GetSettingsPlatformRegion(
    Out<Set::PlatformRegion> out_settings_platform_region) {
    LOG_INFO(Service_AM, "called");
    *out_settings_platform_region = Set::PlatformRegion::Global;
    R_SUCCEED();
}

Result ICommonStateGetter::SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled() {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->request_exit_to_library_applet_at_execute_next_program_enabled = true;

    R_SUCCEED();
}

Result ICommonStateGetter::PushToGeneralChannel(SharedPointer<IStorage> storage) {
    LOG_DEBUG(Service_AM, "called");
    system.PushGeneralChannelData(storage->GetData());
    R_SUCCEED();
}

Result ICommonStateGetter::GetHomeButtonReaderLockAccessor(Out<SharedPointer<ILockAccessor>> out_lock_accessor) {
    LOG_DEBUG(Service_AM, "called");
    *out_lock_accessor = std::make_shared<ILockAccessor>(system);
    R_SUCCEED();
}

Result ICommonStateGetter::SetHandlingHomeButtonShortPressedEnabled(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={} applet_id={}", enabled, m_applet->applet_id);

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_short_pressed_blocked = !enabled;
    R_SUCCEED();
}

Result ICommonStateGetter::Unknown610(u64 unk) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ICommonStateGetter::Unknown611(u8 unk) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ICommonStateGetter::BeginVrMode3d() {
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled_3d = true;
    LOG_WARNING(Service_AM, "VR Mode is {}", m_applet->vr_mode_enabled_3d ? "on" : "off");
    R_SUCCEED();
}

Result ICommonStateGetter::EndVrMode3d() {
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled_3d = false;
    LOG_WARNING(Service_AM, "VR Mode is {}", m_applet->vr_mode_enabled_3d ? "on" : "off");
    R_SUCCEED();
}

Result ICommonStateGetter::IsVrModeEnabled3d(Out<bool> out_is_vr_mode_enabled_3d) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    std::scoped_lock lk{m_applet->lock};
    *out_is_vr_mode_enabled_3d = m_applet->vr_mode_enabled_3d;
    R_SUCCEED();
}

Result ICommonStateGetter::GetVrLaboGoggleViewport(Out<s32> out_x, Out<s32> out_y, Out<s32> out_width, Out<s32> out_height) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_x = 0;
    *out_y = 0;
    *out_width = 1280;
    *out_height = 720;
    R_SUCCEED();
}

Result ICommonStateGetter::GetPanelPhysicalSizeForSpecificTitle(Out<f32> out_width, Out<f32> out_height) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_width = 137250.0f / 1000.0f;
    *out_height = 77200.0f / 1000.0f;
    R_SUCCEED();
}

Result ICommonStateGetter::GetPanelResolutionForSpecificTitle(Out<s32> out_width, Out<s32> out_height) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_width = 1280;
    *out_height = 720;
    R_SUCCEED();
}

} // namespace Service::AM
