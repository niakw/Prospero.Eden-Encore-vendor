// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/logging.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nvdrv/nvmemp.h"

namespace Service::Nvidia {

NVMEMP::NVMEMP(Core::System& system_) : ServiceFramework{system_, "nvmemp"} {
    static const FunctionInfo functions[] = {
        {0, &NVMEMP::RejectUnavailableMemoryProfiler, "Open"},
        {1, &NVMEMP::RejectUnavailableMemoryProfiler, "GetPid"},
    };
    RegisterHandlers(functions);
}

NVMEMP::~NVMEMP() = default;

void NVMEMP::RejectUnavailableMemoryProfiler(HLERequestContext& ctx) {
    // nvmemp is NVIDIA's optional memory-profiler service and is not
    // available on retail Switch units. Encore exposes the correct HOS
    // command names but fails closed instead of trapping on UNIMPLEMENTED.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

} // namespace Service::Nvidia
