// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::OMM {

class IPolicyManagerSystem final : public ServiceFramework<IPolicyManagerSystem> {
public:
    explicit IPolicyManagerSystem(Core::System& system_);
    ~IPolicyManagerSystem() override;

private:
    void GetAutoPowerDownEvent(HLERequestContext& ctx);
    void IsAutoPowerDownRequested(HLERequestContext& ctx);
    void AcceptPolicyNoop(HLERequestContext& ctx);
    void RejectUnknownPolicyCommand(HLERequestContext& ctx);

    KernelHelpers::ServiceContext service_context{system, "IPolicyManagerSystem"};
    Kernel::KEvent* auto_power_down_event{};
};

} // namespace Service::OMM
