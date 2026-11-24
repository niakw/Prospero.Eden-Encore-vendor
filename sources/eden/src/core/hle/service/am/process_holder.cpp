// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_process.h"
#include "core/hle/service/am/process_holder.h"
#include "core/hle/service/am/applet.h"
#include "common/assert.h"
#include <utility>

namespace Service::AM {

ProcessHolder::ProcessHolder(Kernel::KernelCore& kernel, std::shared_ptr<Applet> applet,
                             Kernel::KProcess* process)
    : MultiWaitHolder(process), m_kernel(kernel), m_applet(std::move(applet)), m_process(process) {
    // The applet wrapper may be reinitialized while this wait is pending.
    [[maybe_unused]] const bool opened = m_process->Open(m_kernel);
    ASSERT(opened);
}

ProcessHolder::~ProcessHolder() {
    UnlinkFromMultiWait();
    m_process->Close(m_kernel);
}

} // namespace Service::AM
