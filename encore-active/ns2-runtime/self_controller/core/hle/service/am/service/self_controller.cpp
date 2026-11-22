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
        {17, &ISelfController::StubReplyZeroed, "SetControllerFirmwareUpdateSection"},
        {18, &ISelfController::StubReplyZeroed, "SetRequiresCaptureButtonShortPressedMessage"},
        {19, D<&ISelfController::SetAlbumImageOrientation>, "SetAlbumImageOrientation"},
        {20, &ISelfController::StubReplyZeroed, "SetDesirableKeyboardLayout"},
        {21, &ISelfController::StubReplyZeroed, "GetScreenShotProgramId"},
        {40, D<&ISelfController::CreateManagedDisplayLayer>, "CreateManagedDisplayLayer"},
        {41, D<&ISelfController::IsSystemBufferSharingEnabled>, "IsSystemBufferSharingEnabled"},
        {42, D<&ISelfController::GetSystemSharedLayerHandle>, "GetSystemSharedLayerHandle"},
        {43, D<&ISelfController::GetSystemSharedBufferHandle>, "GetSystemSharedBufferHandle"},
        {44, D<&ISelfController::CreateManagedDisplaySeparableLayer>, "CreateManagedDisplaySeparableLayer"},
        {45, &ISelfController::StubReplyZeroed, "SetManagedDisplayLayerSeparationMode"},
        {46, &ISelfController::StubReplyZeroed, "SetRecordingLayerCompositionEnabled"},
        {50, D<&ISelfController::SetHandlesRequestToDisplay>, "SetHandlesRequestToDisplay"},
        {51, D<&ISelfController::ApproveToDisplay>, "ApproveToDisplay"},
        {60, D<&ISelfController::OverrideAutoSleepTimeAndDimmingTime>, "OverrideAutoSleepTimeAndDimmingTime"},
        {61, D<&ISelfController::SetMediaPlaybackState>, "SetMediaPlaybackState"},
        {62, D<&ISelfController::SetIdleTimeDetectionExtension>, "SetIdleTimeDetectionExtension"},
        {63, D<&ISelfController::GetIdleTimeDetectionExtension>, "GetIdleTimeDetectionExtension"},
        {64, &ISelfController::StubReplyZeroed, "SetInputDetectionSourceSet"},
        {65, D<&ISelfController::ReportUserIsActive>, "ReportUserIsActive"},
        {66, &ISelfController::StubReplyZeroed, "GetCurrentIlluminance"},
        {67, D<&ISelfController::IsIlluminanceAvailable>, "IsIlluminanceAvailable"},
        {68, D<&ISelfController::SetAutoSleepDisabled>, "SetAutoSleepDisabled"},
        {69, D<&ISelfController::IsAutoSleepDisabled>, "IsAutoSleepDisabled"},
        {70, &ISelfController::StubReplyZeroed, "ReportMultimediaError"},
        {71, &ISelfController::StubReplyZeroed, "GetCurrentIlluminanceEx"},
        {72, D<&ISelfController::SetInputDetectionPolicy>, "SetInputDetectionPolicy"},
        {80, &ISelfController::StubReplyZeroed, "SetWirelessPriorityMode"},
        {90, D<&ISelfController::GetAccumulatedSuspendedTickValue>, "GetAccumulatedSuspendedTickValue"},
        {91, D<&ISelfController::GetAccumulatedSuspendedTickChangedEvent>, "GetAccumulatedSuspendedTickChangedEvent"},
        {100, D<&ISelfController::SetAlbumImageTakenNotificationEnabled>, "SetAlbumImageTakenNotificationEnabled"},
        {110, &ISelfController::StubReplyZeroed, "SetApplicationAlbumUserData"},
        {120, D<&ISelfController::SaveCurrentScreenshot>, "SaveCurrentScreenshot"},
        {130, D<&ISelfController::SetRecordVolumeMuted>, "SetRecordVolumeMuted"},
        {230, D<&ISelfController::Unknown230>, "Unknown230"},
        {240, D<&ISelfController::Unknown240>, "Unknown240"},
        {1000, &ISelfController::StubReplyInterface, "GetDebugStorageChannel"},
        {2000, &ISelfController::GetGraphicsSupervisor, "GetGraphicsSupervisor"},
        {2400, &ISelfController::StubReplyZeroedNx2, "Unknown2400"},
        {2401, &ISelfController::StubReplyZeroedNx2, "Unknown2401"},
        {2402, &ISelfController::StubReplyZeroedNx2, "Unknown2402"},
        {22, &ISelfController::StubReplyZeroed, "Unknown22"},
        {23, &ISelfController::StubReplyZeroed, "Unknown23"},
        {24, &ISelfController::StubReplyZeroed, "Unknown24"},
        {73, &ISelfController::StubReplyZeroed, "Unknown73"},
        {200, &ISelfController::StubReplyZeroed, "Unknown200"},
        {210, &ISelfController::StubReplyZeroed, "Unknown210"},
        {211, &ISelfController::StubReplyZeroed, "Unknown211"},
        {220, &ISelfController::StubReplyZeroed, "Unknown220"},
        {221, &ISelfController::StubReplyZeroed, "Unknown221"},
        {2100, &ISelfController::StubReplyZeroed, "Unknown2100"},
        {2200, &ISelfController::StubReplyZeroed, "Unknown2200"},
        {2300, &ISelfController::StubReplyZeroed, "Unknown2300"},
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

// Lichii currently uses StubReplyZeroed for these Switch 2 commands.
// A success header alone can leave request words visible to guest callers
// expecting output. Fill the documented 32 placeholder words explicitly;
// never manufacture an event/session handle or change unrelated commands.
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
    LOG_WARNING(Service_AM, "(STUBBED) called");

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
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    R_RETURN(m_applet->display_layer_manager.CreateManagedDisplaySeparableLayer(
        out_layer_id, out_recording_layer_id));
}

Result ISelfController::SetHandlesRequestToDisplay(bool enable) {
    LOG_WARNING(Service_AM, "(STUBBED) called, enable={}", enable);
    R_SUCCEED();
}

Result ISelfController::ApproveToDisplay() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ISelfController::SetMediaPlaybackState(bool state) {
    LOG_WARNING(Service_AM, "(STUBBED) called, state={}", state);
    R_SUCCEED();
}

Result ISelfController::OverrideAutoSleepTimeAndDimmingTime(s32 a, s32 b, s32 c, s32 d) {
    LOG_WARNING(Service_AM, "(STUBBED) called, a={}, b={}, c={}, d={}", a, b, c, d);
    R_SUCCEED();
}

Result ISelfController::SetIdleTimeDetectionExtension(
    IdleTimeDetectionExtension idle_time_detection_extension) {
    LOG_DEBUG(Service_AM, "(STUBBED) called extension={}", idle_time_detection_extension);

    std::scoped_lock lk{m_applet->lock};
    m_applet->idle_time_detection_extension = idle_time_detection_extension;

    R_SUCCEED();
}

Result ISelfController::GetIdleTimeDetectionExtension(
    Out<IdleTimeDetectionExtension> out_idle_time_detection_extension) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

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
