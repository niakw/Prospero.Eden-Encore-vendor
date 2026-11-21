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
gsv=(R/"encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.cpp").read_text()
open_gpu=gsv.split("void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {", 1)[1].split("\n}", 1)[0]
assert "std::make_shared<IGpuSession>(system, module, m_process)" in open_gpu
assert "catch (const std::bad_alloc&)" in open_gpu
assert "result = Kernel::ResultOutOfResource;" in open_gpu

print("NS2_GPU_IDENTITY_MAPPING_SOURCE_PASS sub101=identity replay=new_as sub102=all_owned_unmap")


# The shared PS5 scheduler/NVDRV owner also serves Maxwell/NS1. Closing the
# guest GPU FD must retire the exact ChannelState instead of leaving its
# DmaPusher/engine payload reachable until process-wide GPU destruction.
scheduler_h=(R/"encore-active/ns2-runtime/gpu_dispatch/video_core/control/scheduler.h").read_text()
scheduler_cpp=(R/"encore-active/ns2-runtime/gpu_dispatch/video_core/control/scheduler.cpp").read_text()
gpu_cpp=(R/"encore-active/eden-core/src/video_core/gpu_vulkan.cpp").read_text()
nvhost_cpp=(R/"encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()
assert "void RetireChannel(const ChannelState* old_channel);" in scheduler_h
retire=scheduler_cpp.split("void Scheduler::RetireChannel(const ChannelState* old_channel) {",1)[1].split(
    "\n}\n",1)[0]
for needle in ("std::unique_lock epoch_lock(scheduling_guard);",
               "std::scoped_lock gpu_lock(gpu_submit_guard);",
               "std::scoped_lock pending_lock(pending_guard);",
               "it->second.get() != old_channel",
               "std::erase(pending_native_channels, it->second);",
               "channels.erase(it);"):
    assert needle in retire,needle

release=gpu_cpp.split("void ReleaseChannel(Control::ChannelState& to_release) {",1)[1].split(
    "\n    /// Binds a renderer to the GPU.",1)[0]
for needle in ("const s32 channel_id = to_release.bind_id;",
               "const auto* expected = &to_release;",
               "RequestSyncOperation([this, channel_id, expected]",
               "scheduler.RetireChannel(owner.get());",
               "renderer->ReadRasterizer()->ReleaseChannel(owner->bind_id);",
               "channels.erase(it);",
               "gpu_thread.TickGPU(is_async);",
               "WaitForSyncOperation(fence);"):
    assert needle in release,needle
assert release.index("scheduler.RetireChannel(owner.get());") < release.index(
    "renderer->ReadRasterizer()->ReleaseChannel(owner->bind_id);") < release.index(
    "channels.erase(it);")
assert release.index("gpu_thread.TickGPU(is_async);") < release.index(
    "WaitForSyncOperation(fence);")

on_close=nvhost_cpp.split("void nvhost_gpu::OnClose(DeviceFD fd) {",1)[1].split("\n}\n",1)[0]
for needle in ("sessions.erase(fd);",
               "sessions.empty() && channel_state",
               "system.IsPoweredOn() && !system.IsShuttingDown()",
               "system.GPU().ReleaseChannel(*channel_state);"):
    assert needle in on_close,needle
assert on_close.index("sessions.erase(fd);") < on_close.index(
    "sessions.empty() && channel_state") < on_close.index(
    "system.GPU().ReleaseChannel(*channel_state);")

dtor=nvhost_cpp.split("nvhost_gpu::~nvhost_gpu() {",1)[1].split("\n}\n",1)[0]
for needle in ("!sessions.empty() && channel_state",
               "system.IsPoweredOn() && !system.IsShuttingDown()",
               "system.GPU().ReleaseChannel(*channel_state);",
               "NativeGpuChannelRegistry::Instance().Unbind(channel_state.get());",
               "syncpoint_manager.FreeSyncpoint(channel_syncpoint);"):
    assert needle in dtor,needle
assert "channel_state->address_space_owner" not in dtor
assert dtor.index("system.GPU().ReleaseChannel(*channel_state);") < dtor.index(
    "NativeGpuChannelRegistry::Instance().Unbind(channel_state.get())") < dtor.index(
    "syncpoint_manager.FreeSyncpoint(channel_syncpoint);")
print("NS1_MAXWELL_CHANNEL_RETIRE_SOURCE_PASS exact_epoch=1 worker_order=1 "
      "pending_wait_removed=1 rasterizer_release=1 final_fd_close=1 "
      "destructor_fallback=1 shutdown_guard=1")


# Maxwell/NS1 uses the same AS BindChannel owner. A raw MemoryManager pointer
# cannot outlive an early AS-FD close, so retain the exact AS object and the
# same existing NvCore session until ChannelState payload teardown completes.
as_h=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.h").read_text()
as_cpp=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
channel_h=(R/"sources/eden/src/video_core/control/channel_state.h").read_text()
container_h=(R/"sources/eden/src/core/hle/service/nvdrv/core/container.h").read_text()
container_cpp=(R/"encore-active/eden-core/src/core/hle/service/nvdrv/core/container.cpp").read_text()
assert "public std::enable_shared_from_this<nvhost_as_gpu>" in as_h
assert channel_h.index("std::shared_ptr<void> address_space_owner{};") < channel_h.index(
    "std::optional<Payload> payload;")
assert "bool RetainSession(SessionId id, const Kernel::KProcess* process);" in container_h
retain=container_cpp.split(
    "bool Container::RetainSession(SessionId id, const Kernel::KProcess* process) {",1)[1].split(
    "\n}\n",1)[0]
for token in ("session_guard", "impl->sessions.size()", "session.is_active",
              "session.process != process", "session.ref_count <= 0",
              "std::numeric_limits<s32>::max()", "++session.ref_count;"):
    assert token in retain,token
assert "std::optional<NvCore::SessionId> m_nv_session{};" in as_h
assert "bool m_channel_session_retained{};" in as_h
assert "m_nv_session = session_id;" in as_cpp
as_bind=as_cpp.split("NvResult nvhost_as_gpu::BindChannel(IoctlBindChannel& params) {",1)[1].split(
    "\n}\n",1)[0]
for token in ("auto owner = weak_from_this().lock();",
              "container.RetainSession(*m_nv_session, m_native_process)",
              "m_channel_session_retained = true;",
              "channel.address_space_owner = std::move(owner);",
              "channel.memory_manager = gmmu.get();"):
    assert token in as_bind,token
assert as_bind.index("container.RetainSession(*m_nv_session, m_native_process)") < as_bind.index(
    "channel.address_space_owner = std::move(owner);") < as_bind.index(
    "channel.memory_manager = gmmu.get();")
as_dtor=as_cpp.split("nvhost_as_gpu::~nvhost_as_gpu() {",1)[1].split(
    "\n}\n",1)[0]
assert as_dtor.index("native_sparse_pins.clear();") < as_dtor.index(
    "container.CloseSession(*m_nv_session);") < as_dtor.index(
    "m_native_process->Close(system.Kernel());")
print("NS1_MAXWELL_AS_LEASE_SOURCE_PASS owner_before_payload=1 exact_session=1 "
      "pins_before_session_close=1 no_new_asid=1")
