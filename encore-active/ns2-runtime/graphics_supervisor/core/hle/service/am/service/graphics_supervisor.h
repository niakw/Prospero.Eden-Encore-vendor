// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Bounded research-only supervisor. The upstream NVDRV/VI2 implementation
// remains unported; command 497 yields metadata-only IGpuSession.
#include <memory>
#include <map>
#include <optional>
#include <mutex>
#include <vector>
#include "core/hle/service/service.h"
#include "core/hle/service/am/service/graphics_supervisor_bootstrap.h"
#include "core/hle/service/am/service/graphics_supervisor_protocol.h"
#include "core/hle/service/am/service/native_image_layout.h"
namespace Kernel { class KProcess; }
namespace Service::Nvidia { class Module; }
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
    void BindNativeGpuMemory(HLERequestContext& ctx);
    // Caller owns m_native_memory_mutex; shared by TIPC 114 and 423.
    Result BindNativeMemoryBacking(u32 id, u64 address, u64 size, u64 flags, u64 offset);
    void CreateNativeImage(HLERequestContext& ctx);
    void DestroyNativeImage(HLERequestContext& ctx);
    void CalculateNativeImageSize(HLERequestContext& ctx);
    void CalculateNativeImageRequirements(HLERequestContext& ctx);
    void RegisterGpuAddressSpace(HLERequestContext& ctx);
    Kernel::KProcess* const m_process;
    GraphicsSupervisorBootstrap::State m_bootstrap;
    std::vector<std::weak_ptr<IGpuSession>> m_gpu_sessions;
    std::mutex m_native_memory_mutex;
    GraphicsSupervisorProtocol::NativeMemoryLedger m_native_memory;
    std::shared_ptr<Nvidia::Module> m_native_module;
    std::optional<std::size_t> m_native_memory_session;
    NativeImageLedger m_native_images;
    // child NVMap handle -> pinned, internally duplicated parent NVMap handle
    std::map<u32, u32> m_native_parents;
};
} // namespace Service::AM
