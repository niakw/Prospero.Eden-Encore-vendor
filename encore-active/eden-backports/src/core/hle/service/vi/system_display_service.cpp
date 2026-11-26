// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/system_display_service.h"
#include "core/hle/service/vi/vi_types.h"
#include "core/hle/service/vi/vi_results.h"

namespace Service::VI {

ISystemDisplayService::ISystemDisplayService(Core::System& system_,
                                             std::shared_ptr<Container> container)
    : ServiceFramework{system_, "ISystemDisplayService"}, m_container{std::move(container)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1200, C<&ISystemDisplayService::GetZOrderCountMin>, "GetZOrderCountMin"},
        {1202, C<&ISystemDisplayService::GetZOrderCountMax>, "GetZOrderCountMax"},
        {1203, C<&ISystemDisplayService::GetDisplayLogicalResolution>, "GetDisplayLogicalResolution"},
        {1204, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayMagnification"},
        {2201, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetLayerPosition"},
        {2203, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetLayerSize"},
        {2204, C<&ISystemDisplayService::GetLayerZ>, "GetLayerZ"},
        {2205, C<&ISystemDisplayService::SetLayerZ>, "SetLayerZ"},
        {2207, C<&ISystemDisplayService::SetLayerVisibility>, "SetLayerVisibility"},
        {2209, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetLayerAlpha"},
        {2210, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetLayerPositionAndSize"},
        {2312, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "CreateStrayLayer"},
        {2400, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "OpenIndirectLayer"},
        {2401, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "CloseIndirectLayer"},
        {2402, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "FlipIndirectLayer"},
        {3000, C<&ISystemDisplayService::ListDisplayModes>, "ListDisplayModes"},
        {3001, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "ListDisplayRgbRanges"},
        {3002, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "ListDisplayContentTypes"},
        {3200, C<&ISystemDisplayService::GetDisplayMode>, "GetDisplayMode"},
        {3201, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayMode"},
        {3202, &ISystemDisplayService::RejectDisplayUnderscan, "GetDisplayUnderscan"},
        {3203, &ISystemDisplayService::RejectDisplayUnderscan, "SetDisplayUnderscan"},
        {3204, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "GetDisplayContentType"},
        {3205, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayContentType"},
        {3206, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "GetDisplayRgbRange"},
        {3207, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayRgbRange"},
        {3208, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "GetDisplayCmuMode"},
        {3209, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayCmuMode"},
        {3210, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "GetDisplayContrastRatio"},
        {3211, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayContrastRatio"},
        {3214, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "GetDisplayGamma"},
        {3215, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayGamma"},
        {3216, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "GetDisplayCmuLuma"},
        {3217, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayCmuLuma"},
        {3218, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "SetDisplayCrcMode"},
        {6013, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "GetLayerPresentationSubmissionTimestamps"},
        {8225, C<&ISystemDisplayService::GetSharedBufferMemoryHandleId>, "GetSharedBufferMemoryHandleId"},
        {8250, C<&ISystemDisplayService::OpenSharedLayer>, "OpenSharedLayer"},
        {8251, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "CloseSharedLayer"},
        {8252, C<&ISystemDisplayService::ConnectSharedLayer>, "ConnectSharedLayer"},
        {8253, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "DisconnectSharedLayer"},
        {8254, C<&ISystemDisplayService::AcquireSharedFrameBuffer>, "AcquireSharedFrameBuffer"},
        {8255, C<&ISystemDisplayService::PresentSharedFrameBuffer>, "PresentSharedFrameBuffer"},
        {8256, C<&ISystemDisplayService::GetSharedFrameBufferAcquirableEvent>, "GetSharedFrameBufferAcquirableEvent"},
        {8257, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "FillSharedFrameBufferColor"},
        {8258, C<&ISystemDisplayService::CancelSharedFrameBuffer>, "CancelSharedFrameBuffer"},
        {9000, &ISystemDisplayService::RejectUnsupportedSystemDisplayCommand, "GetDp2hdmiController"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

ISystemDisplayService::~ISystemDisplayService() = default;

Result ISystemDisplayService::GetZOrderCountMin(Out<s64> out_min, u64 display_id) {
    LOG_DEBUG(Service_VI, "called, display_id={}", display_id);
    *out_min = 1;
    R_SUCCEED();
}

Result ISystemDisplayService::GetZOrderCountMax(Out<s64> out_max, u64 display_id) {
    LOG_DEBUG(Service_VI, "called, display_id={}", display_id);
    *out_max = 255;
    R_SUCCEED();
}

Result ISystemDisplayService::GetDisplayLogicalResolution(Out<s32> out_width, Out<s32> out_height,
                                                          u64 display_id) {
    LOG_DEBUG(Service_VI, "called, display_id={}", display_id);

    if (Settings::IsDockedMode()) {
        *out_width = static_cast<s32>(DisplayResolution::DockedWidth);
        *out_height = static_cast<s32>(DisplayResolution::DockedHeight);
    } else {
        *out_width = static_cast<s32>(DisplayResolution::UndockedWidth);
        *out_height = static_cast<s32>(DisplayResolution::UndockedHeight);
    }

    R_SUCCEED();
}

Result ISystemDisplayService::GetLayerZ(Out<u64> out_z_value, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. layer_id={}", layer_id);
    s32 z{};
    const auto res = m_container->GetLayerZIndex(layer_id, &z);
    R_TRY(res);
    *out_z_value = static_cast<u64>(z);
    R_SUCCEED();
}

Result ISystemDisplayService::SetLayerZ(u64 layer_id, u64 z_value) {
    LOG_DEBUG(Service_VI, "called. layer_id={}, z_value={}", layer_id, z_value);
    // Forward to container using internal API when available
    R_RETURN(m_container->SetLayerZIndex(layer_id, static_cast<s32>(z_value)));
}

// This function currently does nothing but return a success error code in
// the vi library itself, so do the same thing, but log out the passed in values.
Result ISystemDisplayService::SetLayerVisibility(bool visible, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called, layer_id={}, visible={}", layer_id, visible);
    R_RETURN(m_container->SetLayerVisibility(layer_id, visible));
}

Result ISystemDisplayService::ListDisplayModes(
    Out<u64> out_count, u64 display_id,
    OutArray<DisplayMode, BufferAttr_HipcMapAlias> out_display_modes) {
    LOG_WARNING(Service_VI, "(STUBBED) called, display_id={}", display_id);

    if (!out_display_modes.empty()) {
        const bool docked = Settings::IsDockedMode();
        out_display_modes[0] = {
            .width = static_cast<u32>(docked ? DisplayResolution::DockedWidth
                                             : DisplayResolution::UndockedWidth),
            .height = static_cast<u32>(docked ? DisplayResolution::DockedHeight
                                              : DisplayResolution::UndockedHeight),
            .refresh_rate = 60.f,
            .unknown = {},
        };
        *out_count = 1;
    } else {
        *out_count = 0;
    }

    R_SUCCEED();
}

Result ISystemDisplayService::GetDisplayMode(Out<DisplayMode> out_display_mode, u64 display_id) {
    LOG_WARNING(Service_VI, "(STUBBED) called, display_id={}", display_id);

    if (Settings::IsDockedMode()) {
        out_display_mode->width = static_cast<u32>(DisplayResolution::DockedWidth);
        out_display_mode->height = static_cast<u32>(DisplayResolution::DockedHeight);
    } else {
        out_display_mode->width = static_cast<u32>(DisplayResolution::UndockedWidth);
        out_display_mode->height = static_cast<u32>(DisplayResolution::UndockedHeight);
    }

    out_display_mode->refresh_rate = 60.f; // This wouldn't seem to be correct for 30 fps games.
    out_display_mode->unknown = 0;

    R_SUCCEED();
}

void ISystemDisplayService::RejectUnsupportedSystemDisplayCommand(HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "unsupported system-display command {}", ctx.GetCommand());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultNotSupported);
}

void ISystemDisplayService::RejectDisplayUnderscan(HLERequestContext& ctx) {
    LOG_DEBUG(Service_VI, "underscan is unsupported by retail display output");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultNotSupported);
}

Result ISystemDisplayService::GetSharedBufferMemoryHandleId(
    Out<s32> out_nvmap_handle, Out<u64> out_size,
    OutLargeData<SharedMemoryPoolLayout, BufferAttr_HipcMapAlias> out_pool_layout, u64 buffer_id,
    ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_VI, "called. buffer_id={}, aruid={:#x}", buffer_id, aruid.pid);

    R_RETURN(m_container->GetSharedBufferManager()->GetSharedBufferMemoryHandleId(
        out_size, out_nvmap_handle, out_pool_layout, buffer_id, aruid.pid));
}

Result ISystemDisplayService::OpenSharedLayer(u64 layer_id) {
    LOG_INFO(Service_VI, "(STUBBED) called. layer_id={}", layer_id);
    R_SUCCEED();
}

Result ISystemDisplayService::ConnectSharedLayer(u64 layer_id) {
    LOG_INFO(Service_VI, "(STUBBED) called. layer_id={}", layer_id);
    R_SUCCEED();
}

Result ISystemDisplayService::AcquireSharedFrameBuffer(Out<android::Fence> out_fence,
                                                       Out<std::array<s32, 4>> out_slots,
                                                       Out<s64> out_target_slot, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->AcquireSharedFrameBuffer(
        out_fence, *out_slots, out_target_slot, layer_id));
}

Result ISystemDisplayService::PresentSharedFrameBuffer(android::Fence fence,
                                                       Common::Rectangle<s32> crop_region,
                                                       u32 window_transform, s32 swap_interval,
                                                       u64 layer_id, s64 surface_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->PresentSharedFrameBuffer(
        fence, crop_region, window_transform, swap_interval, layer_id, surface_id));
}

Result ISystemDisplayService::GetSharedFrameBufferAcquirableEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->GetSharedFrameBufferAcquirableEvent(out_event,
                                                                                        layer_id));
}

Result ISystemDisplayService::CancelSharedFrameBuffer(u64 layer_id, s64 slot) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->CancelSharedFrameBuffer(layer_id, slot));
}

} // namespace Service::VI
