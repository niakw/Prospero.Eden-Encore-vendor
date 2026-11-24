// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/overlay_functions.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/window_system.h"

namespace Service::AM {
    IOverlayFunctions::IOverlayFunctions(Core::System &system_, std::shared_ptr<Applet> applet)
        : ServiceFramework{system_, "IOverlayFunctions"}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IOverlayFunctions::BeginToWatchShortHomeButtonMessage>, "BeginToWatchShortHomeButtonMessage"},
        {1, D<&IOverlayFunctions::EndToWatchShortHomeButtonMessage>, "EndToWatchShortHomeButtonMessage"},
        {2, D<&IOverlayFunctions::GetApplicationIdForLogo>, "GetApplicationIdForLogo"},
        {3, D<&IOverlayFunctions::SetGpuTimeSliceBoost>, "SetGpuTimeSliceBoost"},
        {4, D<&IOverlayFunctions::SetAutoSleepTimeAndDimmingTimeEnabled>, "SetAutoSleepTimeAndDimmingTimeEnabled"},
        {5, D<&IOverlayFunctions::TerminateApplicationAndSetReason>, "TerminateApplicationAndSetReason"},
        {6, D<&IOverlayFunctions::SetScreenShotPermissionGlobally>, "SetScreenShotPermissionGlobally"},
        {10, D<&IOverlayFunctions::StartShutdownSequenceForOverlay>, "StartShutdownSequenceForOverlay"},
        {11, D<&IOverlayFunctions::StartRebootSequenceForOverlay>, "StartRebootSequenceForOverlay"},
        {20, D<&IOverlayFunctions::SetHandlingHomeButtonShortPressedEnabled>, "SetHandlingHomeButtonShortPressedEnabled"},
        {21, D<&IOverlayFunctions::SetHandlingTouchScreenInputEnabled>, "SetHandlingTouchScreenInputEnabled"},
        {30, D<&IOverlayFunctions::SetHealthWarningShowingState>, "SetHealthWarningShowingState"},
        {31, D<&IOverlayFunctions::IsHealthWarningRequired>, "IsHealthWarningRequired"},
        {40, D<&IOverlayFunctions::GetApplicationNintendoLogo>, "GetApplicationNintendoLogo"},
        {41, D<&IOverlayFunctions::GetApplicationStartupMovie>, "GetApplicationStartupMovie"},
        {50, D<&IOverlayFunctions::SetGpuTimeSliceBoostForApplication>, "SetGpuTimeSliceBoostForApplication"},
        {60, D<&IOverlayFunctions::RejectUnknown60>, "Unknown60"},
        {70, D<&IOverlayFunctions::Unknown70>, "Unknown70"},
        {71, D<&IOverlayFunctions::RejectUnknown71>, "Unknown71"},
        {75, D<&IOverlayFunctions::RejectUnknown75>, "Unknown75"},
        {90, D<&IOverlayFunctions::SetRequiresGpuResourceUse>, "SetRequiresGpuResourceUse"},
        {101, D<&IOverlayFunctions::RejectDebugOnlyCommand>, "BeginToObserveHidInputForDevelop"},
    };
        // clang-format on

        RegisterHandlers(functions);
    }

    IOverlayFunctions::~IOverlayFunctions() = default;

    Result IOverlayFunctions::RejectDebugOnlyCommand() {
        LOG_WARNING(Service_AM, "Rejected overlay development command on release runtime");
        R_RETURN(ResultUnknown);
    }

    Result IOverlayFunctions::BeginToWatchShortHomeButtonMessage() {
        LOG_DEBUG(Service_AM, "called");

        {
            std::scoped_lock lk{m_applet->lock};
            m_applet->overlay_watching_short_home_button = true;
        }

        if (auto* window_system = system.GetAppletManager().GetWindowSystem()) {
            window_system->RequestUpdate();
        }

        R_SUCCEED();
    }

    Result IOverlayFunctions::EndToWatchShortHomeButtonMessage() {
        LOG_DEBUG(Service_AM, "called");

        {
            std::scoped_lock lk{m_applet->lock};
            m_applet->overlay_watching_short_home_button = false;
        }

        if (auto* window_system = system.GetAppletManager().GetWindowSystem()) {
            window_system->RequestUpdate();
        }

        R_SUCCEED();
    }

    Result IOverlayFunctions::SetHandlingTouchScreenInputEnabled(bool enabled) {
        LOG_DEBUG(Service_AM, "called, enabled={}", enabled);

        std::scoped_lock lk{m_applet->lock};
        m_applet->overlay_handling_touch_input = enabled;
        R_SUCCEED();
    }

    Result IOverlayFunctions::GetApplicationIdForLogo(Out<u64> out_application_id) {
        LOG_DEBUG(Service_AM, "called");

        std::shared_ptr<Applet> target_applet;

        auto* window_system = system.GetAppletManager().GetWindowSystem();
        if (window_system) {
            target_applet = window_system->GetMainApplet();
            if (target_applet) {
                std::scoped_lock lk{target_applet->lock};
                LOG_DEBUG(Service_AM, "applet_id={}, program_id={:016X}, type={}",
                          static_cast<u32>(target_applet->applet_id), target_applet->program_id,
                          static_cast<u32>(target_applet->type));

                u64 id = target_applet->screen_shot_identity.application_id;
                if (id == 0) {
                    id = target_applet->program_id;
                }
                LOG_DEBUG(Service_AM, "application_id={:016X}", id);
                *out_application_id = id;
                R_SUCCEED();
            }
        }

        std::scoped_lock lk{m_applet->lock};
        u64 id = m_applet->screen_shot_identity.application_id;
        if (id == 0) {
            id = m_applet->program_id;
        }
        LOG_DEBUG(Service_AM, "application_id={:016X} (fallback)", id);
        *out_application_id = id;
        R_SUCCEED();
    }


    u64 IOverlayFunctions::ResolveApplicationIdForLogo() {
        if (auto* window_system = system.GetAppletManager().GetWindowSystem()) {
            if (const auto target_applet = window_system->GetMainApplet()) {
                std::scoped_lock lk{target_applet->lock};
                const u64 id = target_applet->screen_shot_identity.application_id != 0
                                   ? target_applet->screen_shot_identity.application_id
                                   : target_applet->program_id;
                if (id != 0) {
                    return id;
                }
            }
        }

        std::scoped_lock lk{m_applet->lock};
        return m_applet->screen_shot_identity.application_id != 0
                   ? m_applet->screen_shot_identity.application_id
                   : m_applet->program_id;
    }

    Result IOverlayFunctions::ReadApplicationLogoResource(
        Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
        std::string_view file_name) {
        const u64 application_id = ResolveApplicationIdForLogo();
        R_UNLESS(application_id != 0, AM::ResultApplicationRecordNotFound);

        auto& content_provider = system.GetContentProviderUnion();
        auto program =
            content_provider.GetEntry(application_id, FileSys::ContentRecordType::Program);
        if (!program) {
            const u64 base_id = FileSys::GetBaseTitleID(application_id);
            if (base_id != application_id) {
                program = content_provider.GetEntry(base_id, FileSys::ContentRecordType::Program);
            }
        }
        R_UNLESS(program != nullptr, AM::ResultApplicationRecordNotFound);

        const auto logo_dir = program->GetLogoPartition();
        R_UNLESS(logo_dir != nullptr, ResultUnknown);

        const auto file = logo_dir->GetFile(file_name);
        R_UNLESS(file != nullptr, ResultUnknown);

        const auto data = file->ReadAllBytes();
        R_UNLESS(data.size() <= out_buffer.size(), ResultUnknown);

        if (!data.empty()) {
            std::memcpy(out_buffer.data(), data.data(), data.size());
        }
        *out_size = static_cast<u64>(data.size());

        LOG_DEBUG(Service_AM, "read {} bytes from {} for application_id={:016X}", data.size(),
                  file_name, application_id);
        R_SUCCEED();
    }

    Result IOverlayFunctions::GetApplicationNintendoLogo(
        Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
        R_RETURN(ReadApplicationLogoResource(out_size, out_buffer, "NintendoLogo.png"));
    }

    Result IOverlayFunctions::GetApplicationStartupMovie(
        Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
        R_RETURN(ReadApplicationLogoResource(out_size, out_buffer, "StartupMovie.gif"));
    }

    Result IOverlayFunctions::SetGpuTimeSliceBoost(u64 value) {
        // Horizon's GPU time-slice request targets the Switch scheduler. Encore's guest GPU work
        // is scheduled by the PS5 host/RADV path and must not alter host scheduling policy.
        LOG_DEBUG(Service_AM, "called, value={} (host GPU scheduler unchanged)", value);
        R_SUCCEED();
    }

    Result IOverlayFunctions::TerminateApplicationAndSetReason(Result reason) {
        LOG_INFO(Service_AM, "called, reason={:#x}", reason.GetInnerValue());

        auto* window_system = system.GetAppletManager().GetWindowSystem();
        R_UNLESS(window_system != nullptr, ResultUnknown);

        const auto application = window_system->GetMainApplet();
        R_UNLESS(application != nullptr, AM::ResultApplicationRecordNotFound);

        {
            std::scoped_lock lk{application->lock};
            application->terminate_result = reason;
            application->process->Terminate();
        }

        window_system->RequestUpdate();
        R_SUCCEED();
    }

    Result IOverlayFunctions::StartShutdownSequenceForOverlay() {
        LOG_INFO(Service_AM, "called");
        // Same guest power transition used by IGlobalStateController. This exits the emulated
        // Horizon system; it does not request a PS5 host shutdown.
        system.Exit();
        R_SUCCEED();
    }

    Result IOverlayFunctions::StartRebootSequenceForOverlay() {
        LOG_INFO(Service_AM, "called");
        // Encore has no guest reboot relaunch path distinct from shutdown yet; the global AM
        // controller uses the same system-exit transition, so keep both interfaces coherent.
        system.Exit();
        R_SUCCEED();
    }

    Result IOverlayFunctions::SetGpuTimeSliceBoostForApplication(u64 value) {
        // HOS23 ABI is a single u64. Ryujinx treats this policy hint as an accepted no-op,
        // and Encore does not expose a separate host GPU time-slice boost control.
        LOG_DEBUG(Service_AM, "called, value={} (accepted policy hint)", value);
        R_SUCCEED();
    }

    Result IOverlayFunctions::SetRequiresGpuResourceUse(bool required) {
        // SwitchBrew documents this command as an input bool that simply returns success.
        LOG_DEBUG(Service_AM, "called, required={} (contractual no-op)", required);
        R_SUCCEED();
    }

    Result IOverlayFunctions::SetScreenShotPermissionGlobally(bool enabled) {
        LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
        std::scoped_lock lk{m_applet->lock};
        m_applet->screenshot_permission =
            enabled ? ScreenshotPermission::Enable : ScreenshotPermission::Disable;
        R_SUCCEED();
    }

    Result IOverlayFunctions::SetHealthWarningShowingState(bool showing) {
        // Encore has no PS5-host health-warning overlay corresponding to Horizon's warning UI.
        // When the guest reports the warning hidden, signal the existing disappearance event so
        // waiters can continue instead of blocking on an unrendered warning.
        LOG_DEBUG(Service_AM, "called, showing={}", showing);
        if (!showing) {
            m_applet->health_warning_disappeared_system_event.Signal();
        }
        R_SUCCEED();
    }

    Result IOverlayFunctions::SetAutoSleepTimeAndDimmingTimeEnabled(bool enabled) {
        LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
        std::scoped_lock lk{m_applet->lock};
        m_applet->auto_sleep_disabled = !enabled;
        R_SUCCEED();
    }

    Result IOverlayFunctions::IsHealthWarningRequired(Out<bool> is_required) {
        LOG_DEBUG(Service_AM, "called");
        std::scoped_lock lk{m_applet->lock};
        *is_required = false;
        R_SUCCEED();
    }

    Result IOverlayFunctions::SetHandlingHomeButtonShortPressedEnabled(bool enabled) {
        LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
        std::scoped_lock lk{m_applet->lock};
        m_applet->home_button_short_pressed_blocked = !enabled;
        R_SUCCEED();
    }

    Result IOverlayFunctions::RejectUnknown60(bool value) {
        LOG_WARNING(Service_AM, "Rejected unknown HOS23 overlay cmd60, value={}", value);
        R_RETURN(ResultUnknown);
    }

    Result IOverlayFunctions::RejectUnknown71() {
        LOG_WARNING(Service_AM, "Rejected unknown HOS23 overlay cmd71");
        R_RETURN(ResultUnknown);
    }

    Result IOverlayFunctions::RejectUnknown75(Out<u64> out_value) {
        *out_value = 0;
        LOG_WARNING(Service_AM, "Rejected unknown HOS23 overlay cmd75");
        R_RETURN(ResultUnknown);
    }

    Result IOverlayFunctions::Unknown70() {
        LOG_DEBUG(Service_AM, "called");
        R_SUCCEED();
    }
} // namespace Service::AM
