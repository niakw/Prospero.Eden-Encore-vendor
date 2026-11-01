// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Bounded research-only supervisor. The upstream NVDRV/VI2 implementation
// remains unported; command 497 yields metadata-only IGpuSession.
#include <memory>
#include <mutex>
#include <vector>
#include "core/hle/service/service.h"
#include "core/hle/service/am/service/graphics_supervisor_bootstrap.h"
#include "core/hle/service/am/service/graphics_supervisor_protocol.h"
namespace Kernel { class KProcess; }
namespace Service::AM {
class IGpuSession;
class IGraphicsSupervisor final : public ServiceFramework<IGraphicsSupervisor> {
public:
    IGraphicsSupervisor(Core::System& system, Kernel::KProcess* process, u64 aruid);
    ~IGraphicsSupervisor() override;
    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override;
private:
    void Initialize(HLERequestContext& ctx);
    void BindProcess(HLERequestContext& ctx);
    void CompleteBootstrap(HLERequestContext& ctx);
    void InitializeGpuContext(HLERequestContext& ctx);
    void OpenGpuSession(HLERequestContext& ctx);
    void CreateNativeMemory(HLERequestContext& ctx);
    void DestroyNativeMemory(HLERequestContext& ctx);
    void BindNativeMemory(HLERequestContext& ctx);
    void CalculateNativeImageSize(HLERequestContext& ctx);
    void CalculateNativeImageRequirements(HLERequestContext& ctx);
    void RegisterGpuAddressSpace(HLERequestContext& ctx);
    Kernel::KProcess* const m_process;
    GraphicsSupervisorBootstrap::State m_bootstrap;
    std::vector<std::weak_ptr<IGpuSession>> m_gpu_sessions;
    std::mutex m_native_memory_mutex;
    GraphicsSupervisorProtocol::NativeMemoryLedger m_native_memory;
};
} // namespace Service::AM
