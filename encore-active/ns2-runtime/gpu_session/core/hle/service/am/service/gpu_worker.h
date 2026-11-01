// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Experimental Switch 2 worker IPC endpoint. Having a kernel session does
// not imply native 4-KiB VA mapping, NvMap registration or GPU submissions.
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include "core/hle/service/service.h"
#include "core/hle/service/am/service/gpu_worker_protocol.h"

namespace Kernel { class KProcess; class KPageGroup; }
namespace Service::Nvidia { class Module; }

namespace Service::AM {
struct GpuWorkerSharedState;
class IGpuWorker final : public ServiceFramework<IGpuWorker> {
public:
    IGpuWorker(Core::System& system, std::shared_ptr<Nvidia::Module> module,
               Kernel::KProcess* process, std::shared_ptr<GpuWorkerSharedState> shared);
    ~IGpuWorker() override;

    Result HandleSyncRequest(Kernel::KServerSession& session,
                             HLERequestContext& ctx) override;
private:
    void CreateGpuAccessibleMemory(HLERequestContext& ctx);
    void DestroyGpuAccessibleMemory(HLERequestContext& ctx);
    void NormalMapGpuMemory4K(HLERequestContext& ctx);
    void NormalModifyGpuMemory4K(HLERequestContext& ctx);
    void NormalUnmapGpuMemory4K(HLERequestContext& ctx);
    void CreateSparseRegion(HLERequestContext& ctx);
    void DestroySparseRegion(HLERequestContext& ctx);
    void ExecuteSparseMapRequests(HLERequestContext& ctx);
    Result RegisterMemory(u64 begin, u64 end, u32 flags, u32 device, u32& id);
    Result ReleaseMemory(u32 id, u32 notification);
    std::mutex m_mutex;
    std::shared_ptr<GpuWorkerSharedState> m_shared;
    std::optional<std::size_t> m_nv_session;
    std::vector<u32> m_memory;
    GpuWorkerProtocol::MappingLedger m_mappings;
    // Physical GPU-only backing is owned by the worker, not by guest handles.
    // It must outlive every GMMU mapping and every in-flight GPU write.
    struct DenseRegion {
        u64 address{};
        u64 size{};
        u64 alias{};
        u32 nvmap_handle{};
        std::unique_ptr<Kernel::KPageGroup> pages;
        // Weak ownership of the actual NVMap object detects remaining refs
        // after our destructive FreeHandle; it does not extend its lifetime.
        std::weak_ptr<void> nvmap_lifetime;
        // Once FreeHandle succeeds, retry only the CPU alias unmap.
        bool nvmap_released{};
        // VA release and physical retirement are independently retryable.
        bool gmmu_released{};
    };
    std::map<std::pair<u32, u64>, DenseRegion> m_dense_regions;
    u64 m_dense_bytes{};
    u64 m_alias_slot_hint{}; // Avoid retrying the same occupied alias range.
    // Admission limits live in gpu_memory_registration.h and have host fixtures.
    Result BackGpuOnlyRegion(u32 address_space, u64 address, u64 size);
    bool RetireGpuOnlyRegion(u32 address_space, u64 address);
    std::shared_ptr<Nvidia::Module> m_module;
    Kernel::KProcess* const m_process;
};
} // namespace Service::AM
