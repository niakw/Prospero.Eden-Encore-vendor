// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

// The Switch 2-only interface returned by IApplicationProxy command 40.
// This exposes emulated guest preference state, not PS5 host clock control.
class IPerformanceFunctions final : public ServiceFramework<IPerformanceFunctions> {
public:
    explicit IPerformanceFunctions(Core::System& system_)
        : ServiceFramework{system_, "IPerformanceFunctions"} {
        static const FunctionInfo functions[] = {
            {0, D<&IPerformanceFunctions::IsCpuOverclockEnabled>, "IsCpuOverclockEnabled"},
            {1, D<&IPerformanceFunctions::SetCpuOverclockEnabled>, "SetCpuOverclockEnabled"},
        };
        RegisterHandlers(functions);
    }

private:
    Result IsCpuOverclockEnabled(Out<bool> out_enabled) {
        *out_enabled = m_requested_overclock;
        R_SUCCEED();
    }

    Result SetCpuOverclockEnabled(bool enabled) {
        m_requested_overclock = enabled;
        R_SUCCEED();
    }

    bool m_requested_overclock{};
};

} // namespace Service::AM
