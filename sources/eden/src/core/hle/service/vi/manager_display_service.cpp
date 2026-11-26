// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_display_service.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

IManagerDisplayService::IManagerDisplayService(Core::System& system_,
                                               std::shared_ptr<Container> container)
    : ServiceFramework{system_, "IManagerDisplayService"}, m_container{std::move(container)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {200, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "AllocateProcessHeapBlock"},
        {201, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "FreeProcessHeapBlock"},
        {1102, C<&IManagerDisplayService::GetDisplayResolution>, "GetDisplayResolution"},
        {2010, C<&IManagerDisplayService::CreateManagedLayer>, "CreateManagedLayer"},
        {2011, C<&IManagerDisplayService::DestroyManagedLayer>, "DestroyManagedLayer"},
        {2012, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateStrayLayer"},
        {2050, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateIndirectLayer"},
        {2051, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DestroyIndirectLayer"},
        {2052, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateIndirectProducerEndPoint"},
        {2053, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DestroyIndirectProducerEndPoint"},
        {2054, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateIndirectConsumerEndPoint"},
        {2055, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DestroyIndirectConsumerEndPoint"},
        {2060, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateWatermarkCompositor"},
        {2062, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetWatermarkText"},
        {2063, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetWatermarkLayerStacks"},
        {2300, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "AcquireLayerTexturePresentingEvent"},
        {2301, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "ReleaseLayerTexturePresentingEvent"},
        {2302, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetDisplayHotplugEvent"},
        {2303, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetDisplayModeChangedEvent"},
        {2402, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetDisplayHotplugState"},
        {2501, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetCompositorErrorInfo"},
        {2601, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetDisplayErrorEvent"},
        {2701, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetDisplayFatalErrorEvent"},
        {4201, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetDisplayAlpha"},
        {4203, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetDisplayLayerStack"},
        {4205, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetDisplayPowerState"},
        {4206, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetDefaultDisplay"},
        {4207, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "ResetDisplayPanel"},
        {4208, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetDisplayFatalErrorEnabled"},
        {4209, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "IsDisplayPanelOn"},
        {4300, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetInternalPanelId"},
        {6000, C<&IManagerDisplayService::AddToLayerStack>, "AddToLayerStack"},
        {6001, C<&IManagerDisplayService::RemoveFromLayerStack>, "RemoveFromLayerStack"},
        {6002, C<&IManagerDisplayService::SetLayerVisibility>, "SetLayerVisibility"},
        {6003, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetLayerConfig"},
        {6004, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "AttachLayerPresentationTracer"},
        {6005, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DetachLayerPresentationTracer"},
        {6006, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "StartLayerPresentationRecording"},
        {6007, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "StopLayerPresentationRecording"},
        {6008, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "StartLayerPresentationFenceWait"},
        {6009, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "StopLayerPresentationFenceWait"},
        {6010, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetLayerPresentationAllFencesExpiredEvent"},
        {6011, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "EnableLayerAutoClearTransitionBuffer"},
        {6012, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DisableLayerAutoClearTransitionBuffer"},
        {6013, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetLayerOpacity"},
        {6014, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "AttachLayerWatermarkCompositor"},
        {6015, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DetachLayerWatermarkCompositor"},
        {7000, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetContentVisibility"},
        {8000, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetConductorLayer"},
        {8001, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetTimestampTracking"},
        {8100, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetIndirectProducerFlipOffset"},
        {8200, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateSharedBufferStaticStorage"},
        {8201, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateSharedBufferTransferMemory"},
        {8202, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DestroySharedBuffer"},
        {8203, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "BindSharedLowLevelLayerToManagedLayer"},
        {8204, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "BindSharedLowLevelLayerToIndirectLayer"},
        {8207, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "UnbindSharedLowLevelLayer"},
        {8208, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "ConnectSharedLowLevelLayerToSharedBuffer"},
        {8209, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DisconnectSharedLowLevelLayerFromSharedBuffer"},
        {8210, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateSharedLayer"},
        {8211, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "DestroySharedLayer"},
        {8216, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "AttachSharedLayerToLowLevelLayer"},
        {8217, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "ForceDetachSharedLayerFromLowLevelLayer"},
        {8218, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "StartDetachSharedLayerFromLowLevelLayer"},
        {8219, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "FinishDetachSharedLayerFromLowLevelLayer"},
        {8220, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetSharedLayerDetachReadyEvent"},
        {8221, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetSharedLowLevelLayerSynchronizedEvent"},
        {8222, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CheckSharedLowLevelLayerSynchronized"},
        {8223, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "RegisterSharedBufferImporterAruid"},
        {8224, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "UnregisterSharedBufferImporterAruid"},
        {8227, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CreateSharedBufferProcessHeap"},
        {8228, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetSharedLayerLayerStacks"},
        {8229, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetSharedLayerLayerStacks"},
        {8291, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "PresentDetachedSharedFrameBufferToLowLevelLayer"},
        {8292, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "FillDetachedSharedFrameBufferColor"},
        {8293, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetDetachedSharedFrameBufferImage"},
        {8294, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetDetachedSharedFrameBufferImage"},
        {8295, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "CopyDetachedSharedFrameBufferImage"},
        {8296, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "SetDetachedSharedFrameBufferSubImage"},
        {8297, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "GetSharedFrameBufferContentParameter"},
        {8298, &IManagerDisplayService::RejectUnsupportedManagerDisplayCommand, "ExpandStartupLogoOnSharedFrameBuffer"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IManagerDisplayService::~IManagerDisplayService() = default;

void IManagerDisplayService::RejectUnsupportedManagerDisplayCommand(HLERequestContext& ctx) {
    // These are HOS manager/display-control extensions for compositor, panel,
    // watermark, tracer and shared-framebuffer facilities that Encore does not
    // currently expose on PS5. Keep the complete HOS23 command table and fail
    // closed rather than leaving an unhandled IPC slot or fabricating success.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result IManagerDisplayService::GetDisplayResolution(Out<s64> out_width, Out<s64> out_height,
                                                    u64 display_id) {
    LOG_DEBUG(Service_VI, "called. display_id={}", display_id);
    *out_width = static_cast<s64>(DisplayResolution::UndockedWidth);
    *out_height = static_cast<s64>(DisplayResolution::UndockedHeight);
    R_SUCCEED();
}

Result IManagerDisplayService::CreateSharedLayerSession(Kernel::KProcess* owner_process,
                                                        u64* out_buffer_id, u64* out_layer_handle,
                                                        u64 display_id, bool enable_blending) {
    R_RETURN(m_container->GetSharedBufferManager()->CreateSession(
        owner_process, out_buffer_id, out_layer_handle, display_id, enable_blending));
}

void IManagerDisplayService::DestroySharedLayerSession(Kernel::KProcess* owner_process) {
    m_container->GetSharedBufferManager()->DestroySession(owner_process);
}

Result IManagerDisplayService::SetLayerBlending(bool enabled, u64 layer_id) {
    R_RETURN(m_container->SetLayerBlending(layer_id, enabled));
}

Result IManagerDisplayService::SetLayerZIndex(s32 z_index, u64 layer_id) {
    R_RETURN(m_container->SetLayerZIndex(layer_id, z_index));
}

Result IManagerDisplayService::CreateManagedLayer(Out<u64> out_layer_id, u32 flags, u64 display_id,
                                                  AppletResourceUserId aruid) {
    LOG_DEBUG(Service_VI, "called. flags={}, display={}, aruid={}", flags, display_id, aruid.pid);
    R_RETURN(m_container->CreateManagedLayer(out_layer_id, display_id, aruid.pid));
}

Result IManagerDisplayService::DestroyManagedLayer(u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. layer_id={}", layer_id);
    R_RETURN(m_container->DestroyManagedLayer(layer_id));
}

Result IManagerDisplayService::AddToLayerStack(u32 stack_id, u64 layer_id) {
    R_RETURN(m_container->UpdateLayerStack(layer_id, stack_id, true));
}

Result IManagerDisplayService::RemoveFromLayerStack(u32 stack_id, u64 layer_id) {
    R_RETURN(m_container->UpdateLayerStack(layer_id, stack_id, false));
}

Result IManagerDisplayService::SetLayerVisibility(bool visible, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called, layer_id={}, visible={}", layer_id, visible);
    R_RETURN(m_container->SetLayerVisibility(layer_id, visible));
}

} // namespace Service::VI
