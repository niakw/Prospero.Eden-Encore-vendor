// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <utility>

#include "common/settings.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

// Switch 2 nn::am IApplicationProxy command 40. Keep the guest's request
// separate from actual host clock policy: changing global CPU clock settings
// while a guest is running would distort its time base. A state shared by the
// parent proxy and its children preserves the request across repeated getter
// calls without changing the pinned NS1 Applet layout/ABI.
struct Ns2PerformanceRequest {
    std::atomic<bool> cpu_overclock_requested{false};
};

class IPerformanceFunctions final : public ServiceFramework<IPerformanceFunctions> {
public:
    explicit IPerformanceFunctions(Core::System& system_,
                                   std::shared_ptr<Ns2PerformanceRequest> request)
        : ServiceFramework{system_, "IPerformanceFunctions"},
          m_request{std::move(request)} {
        static const FunctionInfo functions[] = {
            {0, D<&IPerformanceFunctions::IsCpuOverclockEnabled>, "IsCpuOverclockEnabled"},
            {1, D<&IPerformanceFunctions::SetCpuOverclockEnabled>, "SetCpuOverclockEnabled"},
        };
        RegisterHandlers(functions);
    }

private:
    Result IsCpuOverclockEnabled(Out<bool> out_enabled) {
        *out_enabled = m_request->cpu_overclock_requested.load(std::memory_order_relaxed) ||
                       Settings::values.cpu_clock.GetValue() != Settings::CpuClock::Normal;
        LOG_DEBUG(Service_AM, "called, enabled={}", *out_enabled);
        R_SUCCEED();
    }

    Result SetCpuOverclockEnabled(bool enabled) {
        LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
        m_request->cpu_overclock_requested.store(enabled, std::memory_order_relaxed);
        R_SUCCEED();
    }

    const std::shared_ptr<Ns2PerformanceRequest> m_request;
};

} // namespace Service::AM
