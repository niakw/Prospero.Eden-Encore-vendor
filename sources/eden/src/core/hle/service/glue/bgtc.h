// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Glue {

class BGTC_T final : public ServiceFramework<BGTC_T> {
public:
    explicit BGTC_T(Core::System& system_);
    ~BGTC_T() override;

    void OpenTaskService(HLERequestContext& ctx);
};

class ITaskService final : public ServiceFramework<ITaskService> {
public:
    explicit ITaskService(Core::System& system_);
    ~ITaskService() override;

private:
    Result IsInHalfAwake(Out<bool> out_half_awake);
    Result IsInFullAwake(Out<bool> out_full_awake);
    Result GetOperationMode(Out<u32> out_operation_mode);
    Result WillDisconnectNetworkWhenEnteringSleep(Out<bool> out_disconnect);
    Result WillStayHalfAwakeInsteadSleep(Out<bool> out_half_awake);
};

class BGTC_SC final : public ServiceFramework<BGTC_SC> {
public:
    explicit BGTC_SC(Core::System& system_);
    ~BGTC_SC() override;
};

} // namespace Service::Glue
