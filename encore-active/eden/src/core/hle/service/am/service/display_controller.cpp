// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/result.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/shared_buffer_manager.h"

namespace Service::AM {

IDisplayController::IDisplayController(Core::System& system_, std::shared_ptr<Applet> applet_)
    : ServiceFramework{system_, "IDisplayController"}, applet(std::move(applet_)) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IDisplayController::RejectLegacyCaptureImage>, "GetLastForegroundCaptureImage"},
        {1, D<&IDisplayController::UpdateLastForegroundCaptureImage>, "UpdateLastForegroundCaptureImage"},
        {2, D<&IDisplayController::RejectLegacyCaptureImage>, "GetLastApplicationCaptureImage"},
        {3, D<&IDisplayController::RejectLegacyCaptureImage>, "GetCallerAppletCaptureImage"},
        {4, D<&IDisplayController::UpdateCallerAppletCaptureImage>, "UpdateCallerAppletCaptureImage"},
        {5, D<&IDisplayController::GetLastForegroundCaptureImageEx>, "GetLastForegroundCaptureImageEx"},
        {6, D<&IDisplayController::GetLastApplicationCaptureImageEx>, "GetLastApplicationCaptureImageEx"},
        {7, D<&IDisplayController::GetCallerAppletCaptureImageEx>, "GetCallerAppletCaptureImageEx"},
        {8, D<&IDisplayController::TakeScreenShotOfOwnLayer>, "TakeScreenShotOfOwnLayer"},
        {9, D<&IDisplayController::CopyBetweenCaptureBuffers>, "CopyBetweenCaptureBuffers"},
        {10, D<&IDisplayController::RejectLegacyCaptureBufferAcquire>, "AcquireLastApplicationCaptureBuffer"},
        {11, D<&IDisplayController::ReleaseLastApplicationCaptureBuffer>, "ReleaseLastApplicationCaptureBuffer"},
        {12, D<&IDisplayController::RejectLegacyCaptureBufferAcquire>, "AcquireLastForegroundCaptureBuffer"},
        {13, D<&IDisplayController::ReleaseLastForegroundCaptureBuffer>, "ReleaseLastForegroundCaptureBuffer"},
        {14, D<&IDisplayController::RejectLegacyCaptureBufferAcquire>, "AcquireCallerAppletCaptureBuffer"},
        {15, D<&IDisplayController::ReleaseCallerAppletCaptureBuffer>, "ReleaseCallerAppletCaptureBuffer"},
        {16, D<&IDisplayController::RejectLegacyCaptureBufferAcquire>, "AcquireLastApplicationCaptureBufferEx"},
        {17, D<&IDisplayController::RejectLegacyCaptureBufferAcquire>, "AcquireLastForegroundCaptureBufferEx"},
        {18, D<&IDisplayController::RejectLegacyCaptureBufferAcquire>, "AcquireCallerAppletCaptureBufferEx"},
        {19, &IDisplayController::StubReplyZeroed, "Unknown19"},
        {20, D<&IDisplayController::ClearCaptureBuffer>, "ClearCaptureBuffer"},
        {21, D<&IDisplayController::RejectClearAppletTransitionBuffer>, "ClearAppletTransitionBuffer"},
        {22, D<&IDisplayController::AcquireLastApplicationCaptureSharedBuffer>, "AcquireLastApplicationCaptureSharedBuffer"},
        {23, D<&IDisplayController::ReleaseLastApplicationCaptureSharedBuffer>, "ReleaseLastApplicationCaptureSharedBuffer"},
        {24, D<&IDisplayController::AcquireLastForegroundCaptureSharedBuffer>, "AcquireLastForegroundCaptureSharedBuffer"},
        {25, D<&IDisplayController::ReleaseLastForegroundCaptureSharedBuffer>, "ReleaseLastForegroundCaptureSharedBuffer"},
        {26, D<&IDisplayController::AcquireCallerAppletCaptureSharedBuffer>, "AcquireCallerAppletCaptureSharedBuffer"},
        {27, D<&IDisplayController::ReleaseCallerAppletCaptureSharedBuffer>, "ReleaseCallerAppletCaptureSharedBuffer"},
        {28, D<&IDisplayController::TakeScreenShotOfOwnLayerEx>, "TakeScreenShotOfOwnLayerEx"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDisplayController::~IDisplayController() = default;

Result IDisplayController::RejectLegacyCaptureImage(
    OutBuffer<BufferAttr_HipcMapAlias> out_image_data) {
    LOG_DEBUG(Service_AM, "legacy capture-image command rejected, buffer_size={:#x}",
              out_image_data.size());

    // Horizon keeps cmds 0/2/3 as legacy stubs and returns an error. Do not alias them to the
    // newer cmd5/6/7 CaptureImageEx path, which has different observable semantics.
    R_THROW(ResultUnknown);
}

Result IDisplayController::UpdateLastForegroundCaptureImage() {
    LOG_DEBUG(Service_AM, "called");
    bool was_written{};
    s32 layer_index{};
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        &was_written, &layer_index, VI::CaptureKind::LastForeground));
}

Result IDisplayController::UpdateCallerAppletCaptureImage() {
    LOG_DEBUG(Service_AM, "called");
    bool was_written{};
    s32 layer_index{};
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        &was_written, &layer_index, VI::CaptureKind::CallerApplet));
}

Result IDisplayController::GetLastForegroundCaptureImageEx(
    Out<bool> out_was_written, OutBuffer<BufferAttr_HipcMapAlias> out_image_data) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.ReadAppletCaptureBuffer(
        out_was_written, out_image_data, VI::CaptureKind::LastForeground));
}

Result IDisplayController::GetLastApplicationCaptureImageEx(
    Out<bool> out_was_written, OutBuffer<BufferAttr_HipcMapAlias> out_image_data) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.ReadAppletCaptureBuffer(
        out_was_written, out_image_data, VI::CaptureKind::LastApplication));
}

Result IDisplayController::GetCallerAppletCaptureImageEx(
    Out<bool> out_was_written, OutBuffer<BufferAttr_HipcMapAlias> out_image_data) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.ReadAppletCaptureBuffer(
        out_was_written, out_image_data, VI::CaptureKind::CallerApplet));
}

Result IDisplayController::TakeScreenShotOfOwnLayer(bool screenshot_permitted,
                                                    s32 fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called, screenshot_permitted={} buffer={}", screenshot_permitted,
              fbshare_layer_index);
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBufferToSlot(
        fbshare_layer_index, screenshot_permitted));
}

Result IDisplayController::TakeScreenShotOfOwnLayerEx(
    bool screenshot_permitted, bool immediately, s32 fbshare_layer_index) {
    LOG_DEBUG(Service_AM,
              "called, screenshot_permitted={} immediately={} buffer={}",
              screenshot_permitted, immediately, fbshare_layer_index);

    // Horizon routes cmd28 through the same capture path as cmd8 and only adds the
    // immediate/deferred policy bit. Encore's PS5 capture backend is synchronous, so both
    // policies currently converge on the same real slot write.
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBufferToSlot(
        fbshare_layer_index, screenshot_permitted));
}

Result IDisplayController::CopyBetweenCaptureBuffers(s32 dst_fbshare_layer_index,
                                                     s32 src_fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called, dst={} src={}", dst_fbshare_layer_index,
              src_fbshare_layer_index);
    R_RETURN(applet->display_layer_manager.CopyAppletCaptureBuffer(
        dst_fbshare_layer_index, src_fbshare_layer_index));
}

Result IDisplayController::ClearCaptureBuffer(bool unknown0, s32 fbshare_layer_index, u32 color) {
    LOG_DEBUG(Service_AM, "called, unknown0={} fbshare_layer_index={} color={:#x}", unknown0,
              fbshare_layer_index, color);
    R_RETURN(applet->display_layer_manager.ClearAppletCaptureBuffer(fbshare_layer_index, color));
}

Result IDisplayController::RejectClearAppletTransitionBuffer(u32 color) {
    LOG_WARNING(Service_AM,
                "ClearAppletTransitionBuffer rejected: transition buffer backend unavailable, color={:#x}",
                color);
    R_THROW(ResultUnknown);
}

Result IDisplayController::AcquireLastForegroundCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index, VI::CaptureKind::LastForeground));
}

Result IDisplayController::RejectLegacyCaptureBufferAcquire() {
    LOG_DEBUG(Service_AM, "legacy capture-buffer acquisition rejected on modern Horizon contract");

    // SwitchBrew documents cmds 10/12/14 as firmware stubs. On newer system versions the
    // cmd16/17/18 TransferMemory variants also call an internal function that always errors.
    // HOS 23 therefore must not expose the old 0x384000 TransferMemory path.
    R_THROW(ResultUnknown);
}

Result IDisplayController::ReleaseLastApplicationCaptureBuffer() {
    LOG_DEBUG(Service_AM, "called; no legacy capture buffer is acquired on HOS 23");
    R_THROW(ResultBufferNotAcquired);
}

Result IDisplayController::ReleaseLastForegroundCaptureBuffer() {
    LOG_DEBUG(Service_AM, "called; no legacy capture buffer is acquired on HOS 23");
    R_THROW(ResultBufferNotAcquired);
}

Result IDisplayController::ReleaseCallerAppletCaptureBuffer() {
    LOG_DEBUG(Service_AM, "called; no legacy capture buffer is acquired on HOS 23");
    R_THROW(ResultBufferNotAcquired);
}

Result IDisplayController::ReleaseLastForegroundCaptureSharedBuffer() {
    LOG_DEBUG(Service_AM, "called; capture acquisition is synchronous and holds no persistent ownership");
    // Encore's capture path copies the requested snapshot under SharedBufferManager::m_guard and
    // releases that guard before Acquire* returns, so there is no persistent acquisition to release.
    R_SUCCEED();
}

Result IDisplayController::AcquireCallerAppletCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index, VI::CaptureKind::CallerApplet));
}

Result IDisplayController::ReleaseCallerAppletCaptureSharedBuffer() {
    LOG_DEBUG(Service_AM, "called; capture acquisition is synchronous and holds no persistent ownership");
    // Encore's capture path copies the requested snapshot under SharedBufferManager::m_guard and
    // releases that guard before Acquire* returns, so there is no persistent acquisition to release.
    R_SUCCEED();
}

Result IDisplayController::AcquireLastApplicationCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index, VI::CaptureKind::LastApplication));
}

Result IDisplayController::ReleaseLastApplicationCaptureSharedBuffer() {
    LOG_DEBUG(Service_AM, "called; capture acquisition is synchronous and holds no persistent ownership");
    // Encore's capture path copies the requested snapshot under SharedBufferManager::m_guard and
    // releases that guard before Acquire* returns, so there is no persistent acquisition to release.
    R_SUCCEED();
}

} // namespace Service::AM
