// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/receiver.h"

namespace Service::PSC {

IReceiver::IReceiver(Core::System& system_, std::shared_ptr<OverlayNotificationBroker> broker_)
    : ServiceFramework{system_, "IReceiver"}, service_context{system_, "IReceiver"},
      broker{std::move(broker_)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IReceiver::AddSource>, "AddSource"},
        {1, D<&IReceiver::RemoveSource>, "RemoveSource"},
        {2, D<&IReceiver::GetReceiveEventHandle>, "GetReceiveEventHandle"},
        {3, D<&IReceiver::Receive>, "Receive"},
        {4, D<&IReceiver::ReceiveWithTick>, "ReceiveWithTick"},
    };
    // clang-format on
    RegisterHandlers(functions);
    receive_event = service_context.CreateEvent("IReceiver::ReceiveEvent");
    receiver_id = broker->RegisterReceiver(receive_event);
}

IReceiver::~IReceiver() {
    broker->UnregisterReceiver(receiver_id);
    service_context.CloseEvent(receive_event);
}

Result IReceiver::AddSource(SourceName source_name) {
    const std::string name = source_name.GetString();
    LOG_INFO(Service_PSC, "called: source_name={}", name);
    R_RETURN(broker->AddSource(receiver_id, name));
}

Result IReceiver::RemoveSource(SourceName source_name) {
    const std::string name = source_name.GetString();
    LOG_INFO(Service_PSC, "called: source_name={}", name);
    R_RETURN(broker->RemoveSource(receiver_id, name));
}

Result IReceiver::GetReceiveEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_PSC, "called");
    *out_event = &receive_event->GetReadableEvent();
    R_SUCCEED();
}

Result IReceiver::Receive(Out<OverlayNotification> out_notification, Out<MessageFlags> out_flags) {
    u64 tick{};
    R_RETURN(ReceiveWithTick(out_notification, out_flags, Out<u64>(&tick)));
}

Result IReceiver::ReceiveWithTick(Out<OverlayNotification> out_notification,
                                  Out<MessageFlags> out_flags, Out<u64> out_tick) {
    LOG_DEBUG(Service_PSC, "called");
    R_RETURN(broker->Receive(receiver_id, out_notification.GetPointer(), out_flags.GetPointer(),
                             out_tick.GetPointer()));
}

} // namespace Service::PSC
