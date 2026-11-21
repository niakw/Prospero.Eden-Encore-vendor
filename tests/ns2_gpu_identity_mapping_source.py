#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
shared=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker_shared.h").read_text()
worker=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.cpp").read_text()
worker_hdr=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.h").read_text()
registration=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/gpu_memory_registration.h").read_text()
# Worker 172/173 are real SDK debug-label metadata on *live* NVMap
# handles. The selected worker must neither echo Success for retired IDs nor
# allocate a fake label object without recording its data.
for command, name in ((172, "SetGpuMemoryUserDebugLabel"),
                      (173, "SetGpuMemorySdkDebugLabel")):
    assert f'{{{command}, &IGpuWorker::{name}, "{name}"}}' in worker
    assert f"void {name}(HLERequestContext& ctx);" in worker_hdr
    sig = f"void IGpuWorker::{name}(HLERequestContext& ctx) {{"
    assert worker.count(sig) == 1
    body = worker.split(sig, 1)[1].split("\n}\n", 1)[0]
    for needle in ("std::find(m_memory.begin(), m_memory.end(), request->id)",
                   "m_pending_device_unlocks.contains(request->id)",
                   "GetNvMapFile().GetHandle(request->id)",
                   "m_memory_debug_labels.try_emplace(request->id)",
                   "catch (const std::bad_alloc&)",
                   "result = ResultSuccess"):
        assert needle in body, (name, needle)
for needle in ("packet.size() != 7", "packet[0] != 0xbc", "packet[1] != 5",
               "packet.size() != 5", "packet[0] != 0xbd", "packet[1] != 3",
               "DecodeGpuMemoryUserDebugLabel", "DecodeGpuMemorySdkDebugLabel"):
    assert needle in registration, needle
assert "std::map<u32, MemoryDebugLabels> m_memory_debug_labels;" in worker_hdr
release = worker.split("Result IGpuWorker::ReleaseMemory", 1)[1].split(
    "void IGpuWorker::CreateGpuAccessibleMemory", 1)[0]
assert release.count("m_memory_debug_labels.erase(id);") == 2

# Worker service IDs 209/210 correspond to SDK TIPC request words 0xE1/0xE2
# (the TIPC service command envelope accounts for +0x10). Metadata remains
# keyed to a live process-owned GPU address space and creates no GPU mapping.
for command, name in ((209, "SetGpuSegmentUserDebugLabel"),
                      (210, "SetGpuSegmentSdkDebugLabel")):
    assert f'{{{command}, &IGpuWorker::{name}, "{name}"}}' in worker
    assert f"void {name}(HLERequestContext& ctx);" in worker_hdr
    body=worker.split(f"void IGpuWorker::{name}(HLERequestContext& ctx) {{",1)[1].split(
        "\n}\n",1)[0]
    for needle in ("std::scoped_lock lock{m_mutex, m_shared->mutex}",
                   "m_shared->FindOwnedAddressSpace(request->address_space)",
                   "m_segment_debug_labels.try_emplace(key)",
                   "m_segment_debug_labels.size() < GpuWorkerProtocol::MaxMappings",
                   "catch (const std::bad_alloc&)",
                   "result = ResultSuccess"):
        assert needle in body, (name, needle)
for needle in ("DecodeGpuSegmentUserDebugLabel", "packet.size() != 11",
               "packet[0] != 0xe1", "packet[1] != 9",
               "DecodeGpuSegmentSdkDebugLabel", "packet.size() != 9",
               "packet[0] != 0xe2", "packet[1] != 7",
               "IsValidGpuSegmentDebugRange"):
    assert needle in registration, needle
assert "std::map<std::tuple<u32, u64, u64>, SegmentDebugLabels> m_segment_debug_labels;" in worker_hdr

# Worker 171 is a real nn::gpu SDK command, not a global success stub.
assert '{171, &IGpuWorker::EnsureGpuMemoryComptags, "EnsureGpuMemoryComptags"}' in worker
assert "void EnsureGpuMemoryComptags(HLERequestContext& ctx);" in worker_hdr
for needle in ("packet.size() != 3", "packet[0] != 0xbb", "packet[1] != 1",
               "packet[2] == 0"):
    assert needle in registration, needle
ensure=worker.split("void IGpuWorker::EnsureGpuMemoryComptags(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n",1)[0]
for needle in ("std::find(m_memory.begin(), m_memory.end(), *id)",
               "m_pending_device_unlocks.contains(*id)",
               "GetNvMapFile().GetHandle(*id)",
               "result = handle ? ResultSuccess : Kernel::ResultInvalidState"):
    assert needle in ensure, needle
assert "WaitForIdle" not in ensure
assert "CreateHandle" not in ensure and "Alloc(" not in ensure
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
nvhost_hdr=(R/"encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.h").read_text()
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


# OunceVK's real native channel setup queries event 7 then installs
# H:0x0C SET_ERROR_NOTIFIER before preemption and first GPFIFO2. Native
# Encore must retain the real process-owned NvMap backing and clear the
# 16-byte NVIDIA notification record instead of returning a stub Success.
assert 'case 0xc:' in nvhost_cpp
notifier_route=nvhost_cpp.split("case 0xc:",1)[1].split("case 0xd:",1)[0]
assert "SetErrorNotifier, input, output, fd" in notifier_route
notifier=nvhost_cpp.split(
    "NvResult nvhost_gpu::SetErrorNotifier(IoctlSetErrorNotifier& params, DeviceFD fd) {",1
)[1].split("\n}",1)[0]
for token in (
    "if (!UsesNativeAbi(fd))",
    "ValidateChannelOwner(fd, program_id)",
    "sessions.find(fd)",
    "nvmap.GetHandle(params.mem)",
    "core.GetSession(handle->session_id)",
    "backing_session->process != channel_session->process",
    "constexpr u64 NativeErrorNotifierBytes = 16",
    "handle->Duplicate(true)",
    "current.get() == handle.get()",
    "handle->dupes > 0",
    "NativeErrorNotifierBytes <= handle->orig_size - offset",
    "IsValidVirtualAddressRange",
    "memory.WriteBlock",
    "nvmap.FreeHandle(params.mem, true)",
    "native_error_notifier_handle = params.mem",
    "native_error_notifier_offset = static_cast<u64>(params.offset)",
):
    assert token in notifier, token
assert "NvResult SetErrorNotifier(IoctlSetErrorNotifier& params, DeviceFD fd);" in nvhost_hdr
assert "std::optional<u32> native_error_notifier_handle{};" in nvhost_hdr
assert "u64 native_error_notifier_offset{};" in nvhost_hdr
dtor=nvhost_cpp.split("nvhost_gpu::~nvhost_gpu() {",1)[1].split("\n}",1)[0]
assert "nvmap.FreeHandle(*native_error_notifier_handle, true);" in dtor
assert dtor.index("system.GPU().ReleaseChannel(*channel_state)") < dtor.index(
    "nvmap.FreeHandle(*native_error_notifier_handle, true);")

# OunceVK immediately reads H:0x17 after notifier/preemption and accepts
# channel setup only when the live 16-byte notification status is healthy.
# Read the same retained NvMap record; do not synthesize a zero result.
health_route=nvhost_cpp.split("case 0x17: { // Switch 2 NVGPU_IOCTL_CHANNEL_GET_ERROR_NOTIFICATION",1)[1].split(
    "case 0x18:",1)[0]
for token in ("command.raw != 0xC0104817U",
              "sizeof(IoctlGetErrorNotification)",
              "GetErrorNotification, input, output, fd"):
    assert token in health_route, token
health=nvhost_cpp.split(
    "NvResult nvhost_gpu::GetErrorNotification(IoctlGetErrorNotification& params, DeviceFD fd) {",1
)[1].split("\n}",1)[0]
for token in ("!UsesNativeAbi(fd)", "ValidateChannelOwner(fd, program_id)",
              "!native_error_notifier_handle", "nvmap.GetHandle(*native_error_notifier_handle)",
              "handle->internal_dupes <= 0", "native_error_notifier_offset",
              "IsValidVirtualAddressRange", "memory.ReadBlock",
              "sizeof(IoctlGetErrorNotification)", "return NvResult::Success"):
    assert token in health, token
assert "NvResult GetErrorNotification(IoctlGetErrorNotification& params, DeviceFD fd);" in nvhost_hdr
assert "0 = healthy; non-zero = channel/GPU error" in nvhost_hdr

# OunceVK closes the same notifier by sending a completely zero H:0x0C
# payload. This must release the retained duplicate without accepting malformed
# partially-zero disable requests.
for token in ("if (params.mem == 0)", "params.offset) != 0", "params.size) != 0",
              "nvmap.FreeHandle(*native_error_notifier_handle, true)",
              "native_error_notifier_handle.reset()", "native_error_notifier_offset = 0"):
    assert token in notifier, token

# Switch 2 channel preemption-mode ioctl (SwitchBrew 0xC008481F).
# NVIDIA common UAPI permits compute CTA (bit1), and OunceVK's actual
# native 3D setup sends {graphics=0, compute=2} before first GPFIFO submit.
# Store this policy, but keep unsupported GFXP/CILP values fail-closed.
preempt=nvhost_cpp.split("case 0x1f: { // Switch 2 NVGPU_IOCTL_CHANNEL_SET_PREEMPTION_MODE",1)[1].split(
    "case 0x20: // Switch 2 NVGPU_IOCTL_CHANNEL_OPEN",1)[0]
for token in ("command.raw != 0xC008481FU", "input.size() != 8 || output.size() != 8",
              "const std::scoped_lock lock{channel_mutex};", "!UsesNativeAbi(fd)",
              "ValidateChannelOwner(fd, program_id)", "std::array<u32_le, 2> modes{}",
              "constexpr u32 NativeWfiBit = 1u;", "constexpr u32 NativeComputeCtaBit = 2u;",
              "compute_mode & ~(NativeWfiBit | NativeComputeCtaBit)",
              "native_graphics_preemption_mode = graphics_mode",
              "native_compute_preemption_mode = compute_mode",
              "return NvResult::NotImplemented;",
              "std::memcpy(output.data(), modes.data(), sizeof(modes));"):
    assert token in preempt,token
assert "u32 native_graphics_preemption_mode{};" in nvhost_hdr
assert "u32 native_compute_preemption_mode{};" in nvhost_hdr
assert preempt.index("ValidateChannelOwner(fd, program_id)") < preempt.index(
    "const u32 graphics_mode") < preempt.index(
    "native_compute_preemption_mode = compute_mode") < preempt.index(
    "std::memcpy(output.data(), modes.data(), sizeof(modes));")


# Zero remains no-change; bit0 is WFI and compute bit1 is CTA. Other
# graphics/compute bits are not silently accepted by Encore.
assert '#include <array>' in nvhost_cpp
assert "(graphics_mode & ~NativeWfiBit) != 0" in preempt
assert "(compute_mode & ~(NativeWfiBit | NativeComputeCtaBit)) != 0" in preempt


# NS2 fixed GPU views must not overwrite pinned resources, and a
# FreeSpace followed by UnmapBuffer must not dereference a retired map.
map_ex=as_cpp.split("NvResult nvhost_as_gpu::MapBufferEx(IoctlMapBufferEx& params) {",1)[1].split(
    "\n}",1)[0]
free_map=as_cpp.split("bool nvhost_as_gpu::FreeMappingLocked(u64 offset) noexcept {",1)[1].split(
    "\n}",1)[0]
unmap=as_cpp.split("NvResult nvhost_as_gpu::UnmapBuffer(IoctlUnmapBuffer& params) {",1)[1].split(
    "\n}",1)[0]
for needle in (
    "NativeGpuProcessRegistry::Instance().Contains(m_native_process)",
    "container.GetSession(handle->session_id)",
    "!handle->allocated || !owner || owner->process != m_native_process",
    "mapping_map.lower_bound(address)",
    "std::prev(next)->second.size > address - std::prev(next)->first",
):
    assert needle in map_ex, needle
assert map_ex.index("mapping_map.lower_bound(address)") < map_ex.index(
    "const DAddr pinned = nvmap.PinHandle")
assert "map_buffer_offsets.erase(offset);" in free_map
assert free_map.index("mapping_map.erase(offset);") < free_map.index(
    "map_buffer_offsets.erase(offset);")
assert "if (it == mapping_map.end()) {" in unmap
assert unmap.index("if (it == mapping_map.end()) {") < unmap.index(
    "auto const mapping = it->second;")
assert "map_buffer_offsets.erase(offset_it);" in unmap

# Dynamic native GPU mappings reserve ceil(bytes/page) units; unmap
# must return the same number even for a partially filled final page.
unmap_bytes=unmap.split("if (!mapping.fixed) {",1)[1].split(
    "// Sparse mappings shouldn't be fully unmapped",1)[0]
for needle in (
    "NativeGpuProcessRegistry::Instance().Contains(m_native_process)",
    "Common::AlignUp(mapping.size,",
    "mapping.big_page ? vm.big_page_size : VM::YUZU_PAGESIZE",
    ": mapping.size;",
    "rounded_size >> page_size_bits",
):
    assert needle in unmap_bytes, needle
assert unmap_bytes.index("Common::AlignUp(mapping.size,") < unmap_bytes.index(
    "allocator.Free(")
for page in (0x1000,0x20000):
    for size in (1,page-1,page,page+1,page*2-1,page*2):
        reserved=(size+page-1)//page
        old_freed=size//page
        assert reserved == old_freed+(1 if size%page else 0)


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

# GetVARegions1/3 serialize 64-bit byte offsets from u32 page indexes.
# The old u32-first shift wrapped the 16-GiB GA10F big-page region to 0.
va_regions=as_cpp.split("void nvhost_as_gpu::GetVARegionsImpl(IoctlGetVaRegions& params) {",1)[1].split(
    "\n}",1)[0]
assert ".offset = static_cast<u64>(vm.small_page_allocator->GetVAStart()) << VM::PAGE_SIZE_BITS," in va_regions
assert ".offset = static_cast<u64>(vm.big_page_allocator->GetVAStart()) << vm.big_page_size_bits," in va_regions
for accessor in ("GetVARegions1", "GetVARegions3"):
    block=as_cpp.split("NvResult nvhost_as_gpu::"+accessor+"(",1)[1].split("\n}",1)[0]
    assert "GetVARegionsImpl(params);" in block
native_big_page_index=(1<<34)>>17
assert (native_big_page_index<<17)==(16<<30)
assert ((native_big_page_index<<17)&0xFFFFFFFF)==0


# Native GPU topology and engine/runlist discovery must not leak through
# the shared NVDRV ioctl3 device into a Maxwell/NS1 session. Guard after
# the exact NS2 command-number check, before copying any response bytes.
ctrl_gpu = (R/"encore-active/ns2-runtime/nvdrv/core/hle/service/nvdrv/devices/nvhost_ctrl_gpu.cpp").read_text()
native_info = ctrl_gpu.split("if (command.raw != 0xC010471FU) break;", 1)[1].split("case 0x13: {", 1)[0]
native_vsms = ctrl_gpu.split("if (command.raw != 0xC0084713U) break;", 1)[1].split("default:", 1)[0]
for branch, use in ((native_info, "engine"), (native_vsms, "vsms")):
    assert "if (!UsesNativeAbi(fd)) return NvResult::NotImplemented;" in branch, use
    assert branch.index("if (!UsesNativeAbi(fd)) return NvResult::NotImplemented;") < branch.index(
        "return NvResult::Success;"), use


# Shared native GPU VA allocator: upstream Lichii #1896/#1911 repairs
# FIFO-free holes, fixed-block skipping, and last-block/limit exhaustion.
allocator_cpp = (R/"sources/eden/src/common/address_space.inc").read_text()
allocator_path = allocator_cpp.split("ALLOC_MEMBER(VaType)::Allocate(VaType size) {", 1)[1].split(
    "ALLOC_MEMBER(void)::AllocateFixed(", 1)[0]
for token in ("std::upper_bound(", "std::prev(", "block->Unmapped()",
              "current_linear_alloc_end", "find_free(virt_start)",
              "this->MapLocked(alloc_start, true, size, {});"):
    assert token in allocator_path, token
assert allocator_cpp.count("blocks.erase(block_start_successor, block_end_successor);") >= 1
assert "if (virt + size == current_linear_alloc_end)" in allocator_cpp

# Native GA10F GPU AS consolidation. This source-only contract captures
# the reviewed pending 40-byte remap, real mapped VA owner, physical-page
# alignment, failure-safe pin publication and GSV video image retirement.
# It must not be mistaken for an actual native PS5 build or guest frame.
from pathlib import Path as _Path
_as_runtime = (_Path(__file__).resolve().parents[1] /
    "encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
_as_header = (_Path(__file__).resolve().parents[1] /
    "encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.h").read_text()
_va_layout = (_Path(__file__).resolve().parents[1] /
    "encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/native_gpu_va_layout.h").read_text()
_native_gpu = (_Path(__file__).resolve().parents[1] /
    "encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()
_ctrl_gpu = (_Path(__file__).resolve().parents[1] /
    "encore-active/ns2-runtime/nvdrv/core/hle/service/nvdrv/devices/nvhost_ctrl_gpu.cpp").read_text()
_gsv = (_Path(__file__).resolve().parents[1] /
    "encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.cpp").read_text()
for marker in ("case 0x4: {", "case 0x7: {", "case 0xa: {",
               "case 0xc: {", "case 0xd: {",
               "native_sparse_pins.merge(newly_pinned);",
               "if ((u32{entry.flags} & 0x7000U) == 0)",
               "native_sparse_poisoned = true;",
               "system.GPU().WaitForIdle();"):
    assert marker in _as_runtime, marker
assert "bool native_sparse_poisoned{};" in _as_header
assert "split > end" in _va_layout
assert "std::bad_alloc" in _native_gpu
assert "if (!UsesNativeAbi(fd)) return NvResult::NotImplemented;" in _ctrl_gpu
assert "system.GPU().WaitForIdle();" in _gsv

# Exactly one native GPU idle drain for the retirement ioctls.
# A:3/A:5 delegate to their own locking-aware method guards, instead
# of issuing a second WaitForIdle before forwarding.
_ioctl_as = _as_runtime.split("NvResult nvhost_as_gpu::Ioctl1(",1)[1].split(
    "\nNvResult nvhost_as_gpu::Ioctl2(",1)[0]
for cmd, next_cmd, fn in (("0x3", "0x4", "FreeSpace"),
                          ("0x5", "0x6", "UnmapBuffer")):
    part = _ioctl_as.split(f"case {cmd}:",1)[1].split(f"case {next_cmd}:",1)[0]
    assert "system.GPU().WaitForIdle();" not in part,(cmd,"duplicate wait")
    assert f"&nvhost_as_gpu::{fn}" in part,(cmd,fn)
    method = _as_runtime.split(f"NvResult nvhost_as_gpu::{fn}(",1)[1].split(
        "std::scoped_lock lock(mutex);",1)[0]
    assert "system.GPU().WaitForIdle();" in method,(fn,"missing barrier")

# A failed GMMU MapSparse followed by failed Unmap is a partially
# committed GPU page table. Both native sparse allocator branches must
# quarantine BEFORE returning, keeping the VA ledger/pins reserved.
_allocate_block = _as_runtime.split("NvResult nvhost_as_gpu::AllocateSpace(",1)[1].split(
    "\n}",1)[0]
for allocator_name in ("first_page", "start_page"):
    rollback = (
        "allocation_map.erase(address);\n"
        f"                allocator.Free({allocator_name}, params.pages);"
    )
    # The regular native allocator can erase the same ledger entry in
    # its earlier try_emplace failure path. Inspect the FINAL rollback tied
    # to failed MapSparse, not that unrelated pre-publication cleanup.
    rollback_offset = _allocate_block.rindex(rollback)
    failure = _allocate_block.rfind("} catch (const std::bad_alloc&) {", 0,
                                  rollback_offset)
    assert failure >= 0,allocator_name
    error_block = _allocate_block[failure:rollback_offset]
    # GMMU rollback attempt precedes the inner catch that quarantines it.
    unmap = _allocate_block.rfind("gmmu->Unmap(address, size);", 0, failure)
    assert unmap >= 0 and failure - unmap < 150,allocator_name
    assert "native_sparse_poisoned = true;" in error_block,allocator_name
    assert error_block.index("native_sparse_poisoned = true;") < error_block.index(
        "return NvResult::InvalidState;"),allocator_name

# Native AllocSpace must reject any page size other than 4K or its
# negotiated big-page size before reserving the FlatAllocator page count.
# Otherwise the declared GMMU span can exceed the actual VA reservation.
_alloc = _as_runtime.split("NvResult nvhost_as_gpu::AllocateSpace(IoctlAllocSpace& params) {",1)[1].split(
    "\n}",1)[0]
_native_non_sparse = _alloc.split("const bool big = params.page_size == vm.big_page_size;",1)[0]
for marker in ("params.page_size != VM::YUZU_PAGESIZE",
               "params.page_size != vm.big_page_size",
               "vm.small_page_allocator",
               "vm.big_page_allocator"):
    assert marker in _native_non_sparse,marker
assert _native_non_sparse.index("params.page_size != VM::YUZU_PAGESIZE") < len(_native_non_sparse)


# Native retirement must drain GPU readers before GMMU unmap or
# NvMap unpin; do it outside the AS mutex to avoid blocking the worker.
for name in ("FreeSpace", "UnmapBuffer"):
    block = _as_runtime.split("NvResult nvhost_as_gpu::" + name + "(", 1)[1].split(
        "std::scoped_lock lock(mutex);", 1)[0]
    for marker in ("NativeGpuProcessRegistry::Instance().Contains(m_native_process)",
                   "system.IsPoweredOn() && !system.IsShuttingDown()",
                   "system.GPU().WaitForIdle();"):
        assert marker in block, (name, marker)
    assert block.index("system.GPU().WaitForIdle();") < len(block)


# Preserve public NVDRV A:0x0C mapping-kind coverage for both direct
# NvMap views and sparse remap pins. The two valid ownership domains are
# disjoint; preflight the entire sparse range before the first GMMU mutation.
_mod_kind = _as_runtime.split("NvResult nvhost_as_gpu::ModifyNativeKind(u64 address, u64 size, u8 kind) {",1)[1].split(
    "\n}",1)[0]
for marker in ("mapping_map.upper_bound(address)",
               "NativeGpuAllocation::FindContaining(",
               "native_sparse_regions.upper_bound(address)",
               "native_sparse_mappings.Ranges()",
               "native_sparse_pins.find(range.handle)",
               "NativePinnedPhysicalSpan(pin->second, source, length)",
               "static_cast<Tegra::PTEKind>(kind), big_page)",
               "native_sparse_poisoned = true;"):
    assert marker in _mod_kind, marker
assert _mod_kind.index("while (cursor < end) {") < _mod_kind.index(
    "\n    cursor = address;") < _mod_kind.index("gmmu->Map(cursor,")



# Native repeated ALLOC_AS_EX on the same authenticated descriptor
# fails as a guest ioctl error before the legacy Maxwell host assertion.
_alloc_as = _as_runtime.split("NvResult nvhost_as_gpu::AllocAsEx(IoctlAllocAsEx& params) {",1)[1].split(
    "\n}",1)[0]
_repeat = _alloc_as.split("if (vm.initialised) {",1)[1].split(
    "\n    }",1)[0]
assert _repeat.index("NativeGpuProcessRegistry::Instance().Contains(m_native_process)") < _repeat.index(
    "return NvResult::InvalidState;") < _repeat.index(
    'ASSERT_MSG(false, "Cannot initialise an address space twice!");')


# Preserve the public source contracts when the individual A:14 and A:0xD
# remap ledgers are consolidated into one native sparse pin owner.
_as_ioctl = _as_runtime.split(
    "NvResult nvhost_as_gpu::Ioctl1(DeviceFD fd, Ioctl command,",1)[1].split(
    "\nNvResult nvhost_as_gpu::Ioctl2(",1)[0]
for marker in ("case 0xc: {", "command.raw != 0xC020410CU",
               "static_assert(sizeof(NativeModify) == 32)",
               "const auto status = ModifyNativeKind(",
               "case 0xd: {",
               "(command.raw & 0xC000FFFFU) != 0xC000410DU",
               "sizeof(NativeSparseRemapEntry)",
               "const auto status = RemapNativeSparse(entries);"):
    assert marker in _as_ioctl,marker
assert _as_ioctl.index("system.GPU().WaitForIdle();", _as_ioctl.index("case 0xc: {")) < (
    _as_ioctl.index("const auto status = ModifyNativeKind("))
assert _as_ioctl.index("system.GPU().WaitForIdle();", _as_ioctl.index("case 0xd: {")) < (
    _as_ioctl.index("const auto status = RemapNativeSparse(entries);"))
# Both legacy A:14 and native A:0xD must converge on the single
# transaction protecting intermediate and final NvMap pins.
_remap = _as_runtime.split(
    "NvResult nvhost_as_gpu::RemapNativeSparse(",1)[1].split("\n}",1)[0]
for marker in ("updated = native_sparse_mappings;",
               "updated.Replace(*range);",
               "mapping_map.lower_bound(range->address)",
               "needed = retained;",
               "newly_pinned.try_emplace(handle, DAddr{})",
               "NativePinnedPhysicalSpan(pinned, range.source_offset, range.size)",
               "native_sparse_pins.merge(newly_pinned);",
               "gmmu->MapSparse(range.address, range.size, big_page_ranges[i]);",
               "native_sparse_poisoned = true;",
               "native_sparse_mappings = std::move(updated);",
               "nvmap.UnpinHandle(it->first);"):
    assert marker in _remap,marker
assert _remap.index("updated = native_sparse_mappings;") < _remap.index(
    "newly_pinned.try_emplace(handle, DAddr{})") < _remap.index(
    "native_sparse_pins.merge(newly_pinned);") < _remap.index(
    "native_sparse_mappings = std::move(updated);")
# Big-page and 4K sparse allocations must share a type-correct
# allocator and never publish an unsupported page size.
_alloc = _as_runtime.split("NvResult nvhost_as_gpu::AllocateSpace(",1)[1].split(
    "\n}",1)[0]
for marker in ("const bool small_sparse =", 
               "params.page_size != VM::YUZU_PAGESIZE &&",
               "params.page_size != vm.big_page_size)",
               "auto& allocator = big ? *vm.big_page_allocator : *vm.small_page_allocator;",
               "allocation_map.try_emplace(",
               "gmmu->MapSparse(address, size, big);"):
    assert marker in _alloc,marker
assert _alloc.index("params.page_size != vm.big_page_size)") < _alloc.index(
    "auto& allocator = big ? *vm.big_page_allocator : *vm.small_page_allocator;")

print("NS2_GPU_IDENTITY_MAPPING_SOURCE_PASS sub101=identity replay=new_as sub102=all_owned_unmap as_fd_close=lease_before_payload session=retained retire=gpu-worker shutdown=guarded")
