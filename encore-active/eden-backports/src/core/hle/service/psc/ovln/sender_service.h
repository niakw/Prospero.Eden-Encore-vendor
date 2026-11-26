// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/psc/ovln/ovln_types.h"
#include "core/hle/service/service.h"

namespace Service::PSC {

class ISender;
class OverlayNotificationBroker;

class ISenderService final : public ServiceFramework<ISenderService> {
public:
    explicit ISenderService(Core::System& system_,
                            std::shared_ptr<OverlayNotificationBroker> broker_);
    ~ISenderService() override;

private:
    Result OpenSender(Out<SharedPointer<ISender>> out_sender, SourceName source_name,
                      QueueAttribute attribute);

    std::shared_ptr<OverlayNotificationBroker> broker;
};

} // namespace Service::PSC
