// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/psc/ovln/ovln_types.h"
#include "core/hle/service/service.h"

namespace Service::PSC {

class ISender final : public ServiceFramework<ISender> {
public:
    explicit ISender(Core::System& system_, SourceName source_name, QueueAttribute attribute);
    ~ISender() override;

private:
    Result Send(MessageFlags flags, const OverlayNotification& notification);
    Result GetUnreceivedMessageCount(Out<u32> out_count);

    std::mutex queue_mutex;
    std::vector<OverlayNotification> queue;
    std::string source_name;
    u32 queue_capacity{};
};

} // namespace Service::PSC
