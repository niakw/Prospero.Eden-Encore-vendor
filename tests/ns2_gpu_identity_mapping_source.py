#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
shared=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker_shared.h").read_text()
worker=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.cpp").read_text()
worker_hdr=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.h").read_text()
registration=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/gpu_memory_registration.h").read_text()
# A 215/216 GMMU page-table OOM can poison an ALREADY-BACKED 214 AS.
# The worker destructor must detect it before any NVMap release and must
# skip both resident CPU alias retirement and NvCore CloseSession.
teardown=worker.split("IGpuWorker::~IGpuWorker() {",1)[1].split(
    "Result IGpuWorker::BackGpuOnlyRegion(",1)[0]
for token in (
    "for (const auto id : m_shared->address_spaces.OwnedIds())",
    "as->IsNativeSparsePoisoned()",
    "if (!m_quarantined_nv_session)\n    for (const auto& [key, mapping]",
    "if (!m_quarantined_nv_session)\n    for (const u32 memory_id : m_memory)",
    "if (!m_quarantined_nv_session)\n    while (!m_dense_regions.empty())",
    "if (!m_quarantined_nv_session)\n    for (const u32 id : m_memory)",
    "for (auto& [key, region] : m_dense_regions)",
    "(void)region.pages.release();",
    "if (m_nv_session && !m_quarantined_nv_session)",
):
    assert token in teardown,token
assert teardown.index("as->IsNativeSparsePoisoned()") < teardown.index(
    "for (const auto& [key, mapping]")
assert teardown.index("for (auto& [key, region] : m_dense_regions)") < teardown.index(
    "if (m_nv_session && !m_quarantined_nv_session)")

native_as_h=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.h").read_text()
native_as_cpp=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
# A partially OOM-committed GMMU must never lose its physical page backing.
# Native NvMap FreeHandle(final dupe) can unmap pins; the selected GPU-only
# backing owner must quarantine the CPU alias + KPageGroup in this rare case.
assert "[[nodiscard]] bool IsNativeSparsePoisoned();" in native_as_h
poison=native_as_cpp.split("bool nvhost_as_gpu::IsNativeSparsePoisoned() {",1)[1].split("\n}",1)[0]
assert "std::scoped_lock lock{mutex};" in poison
assert "return native_sparse_poisoned;" in poison
dense=worker.split("Result IGpuWorker::BackGpuOnlyRegion(",1)[1].split(
    "bool IGpuWorker::RetireGpuOnlyRegion(",1)[0]
assert "mapping_as->RemapNativeSparse(" in dense
worker_cpp_destructor=worker.split("IGpuWorker::~IGpuWorker() {",1)[1].split(
    "Result IGpuWorker::BackGpuOnlyRegion(",1)[0]
assert "bool m_quarantined_nv_session{};" in worker_hdr
assert "if (m_nv_session && !m_quarantined_nv_session)" in worker_cpp_destructor
assert "if (m_nv_session && m_quarantined_nv_session)" in worker_cpp_destructor
assert "mapping_as->IsNativeSparsePoisoned()" in dense
quarantine=dense.split("if (mapping_as && mapping_as->IsNativeSparsePoisoned()) {",1)[1].split(
    "if (handle) {",1)[0]
for token in ("m_quarantined_nv_session = true;",
              "(void)pages.release();", "return Kernel::ResultOutOfResource;"):
    assert token in quarantine,token
assert dense.index("mapping_as->IsNativeSparsePoisoned()") < dense.index(
    "nvmap.FreeHandle(id, false)")

worker_protocol=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/gpu_worker_protocol.h").read_text()
# MKW recreates sparse region 214 at the exact same start VA while
# growing its extent. Encore must retire that exact predecessor like a 215
# before reserving the replacement, but keep arbitrary overlap fail-closed.
for token in (
    "std::optional<Mapping> ExactSparse(",
    "PrepareSparseReplacingExact(",
    "if (exact == mappings.end() || !exact->second.sparse)",
    "next->second.address - address < size",
    "previous->second.address < previous->second.address + previous->second.size"
):
    # The last predicate is checked structurally below because the production
    # expression is written as an overflow-free subtraction.
    if token.startswith("previous->second.address <"):
        continue
    assert token in worker_protocol, token
create_sparse=worker.split(
    "void IGpuWorker::CreateSparseRegion(HLERequestContext& ctx) {",1
)[1].split("\nvoid IGpuWorker::DestroySparseRegion(",1)[0]
for token in (
    "m_mappings.ExactSparse(request->address_space, request->begin)",
    "m_mappings.PrepareSparseReplacingExact(",
    '"214_replace_wait_idle"',
    "system.GPU().WaitForIdle();",
    "as->ReleaseNativeSparse4K(",
    "RetireGpuOnlyRegion(",
    "m_mappings.RecordReleasedSparse(",
    "as->ReserveNativeSparse4K(",
):
    assert token in create_sparse, token
assert create_sparse.index("m_mappings.PrepareSparseReplacingExact(") < create_sparse.index(
    "system.GPU().WaitForIdle();") < create_sparse.index(
    "as->ReleaseNativeSparse4K(") < create_sparse.index(
    "m_mappings.RecordReleasedSparse(") < create_sparse.index(
    "as->ReserveNativeSparse4K(")

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
shared_cpp=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker_shared.cpp").read_text()
registry=(R/"encore-active/ns2-runtime/hle/core/hle/service/nvdrv/devices/native_gpu_process_registry.h").read_text()
as_h=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.h").read_text()
as_cpp=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
for x in ("struct IdentityGpuMapping","identity_gpu_mappings","AddSpace","RemoveSpace"):
    assert x in shared,x
assert "Status Remove(U32 id)" in proto

reg=worker[worker.index("Result IGpuWorker::RegisterMemory"):worker.index("Result IGpuWorker::ReleaseMemory")]
for x in ("IdentityGpuMapping{id, begin, size","as->MapNative4K(id, begin, 0, size, 0)",
          "identity.AddSpace(*owned)","identity_gpu_mappings.pop_back()"):
    assert x in reg,x
reg_replay=reg[reg.index("for (const auto owned : m_shared->address_spaces.OwnedIds())"):
               reg.index("if (identity_result.IsError())")]
assert "catch (const std::bad_alloc&)" in reg_replay
assert "if (!identity.AddSpace(*owned))" in reg_replay
assert reg_replay.index("as->UnmapNative4K(begin, size)") < reg_replay.index(
    "identity_result = Kernel::ResultInvalidState;",reg_replay.index("if (!identity.AddSpace(*owned))"))

# Worker 101 rollback must not unlock/free the guest backing after any failed
# identity retirement, nor publish a failed registration for later AS replay.
assert "R_UNLESS(!m_quarantined_nv_session, Kernel::ResultInvalidState);" in reg
assert "if (as->IsNativeSparsePoisoned()) rollback_safe = false;" in reg_replay
assert "rollback_safe = false;" in reg_replay.split("catch (const std::bad_alloc&)", 1)[1]
reg_rollback = reg.split("if (identity_result.IsError())", 1)[1]
assert "if (as->UnmapNative4K(begin, size) != Nvidia::NvResult::Success)" in reg_rollback
quarantined = reg_rollback.split("if (!rollback_safe) {", 1)[1].split("\n        }", 1)[0]
for marker in ("m_quarantined_nv_session = true;", "id = 0;",
               "return Kernel::ResultInvalidState;"):
    assert marker in quarantined, marker
assert "UnlockForDeviceAddressSpace" not in quarantined and "FreeHandle" not in quarantined
assert reg_rollback.index("identity_gpu_mappings.pop_back()") < reg_rollback.index(
    "if (!rollback_safe)") < reg_rollback.index("UnlockForDeviceAddressSpace") < reg_rollback.index("nvmap.FreeHandle")
assert reg_rollback.index("id = 0;", reg_rollback.index("nvmap.FreeHandle")) > reg_rollback.index("nvmap.FreeHandle")

release=worker[worker.index("Result IGpuWorker::ReleaseMemory"):worker.index("void IGpuWorker::CreateGpuAccessibleMemory")]
for x in ("mapping.memory == id","system.GPU().WaitForIdle()",
          "auto& ledger = owned_map.sparse_overlay ? m_sparse_overlays : m_mappings;",
          "ledger.RecordUnmapped(unmap)",
          "identity_it->RemoveSpace(as_id)","identity_gpu_mappings.erase(identity_it)",
          "handle->pins == 0"):
    assert x in release,x
assert release.index("identity_gpu_mappings.erase(identity_it)") < release.index("handle->pins == 0")

# cmd33 owns only the opaque GSV VM record.
create=session[session.index("void IGpuSession::CreateAddressSpace"):session.index("void IGpuSession::SynchronizeAddressSpace")]
for x in ("NativeGpuProcessRegistry::Instance().AllocateVm(",
          "m_shared->address_spaces.Add(*candidate)",
          "NativeGpuProcessRegistry::Instance().RemoveVm(","id = *candidate;"):
    assert x in create,x
for forbidden in ('m_module->Open("/dev/nvhost-as-gpu"', "m_module->Ioctl1(",
                  "static_cast<u32>(fd)", "MapNative4K", "AllocAsEx"):
    assert forbidden not in create,forbidden
register_vm=session.split("Result IGpuSession::RegisterAddressSpace(u32 id) {",1)[1].split("\n}",1)[0]
assert "NativeGpuProcessRegistry::Instance().RegisterVm(m_process, id)" in register_vm
for x in ("AllocateVm(","RegisterVm(","FindRegisteredVmOwner(","RemoveVm(",
          "std::array<std::optional<VmEntry>, 8> vms{};"):
    assert x in registry,x

# Worker101 replay is now part of the actual NVDRV AS_IMPORT publication.
bind=shared_cpp.split("Nvidia::NvResult GpuWorkerSharedState::BindImportedAddressSpace(",1)[1].split("\n}",1)[0]
for x in ("address_spaces.Registered(id)","opened_gpu_as.try_emplace(id, as)",
          "for (auto& identity : identity_gpu_mappings)",
          "as->MapNative4K(identity.handle, identity.address, 0, identity.size, 0)",
          "if (!identity.AddSpace(id))","(void)as->UnmapNative4K(identity.address, identity.size)",
          "identity.RemoveSpace(id)","opened_gpu_as.erase(id)"):
    assert x in bind,x
assert bind.index("opened_gpu_as.try_emplace(id, as)") < bind.index("as->MapNative4K(identity.handle") < bind.index("identity.AddSpace(id)")
rollback=bind.split("if (replay == Nvidia::NvResult::Success) return replay;",1)[1]
assert rollback.index("as->UnmapNative4K(identity.address, identity.size)") < rollback.index("identity.RemoveSpace(id)") < rollback.index("opened_gpu_as.erase(id)")

for x in ("struct NativeVmImport","u32_le version{};","u32_le vm_id{};",
          'static_assert(sizeof(NativeVmImport) == 40',"u32 m_native_vm_id{};"):
    assert x in as_h,x
case9=as_cpp.split("case 0x9: {",1)[1].split("case 0xa: {",1)[0]
for x in ("command.raw == 0x40284109U","sizeof(NativeVmImport)","ImportNativeVm(parameters)"):
    assert x in case9,x
native_import=as_cpp.split("NvResult nvhost_as_gpu::ImportNativeVm(const NativeVmImport& params) {",1)[1].split("\n}",1)[0]
for x in ("u32{params.version} != 5","u32{params.vm_id} == 0",
          "u64{params.user_first} != NativeNs2GpuVaStart",
          "u64{params.user_end} != NativeNs2GpuVaSmallSplit",
          "FindRegisteredVmOwner(","layout.big_page_size = 0x20000;",
          "layout.va_range_end = NativeNs2GpuVaEnd;","AllocAsEx(layout)",
          "owner->BindImportedAddressSpace(","m_native_vm_id = params.vm_id;"):
    assert x in native_import,x
assert native_import.index("FindRegisteredVmOwner(") < native_import.index("AllocAsEx(layout)") < native_import.index("owner->BindImportedAddressSpace(") < native_import.index("m_native_vm_id = params.vm_id;")

reset=as_cpp.split("bool nvhost_as_gpu::ResetFailedNativeVmImport() {",1)[1].split("\n}",1)[0]
for x in ("!mapping_map.empty()","!allocation_map.empty()","!native_sparse_regions.empty()",
          "!native_sparse_pins.empty()","native_sparse_poisoned","gmmu.reset();","vm = VM{};"):
    assert x in reset,x
on_close=as_cpp.split("void nvhost_as_gpu::OnClose(DeviceFD fd) {",1)[1].split("\n}",1)[0]
assert "ReleaseNativeVmBinding();" in on_close
unbind=shared_cpp.split("void GpuWorkerSharedState::UnbindImportedAddressSpace(",1)[1].split("\n}",1)[0]
for x in ("identity.RemoveSpace(id)","opened_gpu_as.erase(it)"):
    assert x in unbind,x

assert "native_registered = Nvidia::Devices::NativeGpuProcessRegistry::Instance().Register(process);" in shared_cpp
shared_destroy=shared_cpp.split("GpuWorkerSharedState::~GpuWorkerSharedState() {",1)[1].split("\n}",1)[0]
for x in ("NativeGpuProcessRegistry::Instance().RemoveVm(","opened_gpu_as.clear();",
          "NativeGpuProcessRegistry::Instance().Unregister(native_process);"):
    assert x in shared_destroy,x
for forbidden in ("module->Close(","CloseSession(","nv_session"):
    assert forbidden not in shared_destroy,forbidden
assert shared_destroy.index("NativeGpuProcessRegistry::Instance().RemoveVm(") < shared_destroy.index("NativeGpuProcessRegistry::Instance().Unregister(native_process);")

# Native NVDRV descriptors keep the ABI generation they were opened under.
# GSV teardown invalidates that generation and retires imported VM ownership;
# stale Ampere FDs must fail closed instead of falling through to Maxwell.
nvhost_hdr=(R/"encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.h").read_text()
nvhost_cpp=(R/"encore-active/ns2-runtime/nvdrv_gpu/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()
for token in ("std::optional<u64> m_native_generation{};",
              "bool m_native_vm_retired{};",
              "void RetireNativeVm(u32 id);",
              "NativeEpochCurrent() const"):
    assert token in as_h, token
as_open=as_cpp.split("void nvhost_as_gpu::OnOpen(",1)[1].split("\n}",1)[0]
for token in ("CaptureTicket(m_native_process)", "m_native_generation = ticket->generation"):
    assert token in as_open, token
for ioctl in ("Ioctl1", "Ioctl2", "Ioctl3"):
    body=as_cpp.split("NvResult nvhost_as_gpu::"+ioctl+"(",1)[1].split("\n}",1)[0]
    assert "m_native_vm_retired" in body
    assert "m_native_generation && !NativeEpochCurrent()" in body
retire=as_cpp.split("void nvhost_as_gpu::RetireNativeVm(u32 id) {",1)[1].split("\n}",1)[0]
for token in ("m_native_vm_id != id", "m_native_vm_id = 0",
              "m_native_vm_retired = true"):
    assert token in retire, token
assert shared_destroy.index("as->RetireNativeVm(id)") < shared_destroy.index(
    "NativeGpuProcessRegistry::Instance().RemoveVm(")

for token in ("::Common::unordered_map<DeviceFD, u64> native_generations;",
              "bool NativeEpochCurrent(DeviceFD fd) const;"):
    assert token in nvhost_hdr, token
gpu_open=nvhost_cpp.split("void nvhost_gpu::OnOpen(",1)[1].split("\n}",1)[0]
for token in ("CaptureTicket(session->process)",
              "native_generations.emplace(fd, ticket->generation)"):
    assert token in gpu_open, token
uses_native=nvhost_cpp.split("bool nvhost_gpu::UsesNativeAbi(DeviceFD fd) const {",1)[1].split("\n}",1)[0]
assert "native_generations.contains(fd)" in uses_native
assert "NativeGpuProcessRegistry::Instance().Contains" not in uses_native
epoch=nvhost_cpp.split("bool nvhost_gpu::NativeEpochCurrent(DeviceFD fd) const {",1)[1].split("\n}",1)[0]
for token in ("native_generations.find(fd)", "IsCurrent("):
    assert token in epoch, token
for ioctl in ("Ioctl1", "Ioctl2", "Ioctl3"):
    body=nvhost_cpp.split("NvResult nvhost_gpu::"+ioctl+"(",1)[1].split("\n}",1)[0]
    assert "UsesNativeAbi(fd) && !NativeEpochCurrent(fd)" in body
fd_event=nvhost_cpp.split(
    "Kernel::KEvent* nvhost_gpu::QueryEvent(DeviceFD fd, u32 event_id) {",1
)[1].split("\n}",1)[0]
assert "UsesNativeAbi(fd) && !NativeEpochCurrent(fd)" in fd_event

gsv=(R/"encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.cpp").read_text()
open_gpu=gsv.split("void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {",1)[1].split("\n}",1)[0]
assert "std::make_shared<IGpuSession>(system, module, m_process)" in open_gpu
assert "catch (const std::bad_alloc&)" in open_gpu
assert "result = Kernel::ResultOutOfResource;" in open_gpu

# An AS FD can close while its scheduler channel still has queued work.
# Keep a strong AS/GMMU lease in ChannelState through GPU payload teardown.
# Maxwell/NS1 and Ampere/NS2 both require the exact bound AS lease;
# it does not allocate another GPU VA, synthetic mapping or ASID.
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
    "auto owner = weak_from_this().lock();") < as_bind.index(
    "channel.address_space_owner = std::move(owner);") < as_bind.index(
    "channel.memory_manager = gmmu.get();")
assert "#include \"core/hle/service/nvdrv/devices/native_gpu_process_registry.h\"" in as_cpp
assert as_bind.index("if (!m_channel_session_retained)") < as_bind.index(
    "container.RetainSession(*m_nv_session, m_native_process)")
assert "if (!owner) {" in as_bind

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
ioctl_serialization=(R/"encore-active/eden-core/src/core/hle/service/nvdrv/devices/ioctl_serialization.h").read_text()
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
for needle in ("!sessions.empty() && channel_state",
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


# Channel/TSG enable is one native state. Ounce exposes H:0x0E..0x11;
# NVIDIA TSG enable walks its channels and invokes the same channel enable.
for raw,cmd,state,next_cmd in (
    ("0x0000480EU","0xe","true","0xf"),
    ("0x0000480FU","0xf","false","0x10"),
):
    route=nvhost_cpp.split("case "+cmd+":",1)[1].split("case "+next_cmd+":",1)[0]
    assert "command.raw != "+raw in route
    assert "!input.empty() || !output.empty()" in route
    assert "NativeChannelSetEnabled(fd, "+state+")" in route
for raw,cmd,next_cmd in (
    ("0x00004810U","0x10","0x11"),
    ("0x00004811U","0x11","0x12"),
):
    route=nvhost_cpp.split("case "+cmd+":",1)[1].split("case "+next_cmd+":",1)[0]
    assert "command.raw != "+raw in route
    assert "return NvResult::NotImplemented;" in route
assert "bool native_channel_enabled{};" in nvhost_hdr
assert "SetNativeTsgChannelEnabled(DeviceFD fd, NvCore::SessionId session" in nvhost_hdr
set_direct=nvhost_cpp.split(
    "NvResult nvhost_gpu::NativeChannelSetEnabled(DeviceFD fd, bool enabled) {",1
)[1].split("\n}",1)[0]
for token in ("const std::scoped_lock lock{channel_mutex}",
              "if (!UsesNativeAbi(fd)) return NvResult::NotImplemented",
              "ValidateChannelOwner(fd, program_id)",
              "native_channel_enabled = enabled"):
    assert token in set_direct,token
set_tsg=nvhost_cpp.split(
    "NvResult nvhost_gpu::SetNativeTsgChannelEnabled(DeviceFD fd, NvCore::SessionId owner,",1
)[1].split("\n}",1)[0]
for token in ("channel->process != tsg->process",
              "!UsesNativeAbi(fd) || !NativeEpochCurrent(fd)",
              "native_channel_enabled = enabled"):
    assert token in set_tsg,token
# Native legacy ALLOC_GPFIFO_EX/EX2 cannot claim a configured channel from
# zero entries or from a count unsafe for the nvgpu ring-sizing calculation.
# Unlike S2 SETUP_BIND, the legacy ABI permits non-power-of-two counts.
legacy_count=(R/"encore-active/ns2-runtime/hle/core/hle/service/nvdrv/devices/native_gpu_submission.h").read_text()
assert "constexpr bool ValidNativeGpfifoLegacyAllocationCount(" in legacy_count
assert "entries != 0 && entries <= (1U << 28)" in legacy_count
# Recent Lichii/upstream GPFIFO hot-path optimization: the five native
# GPU submit dispatch routes (ordinary/kickoff plus S2 retry aliases) opt into
# 512 inline headers. All other NVDRV
# wrappers keep InputCapacity=0, while the copied CommandList avoids a
# redundant zero fill before every header is overwritten.
for token in ("using IoctlInputBuffer = std::conditional_t<",
              "boost::container::small_vector<T, Capacity>",
              "size_t InputCapacity = 0",
              "var_args.resize(num_var_args, boost::container::default_init)",
              "inl_in_var_args.resize(num_inl_in_var_args, boost::container::default_init)",
              "WrapGeneric<FixedArg, VarArg, Null, Null, InputCapacity>",
              "WrapGeneric<FixedArg, Null, InlInVarArg, Null, InputCapacity>"):
    assert token in ioctl_serialization,token
assert "constexpr size_t GpfifoInlineEntries = 512;" in nvhost_cpp
assert nvhost_cpp.count("WrapFixedVariable<GpfifoInlineEntries>(") == 3
assert nvhost_cpp.count("WrapFixedInlIn<GpfifoInlineEntries>(") == 2
assert nvhost_cpp.count(
    "entries.command_lists.resize(params.num_entries, boost::container::default_init);"
) == 2
for retry_opcode in ("0xC0004819U", "0xC018481CU"):
    assert retry_opcode in nvhost_cpp, retry_opcode

# Ampere setup-bind itself makes the channel runnable:
# nvgpu_channel_setup_bind -> ga10b_channel_bind -> nvgpu_channel_enable.
# Ounce relies on that and sends no explicit H:0x0E/TSG ENABLE before the
# first submit. Both legacy GPFIFO alloc entry points must therefore publish
# the native enabled state immediately after Encore's real InitChannel.
for name,next_name in (("AllocGPFIFOEx","AllocGPFIFOEx2"),
                       ("AllocGPFIFOEx2","GetObjectContextClassNumberIndex")):
    alloc=nvhost_cpp.split("NvResult nvhost_gpu::"+name+"(",1)[1].split(
        "NvResult nvhost_gpu::"+next_name if next_name=="AllocGPFIFOEx2" else
        "s32_le nvhost_gpu::GetObjectContextClassNumberIndex",1)[0]
    for token in ("system.GPU().InitChannel(*channel_state, program_id);",
                  "if (UsesNativeAbi(fd))",
                  "native_channel_enabled = true;"):
        assert token in alloc,(name,token)
    assert "ValidNativeGpfifoLegacyAllocationCount(" in alloc
    assert alloc.index("ValidNativeGpfifoLegacyAllocationCount(") < alloc.index(
        "if (channel_state->initialized)")
    assert alloc.index("ValidNativeGpfifoLegacyAllocationCount(") < alloc.index(
        "channel_state->program_id = program_id;")
    # AS BindChannel can legally follow a successful GPFIFO allocation.
    # The deferred branch must publish the NS2 logical runnable state even
    # though physical InitChannel is still gated on a real GMMU.
    deferred=alloc.split("if (!channel_state->memory_manager) {",1)[1].split(
        "system.GPU().InitChannel(*channel_state, program_id);",1)[0]
    for token in ("if (UsesNativeAbi(fd))",
                  "native_channel_enabled = true;",
                  "params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);",
                  "return NvResult::Success;"):
        assert token in deferred,(name,"deferred",token)
    assert deferred.index("native_channel_enabled = true;") < deferred.index(
        "params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);")
    # The immediate (already-bound) branch remains runnable after real init.
    immediate=alloc.split("system.GPU().InitChannel(*channel_state, program_id);",1)[1]
    assert immediate.index("native_channel_enabled = true;") < immediate.index(
        "native_gpfifo_configured = true;") < immediate.index(
        "params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);")
    assert deferred.index("native_channel_enabled = true;") < deferred.index(
        "native_gpfifo_configured = true;") < deferred.index(
        "params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);")
    assert "if (!channel_state->initialized || channel_state->bind_id < 0)" in immediate
    assert immediate.index("if (!channel_state->initialized || channel_state->bind_id < 0)") < (
        immediate.index("native_gpfifo_configured = true;"))

# CHANNEL_SETUP_BIND is a second native GPFIFO admission path. A
# successful InitChannel must mark it runnable before the first H:0x1B submit.
# Invalid or incomplete binding never publishes this enabled state.
setup_bind=nvhost_cpp.split("NvResult nvhost_gpu::ChannelSetupBind(",1)[1].split(
    "\n}",1)[0]
assert "if (!channel_state || !channel_state->memory_manager ||" in setup_bind
assert "if (channel_state->initialized) return NvResult::AlreadyAllocated;" in setup_bind
for token in ("system.GPU().InitChannel(*channel_state, program_id);",
              "if (!channel_state->initialized || channel_state->bind_id < 0)",
              "return NvResult::InvalidState;",
              "native_channel_enabled = true;",
              "params.work_submit_token = 0;"):
    assert token in setup_bind,token
assert setup_bind.index("system.GPU().InitChannel(*channel_state, program_id);") < setup_bind.index(
    "if (!channel_state->initialized || channel_state->bind_id < 0)") < setup_bind.index(
    "native_channel_enabled = true;") < setup_bind.index(
    "native_gpfifo_configured = true;") < setup_bind.index(
    "params.work_submit_token = 0;")

submit_impl=nvhost_cpp.split("NvResult nvhost_gpu::SubmitGPFIFOImpl(",1)[1].split(
    "\n}",1)[0]
assert "if (!native_channel_enabled)" in submit_impl
assert "reason=channel-disabled" in submit_impl
assert "bool native_gpfifo_configured{};" in nvhost_hdr
assert "if (!native_gpfifo_configured)" in submit_impl
assert submit_impl.index("if (!native_channel_enabled)") < submit_impl.index(
    "if (!native_gpfifo_configured)") < submit_impl.index(
    "for (const auto& [other_fd, session_id] : sessions)")

# OunceVK configures both a 3D and a copy /dev/nvhost-gpu channel and
# calls nvQueryEvent(fd, 7) for each. Module::QueryEvent must therefore retain
# the descriptor through virtual dispatch; a device-wide sessions.size()==1
# gate rejects the second valid channel before its first submit.
module_query=(R/"sources/eden/src/core/hle/service/nvdrv/nvdrv.cpp").read_text()
nvdevice=(R/"sources/eden/src/core/hle/service/nvdrv/devices/nvdevice.h").read_text()
assert "virtual Kernel::KEvent* QueryEvent(DeviceFD fd, u32 event_id)" in nvdevice
fd_fallback=nvdevice.split(
    "virtual Kernel::KEvent* QueryEvent(DeviceFD fd, u32 event_id) {",1
)[1].split("\n    }",1)[0]
assert "(void)fd;" in fd_fallback
assert "return QueryEvent(event_id);" in fd_fallback
module_query_body=module_query.split(
    "NvResult Module::QueryEvent(DeviceFD fd, u32 event_id, Kernel::KEvent*& event) {",1
)[1].split("\n}",1)[0]
assert "itr->second->QueryEvent(fd, event_id)" in module_query_body
assert "itr->second->QueryEvent(event_id)" not in module_query_body
assert "Kernel::KEvent* QueryEvent(DeviceFD fd, u32 event_id) override;" in nvhost_hdr
fd_event=nvhost_cpp.split(
    "Kernel::KEvent* nvhost_gpu::QueryEvent(DeviceFD fd, u32 event_id) {",1
)[1].split("\n}",1)[0]
for token in ("if (event_id == 7)", "UsesNativeAbi(fd)",
              "error_notifier_event", "return QueryEvent(event_id);"):
    assert token in fd_event,token
assert "sessions.size() == 1" not in fd_event
legacy_event=nvhost_cpp.split(
    "Kernel::KEvent* nvhost_gpu::QueryEvent(u32 event_id) {",1
)[1].split(
    "Kernel::KEvent* nvhost_gpu::QueryEvent(DeviceFD fd, u32 event_id)",1
)[0]
for token in ("case 1:", "case 2:", "case 3:"):
    assert token in legacy_event,token
assert "case 7:" not in legacy_event

# Ounce S2 channel EventIdControl is input-only 0x40104812 and carries
# {u32 cmd, u32 QueryEvent id, u64 zero}. The real setup path enables id=2.
# SwitchBrew exposes exactly the three /dev/nvhost-gpu QueryEvent ids 1..3.
# EventIdControl rejects all other ids rather than inventing extra producers.
assert "struct IoctlNativeEventIdControl" in nvhost_hdr
assert "u64_le reserved{}; // Ounce S2 wrapper sends zero" in nvhost_hdr
assert 'static_assert(sizeof(IoctlNativeEventIdControl) == 16' in nvhost_hdr
assert "u32 native_event_enable_mask{};" in nvhost_hdr
event_route=nvhost_cpp.split(
    "case 0x12: { // Switch 2 NVGPU_IOCTL_CHANNEL_EVENT_ID_CONTROL",1
)[1].split("case 0x16:",1)[0]
for token in ("command.raw != 0x40104812U",
              "input.size() != sizeof(IoctlNativeEventIdControl)",
              "!output.empty()",
              "if (!UsesNativeAbi(fd)) return NvResult::NotImplemented",
              "NativeEventIdControl, input, output, fd"):
    assert token in event_route,token
event_ctrl=nvhost_cpp.split(
    "NvResult nvhost_gpu::NativeEventIdControl(IoctlNativeEventIdControl& params, DeviceFD fd) {",1
)[1].split("\n}",1)[0]
for token in ("ValidateChannelOwner(fd, program_id)",
              "cmd > 2 || event_id == 0 || event_id > 3",
              "static_cast<u64>(params.reserved) != 0",
              "case 1:", "sm_exception_breakpoint_int_report_event",
              "case 2:", "sm_exception_breakpoint_pause_report_event",
              "case 3:", "error_notifier_event",
              "native_event_enable_mask &= ~bit",
              "native_event_enable_mask |= bit",
              "GetReadableEvent().Clear(system.Kernel())"):
    assert token in event_ctrl,token
assert "default:\n        // Rejected by the public QueryEvent-id bound above.\n        return NvResult::BadParameter;" in event_ctrl
assert event_ctrl.index("case 2:") < event_ctrl.index(
    "sm_exception_breakpoint_pause_report_event")
assert event_ctrl.count("return NvResult::Success;") == 1

# OunceVK installs H:0x0C before preemption/GPFIFO2. Nintendo's
# channel ABI intentionally does not expose Linux's dma-buf notifier:
# mem!=0 enables internal state, mem==0 disables it, and offset/size are
# ignored. H:0x16/H:0x17 fetch the current error snapshots.
assert 'case 0xc:' in nvhost_cpp
notifier_route=nvhost_cpp.split("case 0xc:",1)[1].split("case 0xd:",1)[0]
assert "SetErrorNotifier, input, output, fd" in notifier_route
notifier=nvhost_cpp.split(
    "NvResult nvhost_gpu::SetErrorNotifier(IoctlSetErrorNotifier& params, DeviceFD fd) {",1
)[1].split("\n}",1)[0]
for token in ("if (!UsesNativeAbi(fd))",
              "ValidateChannelOwner(fd, program_id)",
              "native_error_notifier_initialized = static_cast<u32>(params.mem) != 0",
              "native_error_info = {}",
              "native_error_notification = {}",
              "requires status==0 for a healthy channel",
              "error_notifier_event->GetReadableEvent().Clear(system.Kernel())"):
    assert token in notifier,token
for forbidden in ("nvmap.GetHandle(params.mem)", "handle->Duplicate(true)",
                  "memory.WriteBlock", "native_error_notifier_handle",
                  "params.offset) != 0", "params.size) != 0"):
    assert forbidden not in notifier,forbidden

assert "struct IoctlGetErrorInfo" in nvhost_hdr
assert "std::array<u32_le, 31> info{};" in nvhost_hdr
assert 'static_assert(sizeof(IoctlGetErrorInfo) == 128' in nvhost_hdr
assert "0=healthy; non-zero reports a channel fault" in nvhost_hdr
assert "bool native_error_notifier_initialized{};" in nvhost_hdr
assert "IoctlGetErrorInfo native_error_info{};" in nvhost_hdr
assert "IoctlGetErrorNotification native_error_notification{};" in nvhost_hdr
assert "std::optional<u32> native_error_notifier_handle{};" not in nvhost_hdr

health_info_route=nvhost_cpp.split(
    "case 0x16: { // Switch NVGPU_IOCTL_CHANNEL_GET_ERROR_INFO",1
)[1].split("case 0x17:",1)[0]
for token in ("command.raw != 0x80804816U", "!input.empty()",
              "output.size() != sizeof(IoctlGetErrorInfo)",
              "if (!UsesNativeAbi(fd)) return NvResult::NotImplemented",
              "GetErrorInfo, input, output, fd"):
    assert token in health_info_route,token

health_route=nvhost_cpp.split(
    "case 0x17: { // Switch NVGPU_IOCTL_CHANNEL_GET_ERROR_NOTIFICATION",1
)[1].split("case 0x18:",1)[0]
for token in ("command.raw != 0xC0104817U",
              "sizeof(IoctlGetErrorNotification)",
              "if (!UsesNativeAbi(fd)) return NvResult::NotImplemented",
              "GetErrorNotification, input, output, fd"):
    assert token in health_route,token

health_info=nvhost_cpp.split(
    "NvResult nvhost_gpu::GetErrorInfo(IoctlGetErrorInfo& params, DeviceFD fd) {",1
)[1].split("\n}",1)[0]
health=nvhost_cpp.split(
    "NvResult nvhost_gpu::GetErrorNotification(IoctlGetErrorNotification& params, DeviceFD fd) {",1
)[1].split("\n}",1)[0]
for body in (health_info,health):
    assert "ValidateChannelOwner(fd, program_id)" in body
    assert "if (!native_error_notifier_initialized)" in body
    assert "return NvResult::InvalidState;" in body
assert "params = native_error_info;" in health_info
assert "params = native_error_notification;" in health
# OunceVK checks only H:0x17 status for channel health. A title-wide fatal
# GPU latch therefore maps to NVIDIA's generic robust-channel ERROR status,
# without fabricating a firmware-specific info32/type classification.
for token in ("::Eden::PeekGpuFailure()",
              "constexpr u16 RobustChannelErrorStatus = 0xFFFF;",
              "native_error_notification.status = RobustChannelErrorStatus;",
              "error_notifier_event->Signal(system.Kernel());"):
    assert token in health,token
assert health.index("::Eden::PeekGpuFailure()") < health.index(
    "native_error_notification.status = RobustChannelErrorStatus;") < health.index(
    "params = native_error_notification;")
assert "native_error_notification.info32 =" not in health
assert "native_error_info.type =" not in health
assert "memory.ReadBlock" not in health

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

# Worker 211 fixed VA ownership must be exception-safe under host OOM.
# FlatAddressSpaceMap can split both ends and therefore may need two new
# vector blocks; reserve them before mutating the map/cursor in every
# allocator entry point used by NS2 mapping/retirement.
_reserve = "this->blocks.reserve(this->blocks.size() + 2);"
_alloc_body = allocator_cpp.split("ALLOC_MEMBER(VaType)::Allocate(VaType size) {", 1)[1].split(
    "ALLOC_MEMBER(void)::AllocateFixed(", 1)[0]
_fixed_body = allocator_cpp.split(
    "ALLOC_MEMBER(void)::AllocateFixed(VaType virt, VaType size) {", 1)[1].split(
    "ALLOC_MEMBER(void)::Free(", 1)[0]
_free_body = allocator_cpp.split(
    "ALLOC_MEMBER(void)::Free(VaType virt, VaType size) {", 1)[1].split(
    "} // namespace Common", 1)[0]
assert _reserve in _alloc_body
assert _alloc_body.index(_reserve) < _alloc_body.index("current_linear_alloc_end =")
assert _alloc_body.index(_reserve) < _alloc_body.index("MapLocked(")
assert _reserve in _fixed_body
assert _fixed_body.index(_reserve) < _fixed_body.index("MapLocked(")
assert _reserve in _free_body
assert _free_body.index(_reserve) < _free_body.index("UnmapLocked(")

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

print("NS2_GPU_IDENTITY_MAPPING_SOURCE_PASS sub101=identity replay=as_import vm_id=opaque sub102=all_owned_unmap as_fd_close=nvdrv_owned session=retained retire=gpu-worker shutdown=guarded")


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

# Early NVDRV FD close can arrive before any GPU bind; never compare an
# indeterminate current_channel pointer against the retiring owner.
assert "Tegra::Control::ChannelState* current_channel{};" in gpu_cpp
assert "s32 bound_channel{-1};" in gpu_cpp
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
assert "if (current_channel == owner.get()) {" in release
assert "current_channel = nullptr;" in release
assert "bound_channel = -1;" in release
print("NS1_PREBIND_GPU_CLOSE_SOURCE_PASS initialized_pointer=1 unbound_close=1")

# Final-FD retirement must never prevent Module::Close from erasing the FD,
# and the destructor fallback must remain noexcept in practice. A failed GPU
# worker is already a latched fatal condition; record it and continue cleanup.
module_cpp=(R/"sources/eden/src/core/hle/service/nvdrv/nvdrv.cpp").read_text()
on_close=nvhost_cpp.split("void nvhost_gpu::OnClose(DeviceFD fd) {",1)[1].split("\n}\n",1)[0]
dtor=nvhost_cpp.split("nvhost_gpu::~nvhost_gpu() {",1)[1].split("\n}\n",1)[0]
for block in (on_close,dtor):
    assert "try {" in block
    assert "system.GPU().ReleaseChannel(*channel_state);" in block
    assert "catch (...) {" in block
    assert "::Eden::RecordGpuFailure(std::current_exception());" in block
assert on_close.index("sessions.erase(fd);") < on_close.index("try {")
close_body=module_cpp.split("NvResult Module::Close(DeviceFD fd) {",1)[1].split(
    "\n}\n",1)[0]
assert close_body.index("try {") < close_body.index("itr->second->OnClose(fd);")
assert close_body.count("open_files.erase(itr);") >= 2
catch_tail=close_body.split("catch (...) {",1)[1]
assert catch_tail.index("open_files.erase(itr);") < catch_tail.index("throw;")
assert close_body.index("itr->second->OnClose(fd);") < close_body.rindex(
    "open_files.erase(itr);")
assert '#include "gpu_failure.h"' in nvhost_cpp
print("NS1_NVDRV_CLOSE_EXCEPTION_SAFE_PASS final_fd=1 destructor_noexcept=1 "
      "failure_latched=1 module_erase_reachable=1 generic_close_rollback=1")

# Module::Open publishes builder-owned devices before device-specific OnOpen.
# A thrown OnOpen must erase that published FD for both known and stub devices.
open_body=module_cpp.split(
    "DeviceFD Module::Open(const std::string& device_name, NvCore::SessionId session_id) {",1)[1].split(
    "\n}\n\nNvResult Module::Ioctl1",1)[0]
assert open_body.count("device->OnOpen(session_id, fd);") == 2
assert open_body.count("catch (...) {") >= 2
assert open_body.count("open_files.erase(fd);") >= 2
for fragment in open_body.split("device->OnOpen(session_id, fd);")[1:]:
    assert "open_files.erase(fd);" in fragment.split("return fd;",1)[0]
print("NS1_NVDRV_OPEN_ROLLBACK_SOURCE_PASS known_device=1 stub_device=1 "
      "failed_onopen_unpublished=1")




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


# Worker 211 overlays one 214 reservation without taking a second VA owner.
# 216 remains physically exclusive and 213 restores sparse PTE state.
_overlay_worker = worker.split("void IGpuWorker::NormalMapGpuMemory4K(",1)[1].split(
    "void IGpuWorker::CreateSparseRegion(",1)[0]
for marker in (
    "m_mappings.SparseReservationPageShift(",
    "auto& ledger = sparse_overlay ? m_sparse_overlays : m_mappings;",
    "request->cache != 0",
    "m_sparse_overlays.CanModify(*request)",
    "m_sparse_overlays.CanUnmap(*request)",
):
    assert marker in _overlay_worker, marker
assert "GpuWorkerProtocol::MappingLedger m_sparse_overlays;" in worker_hdr
assert "SparseReservationPageShift(" in worker_protocol

_overlay_as = as_cpp.split("NvResult nvhost_as_gpu::MapNative4K(",1)[1].split(
    "\nNvResult nvhost_as_gpu::MapNativeImage(",1)[0]
for marker in (
    "bool sparse_overlay = false;",
    "native_sparse_mappings.Ranges()",
    "if (current.handle) return NvResult::BadParameter;",
    "Mapping(handle_id, pinned + source, address, size,",
    "true, false, sparse_overlay, gpu_cacheable)",
    "gmmu->MapSparse(address, size, false);",
):
    assert marker in _overlay_as, marker
_overlay_unmap = as_cpp.split("NvResult nvhost_as_gpu::UnmapNative4K(",1)[1].split(
    "\nNvResult nvhost_as_gpu::ModifyNativeKind(",1)[0]
assert "if (mapping->second.sparse_alloc)" in _overlay_unmap
assert _overlay_unmap.index("gmmu->MapSparse(address, size, false);") < _overlay_unmap.index(
    "nvmap.UnpinHandle(mapping->second.handle);")
assert "bool gpu_cacheable : 1;" in as_h
print("NS2_WORKER_211_SPARSE_OVERLAY_SOURCE_PASS cacheability=retained "
      "214_va_owner=preserved 216_exclusive=1 213_restores_sparse=1")

# Worker teardown cannot drop physical backing after a failed drain or identity unmap.
assert "catch (...)" in teardown.split("system.GPU().WaitForIdle();", 1)[1].split("// A failed native", 1)[0]
assert "::Eden::RecordGpuFailure(std::current_exception());" in teardown
identity_teardown = teardown.split("for (const u32 memory_id : m_memory)",1)[1].split("// The sparse reservations",1)[0]
assert "if (!as || as->UnmapNative4K" in identity_teardown
assert identity_teardown.index("m_quarantined_nv_session = true;") < identity_teardown.index("identity->RemoveSpace(as_id);")
assert "R_UNLESS(!m_quarantined_nv_session, Kernel::ResultInvalidState);" in worker.split("Result IGpuWorker::ReleaseMemory",1)[1].split("void IGpuWorker::CreateGpuAccessibleMemory",1)[0]

# Exclusive native worker free performs its final ownership check under NvMap's
# real handle mutex. The stale shared_ptr cannot pin after retirement.
_nvmap_native = (R/"encore-active/eden-core/src/core/hle/service/nvdrv/core/nvmap.cpp").read_text()
_free_impl = _nvmap_native.split("NvMap::FreeHandleImpl(",1)[1].split("void NvMap::UnmapAllHandles",1)[0]
assert _free_impl.index("std::scoped_lock lock(handle_description->mutex);") < _free_impl.index("handle_description->pins != 0") < _free_impl.index("handle_description->native_retired = true;") < _free_impl.index("--handle_description->dupes")
assert "if (handle_description->native_retired) return 0;" in _nvmap_native
assert "if (!allocated || native_retired)" in _nvmap_native
assert "return FreeHandleImpl(handle, internal_session, false);" in _nvmap_native
assert "nvmap.FreeHandleIfUnpinned(id)" in teardown
assert "nvmap.FreeHandleIfUnpinned(region.nvmap_handle)" in worker
assert "nvmap.FreeHandleIfUnpinned(id)" in worker.split("Result IGpuWorker::ReleaseMemory",1)[1].split("void IGpuWorker::CreateGpuAccessibleMemory",1)[0]
