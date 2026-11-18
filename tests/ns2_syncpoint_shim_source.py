#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
pinned=(R/"sources/eden/src/video_core/memory_manager.h").read_text()
mmh=(R/"encore-active/eden-core/guest-bridge/memory_manager.h").read_text()
mmc=(R/"encore-active/eden-core/src/video_core/memory_manager.cpp").read_text()
ash=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.h").read_text()
asc=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
for s in ("void SetNativeSyncpointShim(GPUVAddr address);",
          "static constexpr u32 NumShimSyncpoints = 192;",
          "std::atomic<GPUVAddr> native_syncpoint_shim{0};"):
    assert s in mmh,s
for s in ("SetNativeSyncpointShim", "NumShimSyncpoints", "native_syncpoint_shim"):
    assert s not in pinned,s
assert 'bool EnqueueDeferredCPUWrite(u64 address, u64 size);' in mmh
assert '::Eden::GpuDeferredWrite::Queue deferred_cpu_write_queue;' in mmh
for s in ("void MemoryManager::SetNativeSyncpointShim",
          "GetGuestSyncpointValue(",
          "native_syncpoint_shim.load(std::memory_order_acquire)",
          "shim_bytes"):
    assert s in mmc,s
for s in ("case 0xb:", "Nx2GetSyncpointShimInfo",
          "vm.va_range_end - VM::YUZU_PAGESIZE",
          "gmmu->SetNativeSyncpointShim(address)",
          "params.stride = sizeof(u32);",
          "params.count = Tegra::MemoryManager::NumShimSyncpoints;",
          "EDEN_NS2_SYNCPOINT_SHIM"):
    assert s in asc,s
assert "IoctlNx2SyncpointShimInfo" in ash
block=asc.split("NvResult nvhost_as_gpu::Nx2GetSyncpointShimInfo",1)[1].split(
    "NvResult nvhost_as_gpu::AllocAsEx",1)[0]
for forbidden in ("PinHandle(", "Allocate(", "MapNative4K(", "sceKernelAllocate", "512_MiB"):
    assert forbidden not in block,forbidden
print("NS2_SYNCPOINT_SHIM_SOURCE_PASS ioctl=0xB words=192 stride=4 physical_bytes=0 upstream=12125bc pinned_eden=immutable selected_header=guest_bridge")
