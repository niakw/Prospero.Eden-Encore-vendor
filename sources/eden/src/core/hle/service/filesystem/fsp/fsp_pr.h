// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::FileSystem {

class FSP_PR final : public ServiceFramework<FSP_PR> {
public:
    explicit FSP_PR(Core::System& system_);
    ~FSP_PR() override;

private:
    void RejectProgramRegistryMutation(HLERequestContext& ctx);
    Result SetCurrentProcess(ClientProcessId process_id);
    Result SetEnabledProgramVerification(bool enabled);

    u64 current_process_id{};
    bool program_verification_enabled{true};
};

} // namespace Service::FileSystem
