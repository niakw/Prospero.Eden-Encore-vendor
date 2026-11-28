#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
pinned=(R/"sources/eden/src/video_core/memory_manager.h").read_text()
mmh=(R/"encore-active/eden/src/video_core/memory_manager_deferred.h").read_text()
native=(R/"encore-active/eden/src/video_core/memory_manager.h").read_text()
mmc=(R/"encore-active/eden/src/video_core/memory_manager.cpp").read_text()
ash=(R/"encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_as_gpu.h").read_text()
asc=(R/"encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
for s in ("void SetNativeSyncpointShim(GPUVAddr address);",
          "static constexpr u32 NumShimSyncpoints = 192;",
          "std::atomic<GPUVAddr> native_syncpoint_shim{0};"):
    assert s in mmh and s in native,s
for s in ("SetNativeSyncpointShim", "NumShimSyncpoints", "native_syncpoint_shim"):
    assert s not in pinned,s
assert 'bool EnqueueDeferredCPUWrite(u64 address, u64 size);' in mmh
assert '::Eden::GpuDeferredWrite::Queue deferred_cpu_write_queue;' in mmh
assert "EnableNativeTiling()" in native and "UsesNativeTiling() const" in native
assert "EnqueueDeferredCPUWrite" not in native
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
gpu=(R/"encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()
gpuh=(R/"encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_gpu.h").read_text()
for h in (mmh,native):
    assert "GPUVAddr GetNativeSyncpointShim() const noexcept" in h
    assert "native_syncpoint_shim.load(std::memory_order_acquire)" in h
assert "struct IoctlGetUserSyncpoint {" in gpuh
assert "static_assert(sizeof(IoctlGetUserSyncpoint) == 16);" in gpuh
assert "case 0x1e: { // Switch 2 NVGPU_IOCTL_CHANNEL_GET_USER_SYNCPOINT" in gpu
assert "command.raw != 0xC010481EU" in gpu
sync=gpu.split("NvResult nvhost_gpu::GetUserSyncpoint(IoctlGetUserSyncpoint& params, DeviceFD fd) {",1)[1].split("\n}",1)[0]
for token in ("UsesNativeAbi(fd)", "ValidateChannelOwner(fd, program_id)",
              "channel_state->memory_manager->GetNativeSyncpointShim()",
              "IsSyncpointAllocated(channel_syncpoint)",
              "GetGuestSyncpointValue(channel_syncpoint)",
              "params.gpu_va = shim + u64{channel_syncpoint} * sizeof(u32);",
              "params.syncpoint_id = channel_syncpoint",
              "params.syncpoint_max = current"):
    assert token in sync,token
assert "GetSyncpointFence(channel_syncpoint)" not in sync
assert sync.index("GetNativeSyncpointShim()") < sync.index("params.gpu_va = shim")
assert sync.index("GetGuestSyncpointValue(channel_syncpoint)") < sync.index("params.syncpoint_max = current")
print("NS2_SYNCPOINT_SHIM_SOURCE_PASS ioctl=0xB words=192 stride=4 physical_bytes=0 upstream=12125bc pinned_eden=immutable selected_header=native_tiling_production experimental_guest_bridge=separate")
