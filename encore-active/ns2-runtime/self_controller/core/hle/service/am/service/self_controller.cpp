// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "common/scope_exit.h"
#include <new>
#include "core/hle/result.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/am/service/self_controller.h"
#include "core/hle/service/am/service/graphics_supervisor.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/caps/caps_su.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/nvnflinger/nvnflinger.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/vi_results.h"

namespace Service::AM {

ISelfController::ISelfController(Core::System& system_, std::shared_ptr<Applet> applet,
                                 Kernel::KProcess* process)
    : ServiceFramework{system_, "ISelfController"}, m_process{process}, m_applet{
                                                                            std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISelfController::Exit>, "Exit"},
        {1, D<&ISelfController::LockExit>, "LockExit"},
        {2, D<&ISelfController::UnlockExit>, "UnlockExit"},
        {3, D<&ISelfController::EnterFatalSection>, "EnterFatalSection"},
        {4, D<&ISelfController::LeaveFatalSection>, "LeaveFatalSection"},
        {9, D<&ISelfController::GetLibraryAppletLaunchableEvent>, "GetLibraryAppletLaunchableEvent"},
        {10, D<&ISelfController::SetScreenShotPermission>, "SetScreenShotPermission"},
        {11, D<&ISelfController::SetOperationModeChangedNotification>, "SetOperationModeChangedNotification"},
        {12, D<&ISelfController::SetPerformanceModeChangedNotification>, "SetPerformanceModeChangedNotification"},
        {13, D<&ISelfController::SetFocusHandlingMode>, "SetFocusHandlingMode"},
        {14, D<&ISelfController::SetRestartMessageEnabled>, "SetRestartMessageEnabled"},
        {15, D<&ISelfController::SetScreenShotAppletIdentityInfo>, "SetScreenShotAppletIdentityInfo"},
        {16, D<&ISelfController::SetOutOfFocusSuspendingEnabled>, "SetOutOfFocusSuspendingEnabled"},
        {17, D<&ISelfController::SetControllerFirmwareUpdateSection>, "SetControllerFirmwareUpdateSection"},
        {18, D<&ISelfController::SetRequiresCaptureButtonShortPressedMessage>, "SetRequiresCaptureButtonShortPressedMessage"},
        {19, D<&ISelfController::SetAlbumImageOrientation>, "SetAlbumImageOrientation"},
        {20, D<&ISelfController::SetDesirableKeyboardLayout>, "SetDesirableKeyboardLayout"},
        {21, D<&ISelfController::GetScreenShotProgramId>, "GetScreenShotProgramId"},
        {40, D<&ISelfController::CreateManagedDisplayLayer>, "CreateManagedDisplayLayer"},
        {41, D<&ISelfController::IsSystemBufferSharingEnabled>, "IsSystemBufferSharingEnabled"},
        {42, D<&ISelfController::GetSystemSharedLayerHandle>, "GetSystemSharedLayerHandle"},
        {43, D<&ISelfController::GetSystemSharedBufferHandle>, "GetSystemSharedBufferHandle"},
        {44, D<&ISelfController::CreateManagedDisplaySeparableLayer>, "CreateManagedDisplaySeparableLayer"},
        {45, D<&ISelfController::RejectUnavailableManagedDisplayLayerSeparationMode>, "SetManagedDisplayLayerSeparationMode"},
        {46, D<&ISelfController::RejectUnavailableRecordingLayerComposition>, "SetRecordingLayerCompositionEnabled"},
        {50, D<&ISelfController::SetHandlesRequestToDisplay>, "SetHandlesRequestToDisplay"},
        {51, D<&ISelfController::ApproveToDisplay>, "ApproveToDisplay"},
        {60, D<&ISelfController::OverrideAutoSleepTimeAndDimmingTime>, "OverrideAutoSleepTimeAndDimmingTime"},
        {61, D<&ISelfController::SetMediaPlaybackState>, "SetMediaPlaybackState"},
        {62, D<&ISelfController::SetIdleTimeDetectionExtension>, "SetIdleTimeDetectionExtension"},
        {63, D<&ISelfController::GetIdleTimeDetectionExtension>, "GetIdleTimeDetectionExtension"},
        {64, D<&ISelfController::SetInputDetectionSourceSet>, "SetInputDetectionSourceSet"},
        {65, D<&ISelfController::ReportUserIsActive>, "ReportUserIsActive"},
        {66, D<&ISelfController::GetCurrentIlluminance>, "GetCurrentIlluminance"},
        {67, D<&ISelfController::IsIlluminanceAvailable>, "IsIlluminanceAvailable"},
        {68, D<&ISelfController::SetAutoSleepDisabled>, "SetAutoSleepDisabled"},
        {69, D<&ISelfController::IsAutoSleepDisabled>, "IsAutoSleepDisabled"},
        {70, &ISelfController::ReportMultimediaError, "ReportMultimediaError"},
        {71, D<&ISelfController::GetCurrentIlluminanceEx>, "GetCurrentIlluminanceEx"},
        {72, D<&ISelfController::SetInputDetectionPolicy>, "SetInputDetectionPolicy"},
        {80, D<&ISelfController::SetWirelessPriorityMode>, "SetWirelessPriorityMode"},
        {90, D<&ISelfController::GetAccumulatedSuspendedTickValue>, "GetAccumulatedSuspendedTickValue"},
        {91, D<&ISelfController::GetAccumulatedSuspendedTickChangedEvent>, "GetAccumulatedSuspendedTickChangedEvent"},
        {100, D<&ISelfController::SetAlbumImageTakenNotificationEnabled>, "SetAlbumImageTakenNotificationEnabled"},
        {110, &ISelfController::SetApplicationAlbumUserData, "SetApplicationAlbumUserData"},
        {120, D<&ISelfController::SaveCurrentScreenshot>, "SaveCurrentScreenshot"},
        {130, D<&ISelfController::SetRecordVolumeMuted>, "SetRecordVolumeMuted"},
        {230, D<&ISelfController::Unknown230>, "Unknown230"},
        {240, D<&ISelfController::Unknown240>, "Unknown240"},
        {1000, D<&ISelfController::RejectDebugOnlyCommand>, "GetDebugStorageChannel"},
        {2000, &ISelfController::GetGraphicsSupervisor, "GetGraphicsSupervisor"},
    };
    // clang-format on

    RegisterHandlers(functions);

    std::scoped_lock lk{m_applet->lock};
    m_applet->display_layer_manager.Initialize(system, m_process, m_applet->applet_id,
                                               m_applet->library_applet_mode);
}

ISelfController::~ISelfController() {
    if (m_graphics_supervisor_session) m_graphics_supervisor_session->Close(kernel);
    std::scoped_lock lk{m_applet->lock};
    m_applet->display_layer_manager.Finalize();
}

void ISelfController::StubReplyZeroedNx2(HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) NS2 SelfController command {}", ctx.GetCommand());
    constexpr u32 ZeroWords = 32;
    IPC::ResponseBuilder rb{ctx, 2 + ZeroWords};
    rb.Push(ResultSuccess.raw);
    for (u32 i = 0; i < ZeroWords; ++i) {
        rb.Push<u32>(0);
    }
}

// NS2-only command 2000: preserve a real kernel copy handle and its lifetime.
Result ISelfController::SetControllerFirmwareUpdateSection(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->controller_firmware_update_section = enabled;
    R_SUCCEED();
}

Result ISelfController::SetRequiresCaptureButtonShortPressedMessage(bool required) {
    LOG_DEBUG(Service_AM, "called, required={}", required);

    std::scoped_lock lk{m_applet->lock};
    m_applet->handling_capture_button_short_pressed_message_enabled_for_applet = required;

    R_SUCCEED();
}

Result ISelfController::SetDesirableKeyboardLayout(u32 layout) {
    LOG_DEBUG(Service_AM, "called, layout={}", layout);
    std::scoped_lock lk{m_applet->lock};
    m_applet->desirable_keyboard_layout = layout;
    R_SUCCEED();
}

Result ISelfController::GetScreenShotProgramId(Out<u64> out_program_id) {
    std::scoped_lock lk{m_applet->lock};

    const u64 identity_program_id = m_applet->screen_shot_identity.application_id;
    *out_program_id = identity_program_id != 0 ? identity_program_id : m_applet->program_id;

    LOG_DEBUG(Service_AM, "called, program_id={:016X}", *out_program_id);
    R_SUCCEED();
}

Result ISelfController::RejectUnavailableManagedDisplayLayerSeparationMode(u32 mode) {
    LOG_WARNING(Service_AM,
                "SetManagedDisplayLayerSeparationMode unavailable: recording layer backend is not materialized, mode={}",
                mode);
    R_UNLESS(mode <= 1, ResultUnknown);
    R_RETURN(ResultUnknown);
}

Result ISelfController::RejectUnavailableRecordingLayerComposition(bool enabled) {
    LOG_WARNING(Service_AM,
                "SetRecordingLayerCompositionEnabled unavailable: recording layer backend is not materialized, enabled={}",
                enabled);
    R_RETURN(ResultUnknown);
}

Result ISelfController::SetInputDetectionSourceSet(u32 input_detection_source_set) {
    m_input_detection_source_set = input_detection_source_set;
    LOG_DEBUG(Service_AM, "called, input_detection_source_set={:#x}",
              m_input_detection_source_set);
    R_SUCCEED();
}

Result ISelfController::GetCurrentIlluminance(Out<f32> out_illuminance) {
    // PS5 integration exposes no ambient-light sensor; pair this neutral value with
    // IsIlluminanceAvailable=false so guests can handle the unsupported capability normally.
    *out_illuminance = 0.0f;
    LOG_DEBUG(Service_AM, "called, illuminance={}", *out_illuminance);
    R_SUCCEED();
}

void ISelfController::ReportMultimediaError(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u32 error = rp.Pop<u32>();
    LOG_DEBUG(Service_AM, "Multimedia error reported by guest, code={:#x}, context_size={}", error,
              ctx.GetReadBufferSize());

    // This is a report/notification command. Encore has no Horizon multimedia-error collector;
    // accepting the notification has no guest-visible state transition.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

Result ISelfController::GetCurrentIlluminanceEx(Out<CurrentIlluminanceEx> out_illuminance) {
    *out_illuminance = {};
    LOG_DEBUG(Service_AM, "called, ambient-light sensor unavailable");
    R_SUCCEED();
}

Result ISelfController::SetWirelessPriorityMode(u32 wireless_priority_mode) {
    // There is no Horizon WLAN scheduler on PS5. Keep the requested value as guest-visible
    // service state without attempting to alter host networking priorities.
    m_wireless_priority_mode = wireless_priority_mode;
    LOG_DEBUG(Service_AM, "called, wireless_priority_mode={}", m_wireless_priority_mode);
    R_SUCCEED();
}

void ISelfController::SetApplicationAlbumUserData(HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called, application_album_user_data_size={}", ctx.GetReadBufferSize());

    // The current screenshot backend writes image/title/timestamp data directly and has no
    // consumer for Horizon's opaque application album-user-data blob.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

Result ISelfController::RejectDebugOnlyCommand() {
    LOG_WARNING(Service_AM, "Rejected self-controller debug command on release runtime");
    R_RETURN(ResultUnknown);
}

void ISelfController::GetGraphicsSupervisor(HLERequestContext& ctx) {
    if (m_graphics_supervisor_session) {
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess.raw);
        rb.PushCopyObjects(ctx, m_graphics_supervisor_session);
        return;
    }
    Kernel::KScopedResourceReservation reservation{
        kernel, Kernel::GetCurrentProcessPointer(kernel), Kernel::LimitableResource::SessionCountMax};
    if (!reservation.Succeeded()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultLimitReached.raw);
        return;
    }

    // Prepare all fallible host allocations before committing a kernel session.
    // An allocation failure in the graphics supervisor or server manager must
    // leave the quota and both KSession endpoints untouched.
    auto& server = ctx.GetManager()->GetServerManager();
    std::shared_ptr<SessionRequestManager> manager;
    try {
        auto handler = std::make_shared<IGraphicsSupervisor>(
            system, m_process, m_applet->aruid.pid);
        manager = std::make_shared<SessionRequestManager>(kernel, server);
        manager->SetSessionHandler(std::move(handler));
    } catch (const std::bad_alloc&) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultOutOfResource.raw);
        return;
    }

    auto* session = Kernel::KSession::Create(kernel);
    if (!session) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultOutOfResource.raw);
        return;
    }
    session->Initialize(kernel, nullptr, 0);
    reservation.Commit();
    Kernel::KSession::Register(kernel, session);

    // RegisterSession may fail or allocate *after* the session quota was
    // committed. Release both original endpoints unless the server owns
    // its endpoint. On success SelfController retains the client reference;
    // the guest gets a distinct COPY handle and the destructor closes ours.
    bool transferred_to_server = false;
    SCOPE_EXIT {
        if (!transferred_to_server) {
            session->GetClientSession().Close(kernel);
            session->GetServerSession().Close(kernel);
        }
    };
    Result registered = Kernel::ResultOutOfResource;
    try {
        registered = server.RegisterSession(&session->GetServerSession(),
                                            std::move(manager));
    } catch (const std::bad_alloc&) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultOutOfResource.raw);
        return;
    }
    if (registered.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(registered.raw);
        return;
    }
    transferred_to_server = true;
    m_graphics_supervisor_session = &session->GetClientSession();
    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess.raw);
    rb.PushCopyObjects(ctx, m_graphics_supervisor_session);
}

Result ISelfController::Exit() {
    LOG_DEBUG(Service_AM, "called");

    m_applet->process->Terminate();

    R_SUCCEED();
}

Result ISelfController::LockExit() {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};

    if (m_applet->lifecycle_manager.GetExitRequested()) {
        // With exit already requested, ignore and terminate immediately.
        m_applet->process->Terminate();
    } else {
        // Otherwise, set exit lock state.
        m_applet->exit_locked = true;
        system.SetExitLocked(true);
    }

    R_SUCCEED();
}

Result ISelfController::UnlockExit() {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};

    m_applet->exit_locked = false;
    system.SetExitLocked(false);

    if (m_applet->lifecycle_manager.GetExitRequested()) {
        m_applet->process->Terminate();
    }

    R_SUCCEED();
}

Result ISelfController::EnterFatalSection() {
    std::scoped_lock lk{m_applet->lock};

    m_applet->fatal_section_count++;
    LOG_DEBUG(Service_AM, "called. Num fatal sections entered: {}", m_applet->fatal_section_count);

    R_SUCCEED();
}

Result ISelfController::LeaveFatalSection() {
    LOG_DEBUG(Service_AM, "called");

    // Entry and exit of fatal sections must be balanced.
    std::scoped_lock lk{m_applet->lock};
    R_UNLESS(m_applet->fatal_section_count > 0, AM::ResultFatalSectionCountImbalance);
    m_applet->fatal_section_count--;

    R_SUCCEED();
}

Result ISelfController::GetLibraryAppletLaunchableEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    // The guest waits for this event BEFORE calling CreateLibraryApplet (libnx).
    // Encore does not model launch admission contention; preserve the ready state.
    m_applet->library_applet_launchable_event.Signal(system.Kernel());
    *out_event = m_applet->library_applet_launchable_event.GetHandle();

    R_SUCCEED();
}

Result ISelfController::SetScreenShotPermission(ScreenshotPermission screen_shot_permission) {
    LOG_DEBUG(Service_AM, "called, permission={}", screen_shot_permission);

    std::scoped_lock lk{m_applet->lock};
    m_applet->screenshot_permission = screen_shot_permission;

    R_SUCCEED();
}

Result ISelfController::SetOperationModeChangedNotification(bool enabled) {
    LOG_INFO(Service_AM, "called, enabled={}", enabled);

    std::scoped_lock lk{m_applet->lock};
    m_applet->lifecycle_manager.SetOperationModeChangedNotificationEnabled(system.Kernel(), enabled);

    R_SUCCEED();
}

Result ISelfController::SetPerformanceModeChangedNotification(bool enabled) {
    LOG_INFO(Service_AM, "called, enabled={}", enabled);

    std::scoped_lock lk{m_applet->lock};
    m_applet->lifecycle_manager.SetPerformanceModeChangedNotificationEnabled(system.Kernel(), enabled);

    R_SUCCEED();
}

Result ISelfController::SetFocusHandlingMode(bool notify, bool background, bool suspend) {
    LOG_INFO(Service_AM, "called, notify={} background={} suspend={}", notify, background, suspend);

    std::scoped_lock lk{m_applet->lock};
    m_applet->lifecycle_manager.SetFocusStateChangedNotificationEnabled(system.Kernel(), notify);
    m_applet->lifecycle_manager.SetFocusHandlingMode(suspend);
    m_applet->UpdateSuspensionStateLocked(true);

    R_SUCCEED();
}

Result ISelfController::SetRestartMessageEnabled(bool enabled) {
    LOG_INFO(Service_AM, "called, enabled={}", enabled);

    std::scoped_lock lk{m_applet->lock};
    m_applet->lifecycle_manager.SetResumeNotificationEnabled(enabled);

    R_SUCCEED();
}

Result ISelfController::SetScreenShotAppletIdentityInfo(
    AppletIdentityInfo screen_shot_applet_identity_info) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->screen_shot_identity = screen_shot_applet_identity_info;

    R_SUCCEED();
}

Result ISelfController::SetOutOfFocusSuspendingEnabled(bool enabled) {
    LOG_INFO(Service_AM, "called, enabled={}", enabled);

    std::scoped_lock lk{m_applet->lock};
    m_applet->lifecycle_manager.SetOutOfFocusSuspendingEnabled(enabled);
    m_applet->UpdateSuspensionStateLocked(false);

    R_SUCCEED();
}

Result ISelfController::SetAlbumImageOrientation(
    Capture::AlbumImageOrientation album_image_orientation) {
    LOG_WARNING(Service_AM, "(STUBBED) called, orientation={}", album_image_orientation);

    std::scoped_lock lk{m_applet->lock};
    m_applet->album_image_orientation = album_image_orientation;

    R_SUCCEED();
}

Result ISelfController::IsSystemBufferSharingEnabled() {
    LOG_INFO(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    R_RETURN(m_applet->display_layer_manager.IsSystemBufferSharingEnabled());
}

Result ISelfController::GetSystemSharedBufferHandle(Out<u64> out_buffer_id) {
    LOG_INFO(Service_AM, "called");

    u64 layer_id;

    std::scoped_lock lk{m_applet->lock};
    R_RETURN(m_applet->display_layer_manager.GetSystemSharedLayerHandle(out_buffer_id, &layer_id));
}

Result ISelfController::GetSystemSharedLayerHandle(Out<u64> out_buffer_id, Out<u64> out_layer_id) {
    LOG_INFO(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    R_RETURN(
        m_applet->display_layer_manager.GetSystemSharedLayerHandle(out_buffer_id, out_layer_id));
}

Result ISelfController::CreateManagedDisplayLayer(Out<u64> out_layer_id) {
    LOG_INFO(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    R_RETURN(m_applet->display_layer_manager.CreateManagedDisplayLayer(out_layer_id));
}

Result ISelfController::CreateManagedDisplaySeparableLayer(Out<u64> out_layer_id,
                                                           Out<u64> out_recording_layer_id) {
    LOG_WARNING(Service_AM,
                "CreateManagedDisplaySeparableLayer unavailable: recording layer backend is not materialized");
    R_RETURN(ResultUnknown);
}

Result ISelfController::SetHandlesRequestToDisplay(bool enable) {
    // Preserve the guest policy in the existing Applet state (the native backport does likewise).
    LOG_DEBUG(Service_AM, "called, enable={}", enable);
    std::scoped_lock lk{m_applet->lock};
    m_applet->handles_request_to_display = enable;
    R_SUCCEED();
}

Result ISelfController::ApproveToDisplay() {
    LOG_DEBUG(Service_AM, "called");

    if (auto* window_system = system.GetAppletManager().GetWindowSystem()) {
        window_system->RequestAppletVisibilityState(*m_applet, true);
    } else {
        // The applet manager normally owns a WindowSystem for the whole AM lifetime. Keep the
        // guest-visible state coherent during teardown/startup without dereferencing a null owner.
        std::scoped_lock lk{m_applet->lock};
        m_applet->window_visible = true;
        LOG_WARNING(Service_AM, "ApproveToDisplay without an active WindowSystem");
    }

    R_SUCCEED();
}

Result ISelfController::SetMediaPlaybackState(bool state) {
    LOG_DEBUG(Service_AM, "called, state={}", state);
    std::scoped_lock lk{m_applet->lock};
    m_applet->media_playback_state = state;
    R_SUCCEED();
}

Result ISelfController::OverrideAutoSleepTimeAndDimmingTime(s32 a, s32 b, s32 c, s32 d) {
    LOG_WARNING(Service_AM, "(STUBBED) called, a={}, b={}, c={}, d={}", a, b, c, d);
    R_SUCCEED();
}

Result ISelfController::SetIdleTimeDetectionExtension(
    IdleTimeDetectionExtension idle_time_detection_extension) {
    LOG_DEBUG(Service_AM, "called, extension={}", idle_time_detection_extension);

    std::scoped_lock lk{m_applet->lock};
    m_applet->idle_time_detection_extension = idle_time_detection_extension;

    R_SUCCEED();
}

Result ISelfController::GetIdleTimeDetectionExtension(
    Out<IdleTimeDetectionExtension> out_idle_time_detection_extension) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    *out_idle_time_detection_extension = m_applet->idle_time_detection_extension;

    R_SUCCEED();
}

Result ISelfController::ReportUserIsActive() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ISelfController::SetAutoSleepDisabled(bool is_auto_sleep_disabled) {
    LOG_DEBUG(Service_AM, "called. is_auto_sleep_disabled={}", is_auto_sleep_disabled);

    // On the system itself, if the previous state of is_auto_sleep_disabled
    // differed from the current value passed in, it'd signify the internal
    // window manager to update (and also increment some statistics like update counts)
    //
    // It'd also indicate this change to an idle handling context.
    //
    // However, given we're emulating this behavior, most of this can be ignored
    // and it's sufficient to simply set the member variable for querying via
    // IsAutoSleepDisabled().

    std::scoped_lock lk{m_applet->lock};
    m_applet->auto_sleep_disabled = is_auto_sleep_disabled;

    R_SUCCEED();
}

Result ISelfController::IsAutoSleepDisabled(Out<bool> out_is_auto_sleep_disabled) {
    LOG_DEBUG(Service_AM, "called.");

    std::scoped_lock lk{m_applet->lock};
    *out_is_auto_sleep_disabled = m_applet->auto_sleep_disabled;

    R_SUCCEED();
}

Result ISelfController::IsIlluminanceAvailable(Out<bool> out_is_illuminance_available) {
    LOG_WARNING(Service_AM, "(stubbed)");
    *out_is_illuminance_available = false;
    R_SUCCEED();
}

Result ISelfController::SetInputDetectionPolicy(InputDetectionPolicy input_detection_policy) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ISelfController::GetAccumulatedSuspendedTickValue(
    Out<u64> out_accumulated_suspended_tick_value) {
    LOG_DEBUG(Service_AM, "called.");

    // This command returns the total number of system ticks since ISelfController creation
    // where the game was suspended. Since Yuzu doesn't implement game suspension, this command
    // can just always return 0 ticks.
    std::scoped_lock lk{m_applet->lock};
    *out_accumulated_suspended_tick_value = m_applet->suspended_ticks;

    R_SUCCEED();
}

Result ISelfController::GetAccumulatedSuspendedTickChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called.");

    *out_event = m_applet->accumulated_suspended_tick_changed_event.GetHandle();
    R_SUCCEED();
}

Result ISelfController::SetAlbumImageTakenNotificationEnabled(bool enabled) {
    LOG_WARNING(Service_AM, "(STUBBED) called. enabled={}", enabled);

    // This service call sets an internal flag whether a notification is shown when an image is
    // captured. Currently we do not support capturing images via the capture button, so this can be
    // stubbed for now.
    std::scoped_lock lk{m_applet->lock};
    m_applet->album_image_taken_notification_enabled = enabled;

    R_SUCCEED();
}

Result ISelfController::SaveCurrentScreenshot(Capture::AlbumReportOption album_report_option) {
    LOG_INFO(Service_AM, "called, report_option={}", album_report_option);

    const auto screenshot_service =
        system.ServiceManager().GetService<Service::Capture::IScreenShotApplicationService>(
            "caps:su");

    if (screenshot_service) {
        screenshot_service->CaptureAndSaveScreenshot(album_report_option);
    }

    R_SUCCEED();
}

Result ISelfController::SetRecordVolumeMuted(bool muted) {
    LOG_WARNING(Service_AM, "(STUBBED) called. muted={}", muted);

    std::scoped_lock lk{m_applet->lock};
    m_applet->record_volume_muted = muted;

    R_SUCCEED();
}

Result ISelfController::Unknown230(u32 in_val, Out<u16> out_val) {
    LOG_WARNING(Service_AM, "(STUBBED) called, in_val={}", in_val);

    *out_val = 0;

    R_SUCCEED();
}

Result ISelfController::Unknown240(u32 in_val) {
    LOG_WARNING(Service_AM, "(STUBBED) called, in_val={}", in_val);

    R_SUCCEED();
}

} // namespace Service::AM
