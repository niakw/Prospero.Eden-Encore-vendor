#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
shared=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker_shared.h").read_text()
worker=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.cpp").read_text()
session=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_session.cpp").read_text()
proto=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/gpu_session_protocol.h").read_text()
for x in ("struct IdentityGpuMapping","identity_gpu_mappings","AddSpace","RemoveSpace"):
    assert x in shared,x
assert "Status Remove(U32 id)" in proto
reg=worker[worker.index("Result IGpuWorker::RegisterMemory"):worker.index("Result IGpuWorker::ReleaseMemory")]
for x in (
 "IdentityGpuMapping{id, begin, size",
 "as->MapNative4K(id, begin, 0, size, 0)",
 "identity.AddSpace(*owned)",
 "identity_gpu_mappings.pop_back()",
): assert x in reg,x
release=worker[worker.index("Result IGpuWorker::ReleaseMemory"):worker.index("void IGpuWorker::CreateGpuAccessibleMemory")]
for x in (
 "mapping.memory == id",
 "system.GPU().WaitForIdle()",
 "m_mappings.RecordUnmapped(unmap)",
 "identity_it->RemoveSpace(as_id)",
 "identity_gpu_mappings.erase(identity_it)",
 "handle->pins == 0",
): assert x in release,x
assert release.index("identity_gpu_mappings.erase(identity_it)") < release.index("handle->pins == 0")
create=session[session.index("void IGpuSession::CreateAddressSpace"):session.index("void IGpuSession::GetUserPartition")]
for x in (
 "for (auto& identity : m_shared->identity_gpu_mappings)",
 "as->MapNative4K(identity.handle, identity.address",
 "identity.AddSpace(candidate)",
 "m_shared->address_spaces.Remove(candidate)",
): assert x in create,x
# A successful MapNative4K must never escape without a corresponding
# AddSpace entry. Reject registry saturation and reverse the just-mapped VA.
create_replay = create[create.index("for (auto& identity : m_shared->identity_gpu_mappings)"):
                       create.index("if (replay == Nvidia::NvResult::Success)")]
assert "if (!identity.AddSpace(candidate))" in create_replay
assert "replay = Nvidia::NvResult::InvalidState;" in create_replay
assert create_replay.index("as->UnmapNative4K(identity.address, identity.size)") < create_replay.index(
    "replay = Nvidia::NvResult::InvalidState;")
# OOM after try_emplace/slot allocation must unwind all published owners
# before closing the FD. Partial NS2 GPU setup must remain retryable.
oom = create[create.index("} catch (const std::bad_alloc&) {", create.index("const u32 candidate")):
             create.index("if (result.IsError()) m_module->Close(fd);")]
for needle in ("identity.HasSpace(candidate)", "as->UnmapNative4K(identity.address, identity.size)",
               "identity.RemoveSpace(candidate)", "m_shared->address_spaces.Remove(candidate)",
               "m_shared->opened_gpu_as.erase(candidate)",
               "result = Kernel::ResultOutOfResource;"):
    assert needle in oom, needle
reg_replay = reg[reg.index("for (const auto owned : m_shared->address_spaces.OwnedIds())"):
                 reg.index("if (identity_result.IsError())")]
assert "catch (const std::bad_alloc&)" in reg_replay
assert "if (!identity.AddSpace(*owned))" in reg_replay
assert reg_replay.index("as->UnmapNative4K(begin, size)") < reg_replay.index(
    "identity_result = Kernel::ResultInvalidState;", reg_replay.index("if (!identity.AddSpace(*owned))"))

# Native process admission is a prerequisite for real NVDRV channel
# tickets; never publish a successful GPU session after registration fails.
shared_cpp=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker_shared.cpp").read_text()
assert "native_registered = Nvidia::Devices::NativeGpuProcessRegistry::Instance().Register(process);" in shared_cpp
assert "if (!native_registered)\n        throw std::bad_alloc{};" in shared_cpp
# The GPU 497 reference must be dropped only AFTER its last GPU address
# space and NvCore session have been closed. The separate 1402 owner can
# already have closed without invalidating this process generation.
shared_destroy=shared_cpp.split("GpuWorkerSharedState::~GpuWorkerSharedState() {",1)[1].split(
    "\n}",1)[0]
assert shared_destroy.index("module->Close(static_cast<Nvidia::DeviceFD>(*fd));") < shared_destroy.index(
    "module->GetContainer().CloseSession(Nvidia::NvCore::SessionId{*nv_session});") < shared_destroy.index(
    "NativeGpuProcessRegistry::Instance().Unregister(native_process);")
gsv=(R/"encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.cpp").read_text()
open_gpu=gsv.split("void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {", 1)[1].split("\n}", 1)[0]
assert "std::make_shared<IGpuSession>(system, module, m_process)" in open_gpu
assert "catch (const std::bad_alloc&)" in open_gpu
assert "result = Kernel::ResultOutOfResource;" in open_gpu

# An AS FD can close while its scheduler channel still has queued work.
# Keep a strong AS/GMMU lease in ChannelState through GPU payload teardown.
# The generic Maxwell channel leaves this lease empty; there is no
# additional GPU mapping, synthetic VA or per-game admission.
channel_h=(R/"sources/eden/src/video_core/control/channel_state.h").read_text()
as_h=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.h").read_text()
as_cpp=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
assert "public std::enable_shared_from_this<nvhost_as_gpu>" in as_h
assert channel_h.index("std::shared_ptr<void> address_space_owner{};") < channel_h.index(
    "std::optional<Payload> payload;")
as_bind=as_cpp.split("NvResult nvhost_as_gpu::BindChannel(IoctlBindChannel& params) {",1)[1].split(
    "\n}",1)[0]
assert as_bind.index("const std::scoped_lock lock{mutex, gpu_channel_device->channel_mutex};") < as_bind.index(
    "auto owner = weak_from_this().lock();")
assert as_bind.index("session->process != m_native_process") < as_bind.index(
    "NativeGpuProcessRegistry::Instance().Contains(m_native_process)") < as_bind.index(
    "channel.address_space_owner = std::move(owner);") < as_bind.index(
    "channel.memory_manager = gmmu.get();")
assert "#include \"core/hle/service/nvdrv/devices/native_gpu_process_registry.h\"" in as_cpp
assert as_bind.index("if (NativeGpuProcessRegistry::Instance().Contains(m_native_process)) {") < as_bind.index(
    "container.RetainSession(*m_nv_session, m_native_process)")
assert "if (!owner) return NvResult::InvalidState;" in as_bind

# The AS owner alone cannot prevent NvCore::CloseSession from unmapping
# every NVMap handle. The actual PS5-selected Container must hold a checked
# reference to the same live session until the last bound channel retires.
nvcore_h=(R/"sources/eden/src/core/hle/service/nvdrv/core/container.h").read_text()
nvcore_cpp=(R/"encore-active/eden-core/src/core/hle/service/nvdrv/core/container.cpp").read_text()
assert "bool RetainSession(SessionId id, const Kernel::KProcess* process);" in nvcore_h
retain=nvcore_cpp.split("bool Container::RetainSession(SessionId id, const Kernel::KProcess* process) {",1)[1].split(
    "\n}",1)[0]
for token in ("session_guard", "impl->sessions.size()", "session.is_active",
              "session.process != process", "session.ref_count <= 0",
              "std::numeric_limits<s32>::max()", "++session.ref_count;"):
    assert token in retain,token
assert "std::optional<NvCore::SessionId> m_nv_session{};" in as_h
assert "bool m_channel_session_retained{};" in as_h
assert "m_nv_session = session_id;" in as_cpp
assert "container.CloseSession(*m_nv_session);" in as_cpp
assert as_cpp.index("container.CloseSession(*m_nv_session);") > as_cpp.index(
    "native_sparse_pins.clear();")
assert as_bind.index("container.RetainSession(*m_nv_session, m_native_process)") < as_bind.index(
    "channel.address_space_owner = std::move(owner);")
# A guest can close the native channel while its scheduler still owns a
# parked semaphore. Retire on the GPU worker *after* preceding command
# batches, remove the pending epoch, release rasterizer channel caches,
# then let the channel drop its AS/session lease. Never run this from the
# global shutdown, which has already destroyed GPU before NVDRV.
scheduler_h=(R/"encore-active/ns2-runtime/gpu_dispatch/video_core/control/scheduler.h").read_text()
scheduler_cpp=(R/"encore-active/ns2-runtime/gpu_dispatch/video_core/control/scheduler.cpp").read_text()
gpu_cpp=(R/"encore-active/eden-core/src/video_core/gpu_vulkan.cpp").read_text()
nvhost_cpp=(R/"encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()
assert "void RetireChannel(const ChannelState* old_channel);" in scheduler_h
retire=scheduler_cpp.split("void Scheduler::RetireChannel(const ChannelState* old_channel) {",1)[1].split(
    "\n}",1)[0]
for needle in ("std::unique_lock epoch_lock(scheduling_guard);",
               "std::scoped_lock gpu_lock(gpu_submit_guard);",
               "std::scoped_lock pending_lock(pending_guard);",
               "it->second.get() != old_channel",
               "std::erase(pending_native_channels, it->second);",
               "channels.erase(it);"):
    assert needle in retire,needle
release=gpu_cpp.split("void ReleaseChannel(Control::ChannelState& to_release) {",1)[1].split("\n    }",1)[0]
assert "#if defined(PS5_NATIVE)" in release and "#else" in release
assert release.index("RequestSyncOperation([this, channel_id, expected]") < release.index(
    "gpu_thread.TickGPU(is_async);") < release.index("WaitForSyncOperation(fence);")
assert release.index("const auto it = channels.find(channel_id);") < release.index(
    "it->second.get() != expected") < release.index("const auto owner = it->second;")
assert release.index("scheduler.RetireChannel(owner.get());") < release.index(
    "renderer->ReadRasterizer()->ReleaseChannel(owner->bind_id);") < release.index(
    "channels.erase(it);")
assert release.index("channels.find(channel_id);") > release.index(
    "RequestSyncOperation([this, channel_id, expected]")
dtor=nvhost_cpp.split("nvhost_gpu::~nvhost_gpu() {",1)[1].split("\n}",1)[0]
for needle in ("channel_state->address_space_owner",
               "system.IsPoweredOn()", "!system.IsShuttingDown()",
               "system.GPU().ReleaseChannel(*channel_state)"):
    assert needle in dtor,needle
assert dtor.index("system.GPU().ReleaseChannel(*channel_state)") < dtor.index(
    "NativeGpuChannelRegistry::Instance().Unbind(channel_state.get())")

# The actual core shutdown retires GPU before kernel.CloseServices. Native
# worker destruction may occur inside that CloseServices call: never use the
# stopped GPU optional, while ordinary guest FD close still drains the queue.
worker_destroy=worker.split("IGpuWorker::~IGpuWorker() {",1)[1].split("\n}",1)[0]
core_shutdown=(R/"encore-active/ns2-runtime/process/core/core.cpp").read_text().split(
    "void ShutdownMainProcess() {",1)[1].split("\n    bool IsShuttingDown()",1)[0]
assert core_shutdown.index("gpu_core.reset();") < core_shutdown.index("kernel.CloseServices();")
assert "system.IsPoweredOn() && !system.IsShuttingDown()" in worker_destroy
assert worker_destroy.index("system.IsPoweredOn() && !system.IsShuttingDown()") < worker_destroy.index(
    "m_mappings.Mappings().empty()") < worker_destroy.index("system.GPU().WaitForIdle();")
assert worker_destroy.index("system.GPU().WaitForIdle();") < worker_destroy.index(
    "for (const auto& [key, mapping] : m_mappings.Mappings())")


# Switch 2 channel preemption-mode ioctl (SwitchBrew 0xC008481F) is
# implemented only for WFI/WFI: the existing GPU worker schedules at
# command boundaries, not mid-shader. Unsupported preemption must fail
# closed, and an unrelated NS1 or foreign FD must not change ABI.
preempt=nvhost_cpp.split("case 0x1f: { // Switch 2 NVGPU_IOCTL_CHANNEL_SET_PREEMPTION_MODE",1)[1].split(
    "case 0x20: // Switch 2 NVGPU_IOCTL_CHANNEL_OPEN",1)[0]
for token in ("command.raw != 0xC008481FU", "input.size() != 8 || output.size() != 8",
              "const std::scoped_lock lock{channel_mutex};", "!UsesNativeAbi(fd)",
              "ValidateChannelOwner(fd, program_id)", "std::array<u32_le, 2> modes{}", "constexpr u32 NativeWfiBit = 1u;",
              "modes[0]) & ~NativeWfiBit", "return NvResult::NotImplemented;",
              "std::memcpy(output.data(), modes.data(), sizeof(modes));"):
    assert token in preempt,token
assert preempt.index("ValidateChannelOwner(fd, program_id)") < preempt.index(
    "modes[0]) & ~NativeWfiBit") < preempt.index(
    "std::memcpy(output.data(), modes.data(), sizeof(modes));")


# Linux nvgpu UAPI uses WFI bit0, not the zero-valued NV2080 enum.
# Permit default/no-change or an explicit WFI flag, not CTA/GFXP/CILP.
assert '#include <array>' in nvhost_cpp
assert "(static_cast<u32>(modes[1]) & ~NativeWfiBit) != 0" in preempt
assert preempt.index("constexpr u32 NativeWfiBit = 1u;") < preempt.index(
    "modes[0]) & ~NativeWfiBit") < preempt.index(
    "std::memcpy(output.data(), modes.data(), sizeof(modes));")


# Native NS2 sparse 4K ioctl allocates only guest GPU VA, never
# unvalidated physical backing. Fixed overlaps and OOM are rejected before
# publication; NS1 continues along the original small-sparse rejection.
alloc=as_cpp.split("NvResult nvhost_as_gpu::AllocateSpace(IoctlAllocSpace& params) {",1)[1].split(
    "\n}",1)[0]
native_sparse=alloc.split("const bool small_sparse =",1)[1].split(
    'UNIMPLEMENTED_MSG("Sparse small pages are not implemented!");',1)[0]
for needle in (
    "NativeGpuProcessRegistry::Instance().Contains(m_native_process)",
    "params.pages == 0",
    "allowed_flags",
    "allocator.GetVAStart()",
    "allocator.GetVALimit()",
    "size > limit_va - address",
    "allocation_map.lower_bound(address)",
    "mapping_map.lower_bound(address)",
    "allocator.AllocateFixed(first_page, params.pages)",
    "allocator.Allocate(params.pages)",
    "allocation_map.try_emplace(",
    "gmmu->MapSparse(address, size, false);",
    "catch (const std::bad_alloc&)",
    "allocation_map.erase(address)",
    "allocator.Free(first_page, params.pages)",
):
    assert needle in native_sparse, needle
assert native_sparse.index("allocation_map.try_emplace(") < native_sparse.index(
    "gmmu->MapSparse(address, size, false);")
assert native_sparse.index("gmmu->MapSparse(address, size, false);") < native_sparse.index(
    "params.offset = address;")
assert alloc.index("const bool small_sparse =") < alloc.index(
    'UNIMPLEMENTED_MSG("Sparse small pages are not implemented!");')

print("NS2_GPU_IDENTITY_MAPPING_SOURCE_PASS sub101=identity replay=new_as sub102=all_owned_unmap as_fd_close=lease_before_payload session=retained retire=gpu-worker shutdown=guarded")
