// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/sender.h"
#include "core/hle/service/psc/ovln/sender_service.h"

namespace Service::PSC {

ISenderService::ISenderService(Core::System& system_,
                               std::shared_ptr<OverlayNotificationBroker> broker_)
    : ServiceFramework{system_, "ovln:snd"}, broker{broker_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISenderService::OpenSender>, "OpenSender"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISenderService::~ISenderService() = default;

Result ISenderService::OpenSender(Out<SharedPointer<ISender>> out_sender,
                                  SourceName source_name, QueueAttribute attribute) {
    const auto name = source_name.GetString();
    LOG_DEBUG(Service_PSC, "called, source='{}', queue_length={}", name, attribute.queue_length);
    *out_sender = std::make_shared<ISender>(system, source_name, attribute, broker);
    R_SUCCEED();
}

} // namespace Service::PSC
