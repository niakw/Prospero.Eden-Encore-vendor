// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/system_update_control.h"
#include "core/hle/service/ns/system_update_interface.h"

namespace Service::NS {

ISystemUpdateInterface::ISystemUpdateInterface(Core::System& system_)
    : ServiceFramework{system_, "ns:su"}, service_context{system_, "ns:su"},
      update_notification_event{service_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISystemUpdateInterface::GetBackgroundNetworkUpdateState>, "GetBackgroundNetworkUpdateState"},
        {1, D<&ISystemUpdateInterface::OpenSystemUpdateControl>, "OpenSystemUpdateControl"},
        {2, &ISystemUpdateInterface::NotifyExFatDriverRequired, "NotifyExFatDriverRequired"},
        {3, &ISystemUpdateInterface::ClearExFatDriverStatusForDebug, "ClearExFatDriverStatusForDebug"},
        {4, &ISystemUpdateInterface::RequestBackgroundNetworkUpdate, "RequestBackgroundNetworkUpdate"},
        {5, &ISystemUpdateInterface::NotifyBackgroundNetworkUpdate, "NotifyBackgroundNetworkUpdate"},
        {6, &ISystemUpdateInterface::NotifyExFatDriverDownloadedForDebug, "NotifyExFatDriverDownloadedForDebug"},
        {9, D<&ISystemUpdateInterface::GetSystemUpdateNotificationEventForContentDelivery>, "GetSystemUpdateNotificationEventForContentDelivery"},
        {10, &ISystemUpdateInterface::NotifySystemUpdateForContentDelivery, "NotifySystemUpdateForContentDelivery"},
        {11, &ISystemUpdateInterface::PrepareShutdown, "PrepareShutdown"},
        {12, nullptr, "Unknown12"},
        {13, nullptr, "Unknown13"},
        {14, nullptr, "Unknown14"},
        {15, nullptr, "Unknown15"},
        {16, &ISystemUpdateInterface::DestroySystemUpdateTask, "DestroySystemUpdateTask"},
        {17, &ISystemUpdateInterface::RejectUnavailableSystemUpdateTransfer, "RequestSendSystemUpdate"},
        {18, &ISystemUpdateInterface::RejectUnavailableSystemUpdateTransfer, "GetSendSystemUpdateProgress"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISystemUpdateInterface::~ISystemUpdateInterface() = default;

void ISystemUpdateInterface::RejectUnavailableSystemUpdateTransfer(HLERequestContext& ctx) {
    LOG_WARNING(Service_NS, "System-update transfer is unavailable in Encore");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result ISystemUpdateInterface::GetBackgroundNetworkUpdateState(
    Out<BackgroundNetworkUpdateState> out_background_network_update_state) {
    LOG_DEBUG(Service_NS, "called; background system-update transport is not modeled");
    *out_background_network_update_state = BackgroundNetworkUpdateState::None;
    R_SUCCEED();
}

Result ISystemUpdateInterface::OpenSystemUpdateControl(
    Out<SharedPointer<ISystemUpdateControl>> out_system_update_control) {
    LOG_DEBUG(Service_NS, "called");
    *out_system_update_control = std::make_shared<ISystemUpdateControl>(system);
    R_SUCCEED();
}

Result ISystemUpdateInterface::GetSystemUpdateNotificationEventForContentDelivery(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_NS, "called; content-delivery update producer is not modeled");
    *out_event = update_notification_event.GetHandle();
    R_SUCCEED();
}

void ISystemUpdateInterface::NotifyExFatDriverRequired(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "exFAT-driver requirement notification acknowledged; host driver is external");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ISystemUpdateInterface::ClearExFatDriverStatusForDebug(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "exFAT debug status clear acknowledged");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ISystemUpdateInterface::RequestBackgroundNetworkUpdate(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "background system update ignored; emulator does not update host firmware");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ISystemUpdateInterface::NotifyBackgroundNetworkUpdate(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "background-update notification acknowledged");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ISystemUpdateInterface::NotifyExFatDriverDownloadedForDebug(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "exFAT-driver downloaded debug notification acknowledged");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ISystemUpdateInterface::NotifySystemUpdateForContentDelivery(HLERequestContext& ctx) {
    update_notification_event.Signal(system.Kernel());
    LOG_DEBUG(Service_NS, "content-delivery system-update event signalled");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ISystemUpdateInterface::PrepareShutdown(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "system-update shutdown preparation acknowledged; no update task is active");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ISystemUpdateInterface::DestroySystemUpdateTask(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "destroy-system-update-task acknowledged; no host firmware task exists");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

} // namespace Service::NS
