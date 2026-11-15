// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
namespace Service::AM {
class IGraphicsScalingParametersFunctions final
    : public ServiceFramework<IGraphicsScalingParametersFunctions> {
public:
    explicit IGraphicsScalingParametersFunctions(Core::System& system_)
        : ServiceFramework{system_, "nx2:IGraphicsScalingParametersFunctions"} {
        static const FunctionInfo functions[] = {
            {10, D<&IGraphicsScalingParametersFunctions::GetGraphicsScalingParameters>,
             "GetGraphicsScalingParameters"},
            {1050, D<&IGraphicsScalingParametersFunctions::SetGraphicsScaleChangedNotificationEnabled>,
             "SetGraphicsScaleChangedNotificationEnabled"},
        };
        RegisterHandlers(functions);
    }
private:
    Result GetGraphicsScalingParameters(Out<u32> out_scale, Out<u32> out_unknown1,
                                        Out<u32> out_unknown2) {
        *out_scale = 10;
        *out_unknown1 = 0;
        *out_unknown2 = 0;
        R_SUCCEED();
    }
    Result SetGraphicsScaleChangedNotificationEnabled(bool enabled) {
        LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
        R_SUCCEED();
    }
};
} // namespace Service::AM
