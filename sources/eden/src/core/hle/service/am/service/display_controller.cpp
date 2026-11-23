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
        {0, nullptr, "GetLastForegroundCaptureImage"},
        {1, D<&IDisplayController::UpdateLastForegroundCaptureImage>, "UpdateLastForegroundCaptureImage"},
        {2, nullptr, "GetLastApplicationCaptureImage"},
        {3, nullptr, "GetCallerAppletCaptureImage"},
        {4, D<&IDisplayController::UpdateCallerAppletCaptureImage>, "UpdateCallerAppletCaptureImage"},
        {5, D<&IDisplayController::GetLastForegroundCaptureImageEx>, "GetLastForegroundCaptureImageEx"},
        {6, nullptr, "GetLastApplicationCaptureImageEx"},
        {7, D<&IDisplayController::GetCallerAppletCaptureImageEx>, "GetCallerAppletCaptureImageEx"},
        {8, D<&IDisplayController::TakeScreenShotOfOwnLayer>, "TakeScreenShotOfOwnLayer"},
        {9, nullptr, "CopyBetweenCaptureBuffers"},
        {10, nullptr, "AcquireLastApplicationCaptureBuffer"},
        {11, D<&IDisplayController::ReleaseLastApplicationCaptureBuffer>, "ReleaseLastApplicationCaptureBuffer"},
        {12, nullptr, "AcquireLastForegroundCaptureBuffer"},
        {13, D<&IDisplayController::ReleaseLastForegroundCaptureBuffer>, "ReleaseLastForegroundCaptureBuffer"},
        {14, nullptr, "AcquireCallerAppletCaptureBuffer"},
        {15, D<&IDisplayController::ReleaseCallerAppletCaptureBuffer>, "ReleaseCallerAppletCaptureBuffer"},
        {16, nullptr, "AcquireLastApplicationCaptureBufferEx"},
        {17, nullptr, "AcquireLastForegroundCaptureBufferEx"},
        {18, nullptr, "AcquireCallerAppletCaptureBufferEx"},
        {20, D<&IDisplayController::ClearCaptureBuffer>, "ClearCaptureBuffer"},
        {21, nullptr, "ClearAppletTransitionBuffer"},
        {22, D<&IDisplayController::AcquireLastApplicationCaptureSharedBuffer>, "AcquireLastApplicationCaptureSharedBuffer"},
        {23, D<&IDisplayController::ReleaseLastApplicationCaptureSharedBuffer>, "ReleaseLastApplicationCaptureSharedBuffer"},
        {24, D<&IDisplayController::AcquireLastForegroundCaptureSharedBuffer>, "AcquireLastForegroundCaptureSharedBuffer"},
        {25, D<&IDisplayController::ReleaseLastForegroundCaptureSharedBuffer>, "ReleaseLastForegroundCaptureSharedBuffer"},
        {26, D<&IDisplayController::AcquireCallerAppletCaptureSharedBuffer>, "AcquireCallerAppletCaptureSharedBuffer"},
        {27, D<&IDisplayController::ReleaseCallerAppletCaptureSharedBuffer>, "ReleaseCallerAppletCaptureSharedBuffer"},
        {28, nullptr, "TakeScreenShotOfOwnLayerEx"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDisplayController::~IDisplayController() = default;

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
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_was_written = true;
    R_SUCCEED();
}

Result IDisplayController::GetCallerAppletCaptureImageEx(
    Out<bool> out_was_written, OutBuffer<BufferAttr_HipcMapAlias> out_image_data) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_was_written = true;
    R_SUCCEED();
}

Result IDisplayController::TakeScreenShotOfOwnLayer(bool unknown0, s32 fbshare_layer_index) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IDisplayController::ClearCaptureBuffer(bool unknown0, s32 fbshare_layer_index, u32 color) {
    LOG_DEBUG(Service_AM, "called, unknown0={} fbshare_layer_index={} color={:#x}", unknown0,
              fbshare_layer_index, color);
    R_RETURN(applet->display_layer_manager.ClearAppletCaptureBuffer(fbshare_layer_index, color));
}

Result IDisplayController::AcquireLastForegroundCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index, VI::CaptureKind::LastForeground));
}

Result IDisplayController::ReleaseLastApplicationCaptureBuffer() {
    LOG_DEBUG(Service_AM, "called; legacy capture acquisition has no persistent ownership");
    // The PS5 port snapshots capture data synchronously. Unlike Horizon's transfer-memory path,
    // Encore does not retain a legacy capture-buffer ownership token after Acquire returns.
    R_SUCCEED();
}

Result IDisplayController::ReleaseLastForegroundCaptureBuffer() {
    LOG_DEBUG(Service_AM, "called; legacy capture acquisition has no persistent ownership");
    R_SUCCEED();
}

Result IDisplayController::ReleaseCallerAppletCaptureBuffer() {
    LOG_DEBUG(Service_AM, "called; legacy capture acquisition has no persistent ownership");
    R_SUCCEED();
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
