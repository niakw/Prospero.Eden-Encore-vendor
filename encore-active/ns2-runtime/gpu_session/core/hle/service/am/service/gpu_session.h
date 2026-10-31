// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bounded research TIPC service. GPU VA FDs and notification events have
// real kernel/NVDRV lifetimes; worker submission remains unsupported.
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include "core/hle/service/service.h"
#include "core/hle/service/am/service/gpu_worker_shared.h"
#include "core/hle/service/am/service/gpu_session_protocol.h"

namespace Kernel { class KProcess; class KEvent; class KClientSession; }
namespace Service::Nvidia { class Module; }
namespace Service::AM {
class IGpuSession final : public ServiceFramework<IGpuSession> {
public:
    IGpuSession(Core::System& system, std::shared_ptr<Nvidia::Module> module,
                Kernel::KProcess* process);
    Result RegisterAddressSpace(u32 id);
    ~IGpuSession() override;
    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override;
private:
    void RequestWorker(HLERequestContext& ctx);
    void AcquireWorker(HLERequestContext& ctx);
    void GetNotificationRange(HLERequestContext& ctx);
    void GetNotificationEvent(HLERequestContext& ctx);
    void CreateAddressSpace(HLERequestContext& ctx);
    void GetAddressSpacePartition(HLERequestContext& ctx);
    void CreateGraphicsDevice(HLERequestContext& ctx);
    void DestroyGraphicsDevice(HLERequestContext& ctx);
    std::mutex m_lock;
    // Module must be initialized before constructing the shared FD owner.
    std::shared_ptr<Nvidia::Module> m_module;
    std::shared_ptr<GpuWorkerSharedState> m_shared;
    Kernel::KProcess* const m_process;
    Kernel::KClientSession* m_worker{};

};
} // namespace Service::AM
