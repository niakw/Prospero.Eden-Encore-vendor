#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
cpp=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_session.cpp").read_text()
hdr=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_session.h").read_text()
proto=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/gpu_session_protocol.h").read_text()
for marker in (
    '{200, &IGpuSession::SynchronizeAddressSpace, "SynchronizeGpuAddressSpace"}',
    '{201, &IGpuSession::GetUserPartition, "GetUserPartition"}',
    '{202, &IGpuSession::GetGsvPartition, "GetGsvPartition"}',
    '{203, &IGpuSession::GetVaRange, "GetVaRange"}',
):
    assert marker in cpp, marker
# Lichii nx2 GSV: TIPC 0 is a real token-echo handshake before graphics init.
# A missing handler in the PS5-selected session returned NotImplemented and
# could stop newer SDKs before a single NVDRV GPFIFO or guest frame.
worker=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.cpp").read_text()
worker_hdr=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.h").read_text()
for owner, owner_hdr, klass in ((cpp,hdr,"IGpuSession"),(worker,worker_hdr,"IGpuWorker")):
    assert '{0, &'+klass+'::Handshake, "Handshake"}' in owner
    assert "void Handshake(HLERequestContext& ctx);" in owner_hdr
    sig="void "+klass+"::Handshake(HLERequestContext& ctx) {"
    assert owner.count(sig)==1
    body=owner.split(sig,1)[1].split("\n}\n",1)[0]
    for needle in ("ctx.CommandBuffer()", "IPC::ResponseBuilder rb{ctx, 4};",
                   "rb.PushRaw<u32>(ResultSuccess.raw);",
                   "const u32 token_lo = command[2];",
                   "const u32 token_hi = command[3];",
                   "rb.PushRaw<u32>(token_lo);",
                   "rb.PushRaw<u32>(token_hi);"):
        assert needle in body, (klass,needle)
    # The production ResponseBuilder constructor clears ctx.CommandBuffer().
    # Reading token words after it is constructed silently echoes zero.
    assert body.index("command[2]") < body.index("command[3]")
    assert body.index("const u32 token_lo = command[2];") < body.index("IPC::ResponseBuilder rb{ctx, 4};")
    assert body.index("const u32 token_hi = command[3];") < body.index("IPC::ResponseBuilder rb{ctx, 4};")
    assert body.index("IPC::ResponseBuilder rb{ctx, 4};") < body.index("rb.PushRaw<u32>(token_lo);")
    ipc=(R/"encore-active/eden-core/include/core/hle/service/ipc_helpers.h").read_text()
    assert "std::memset(cmdbuf, 0, sizeof(u32) * IPC::COMMAND_BUFFER_LENGTH);" in ipc
    assert "return ServiceFrameworkBase::HandleSyncRequest(session, ctx);" in owner
    assert "handlers_tipc.contains(ctx.GetCommand())" in owner
# Lichii nx2 INx2GpuSession declares nine opaque commands as zeroed
# stubs. Encore must recognize their exact IDs in the native TIPC table,
# but keep the response fail-closed until their payload ABI is known.
opaque_ids = (23, 24, 25, 34, 51, 8001, 8002, 8003, 8004)
for cmd in opaque_ids:
    marker = f'{{{cmd}, &IGpuSession::RejectOpaqueNx2Command, "Unknown{cmd}"}}'
    assert cpp.count(marker) == 1, marker
assert "void RejectOpaqueNx2Command(HLERequestContext& ctx);" in hdr
opaque_body = cpp.split(
    "void IGpuSession::RejectOpaqueNx2Command(HLERequestContext& ctx) {",1
)[1].split("\n}",1)[0]
assert "IPC::ResponseBuilder rb{ctx, 2};" in opaque_body
assert "rb.Push(Kernel::ResultNotImplemented.raw);" in opaque_body
assert "ResultSuccess" not in opaque_body
assert "ctx.CommandBuffer()" not in opaque_body
assert "ctx.AddMoveObject(" not in opaque_body

# libounce current GSV ABI requests HIPC MOVE handles for GPU-main
# command 12 (slot event) and command 22 (worker KClientSession). Retain one
# service-owned reference because Encore still signals/closes these objects.
get_event=cpp.split("void IGpuSession::GetNotificationEvent(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nstd::shared_ptr<Nvidia::Devices::nvhost_as_gpu>",1)[0]
for needle in (
    "if (result.IsSuccess() && !readable->Open(kernel))",
    "IPC::ResponseBuilder rb{ctx, 2, 0, 1};",
    "ctx.AddMoveObject(readable);",
):
    assert needle in get_event, needle
assert "ctx.AddCopyObject(readable);" not in get_event
assert "IPC::ResponseBuilder rb{ctx, 2, 1, 0};" not in get_event
for token in ("catch (const std::bad_alloc&)", "readable->Close(kernel);",
              "result = Kernel::ResultOutOfResource;"):
    assert token in get_event, ("notification MOVE OOM rollback",token)
assert get_event.index("ctx.AddMoveObject(readable);") < get_event.index(
    "readable->Close(kernel);") < get_event.index(
    "IPC::ResponseBuilder rb{ctx, 2, 0, 1};")
acquire=cpp.split("void IGpuSession::AcquireWorker(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nvoid IGpuSession::GetNotificationRange",1)[0]
for needle in (
    "if (m_worker->Open(kernel))",
    "IPC::ResponseBuilder rb{ctx, 2, 0, 1};",
    "ctx.AddMoveObject(m_worker);",
):
    assert needle in acquire, needle
assert "ctx.AddCopyObject(m_worker)" not in acquire
for token in ("catch (const std::bad_alloc&)", "m_worker->Close(kernel);",
              "result = Kernel::ResultOutOfResource;"):
    assert token in acquire, ("worker MOVE OOM rollback",token)
assert acquire.index("ctx.AddMoveObject(m_worker);") < acquire.index(
    "m_worker->Close(kernel);") < acquire.index(
    "IPC::ResponseBuilder rb{ctx, 2, 0, 1};")
assert acquire.index("m_worker->Open(kernel)") < acquire.index("ctx.AddMoveObject(m_worker)")
# HLE MOVE inserts the guest handle then consumes exactly one transferred ref.
hle=(R/"sources/eden/src/core/hle/service/hle_ipc.cpp").read_text()
assert "for (auto& object : outgoing_move_objects)" in hle
assert "R_TRY(handle_table.Add(kernel, &handle, object));" in hle
assert "object->Close(kernel);" in hle

# GPU-session TIPC 200 is the SDK's address-space job barrier. The worker's
# GMMU mutations are synchronous here, so completion signals the existing
# notification event without draining unrelated graphics work.
assert "void SynchronizeAddressSpace(HLERequestContext& ctx);" in hdr
assert "DecodeSynchronizeAddressSpace" in proto
for needle in ("packet.size() != 4", "packet[0] != 0xd8", "packet[1] != 2",
               "packet[2] == 0", "packet[3] >= MaxNotificationEvents"):
    assert needle in proto, needle
sync=cpp.split("void IGpuSession::SynchronizeAddressSpace(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nvoid IGpuSession::GetUserPartition",1)[0]
for needle in ("m_shared->address_spaces.Owns(request->address_space)",
               "ResolveOpenedAddressSpace(request->address_space)",
               "m_shared->events[request->notification]",
               "m_shared->events[request->notification]->Signal(kernel)",
               "result = ResultSuccess"):
    assert needle in sync, needle
assert "WaitForIdle" not in sync
assert "GetAddressSpacePartition" not in cpp
assert "GetAddressSpacePartition" not in hdr
for cmd in ("201","202","203"):
    assert f"DecodeAddressSpacePartition(command, {cmd})" in cpp
assert "command != 201 && command != 202 && command != 203" in proto
assert "first = Nvidia::Devices::NativeNs2GpuVaStart;" in cpp
assert cpp.count("size = Nvidia::Devices::NativeNs2GpuVaSmallSplit - first;") == 1
# NVIDIA VA page-table geometry can cover 1 TiB, while NVN's reported
# user region ends at 768 GiB. Keep those distinct exactly as the
# reviewed Lichii upstream reports GetUserPartition/GetVaRange.
layout=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/native_gpu_va_layout.h").read_text()
as_h=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.h").read_text()
as_cpp=(R/"encore-active/ns2-runtime/nvdrv_as/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
registry=(R/"encore-active/ns2-runtime/hle/core/hle/service/nvdrv/devices/native_gpu_process_registry.h").read_text()
assert "NativeNs2GpuVaSmallSplit = 3ULL << 38;" in layout
assert "NativeNs2GpuVaEnd = 1ULL << 40;" in layout
assert "0xC000000000" in cpp

# libounce cpu_window.c: 33 creates an opaque vm_id; 203/202 describe it;
# root 434 registers it; only then does an independently opened NVDRV AS
# descriptor import that id with 0x40284109.
create=cpp.split("void IGpuSession::CreateAddressSpace(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nvoid IGpuSession::SynchronizeAddressSpace",1)[0]
for marker in ("NativeGpuProcessRegistry::Instance().AllocateVm(",
               "m_shared->address_spaces.Add(*candidate)",
               "NativeGpuProcessRegistry::Instance().RemoveVm(",
               "id = *candidate;"):
    assert marker in create,marker
for forbidden in ('m_module->Open("/dev/nvhost-as-gpu"', "m_module->Ioctl1(",
                  "static_cast<u32>(fd)", "AllocAsEx", "MapNative4K"):
    assert forbidden not in create,("cmd33 must stay VM-only",forbidden)

register_vm=cpp.split("Result IGpuSession::RegisterAddressSpace(u32 id) {",1)[1].split("\n}",1)[0]
assert "NativeGpuProcessRegistry::Instance().RegisterVm(m_process, id)" in register_vm
assert "m_shared->address_spaces.Register(id)" in register_vm
for command,name,next_name in ((201,"GetUserPartition","GetGsvPartition"),
                               (202,"GetGsvPartition","GetVaRange"),
                               (203,"GetVaRange","CreateGraphicsDevice")):
    body=cpp.split(f"void IGpuSession::{name}(HLERequestContext& ctx) {{",1)[1].split(
        f"\n}}\n\nvoid IGpuSession::{next_name}",1)[0]
    assert "m_shared->address_spaces.Owns(request->address_space)" in body,(command,name)
    assert "ResolveOpenedAddressSpace" not in body,(command,"pre-import query")

for marker in ("struct NativeVmImport {","u32_le version{};","u32_le vm_id{};",
               "u64_le user_first{};","u64_le user_end{};",
               'static_assert(sizeof(NativeVmImport) == 40'):
    assert marker in as_h,marker
case9=as_cpp.split("case 0x9: {",1)[1].split("case 0xa: {",1)[0]
for marker in ("command.raw == 0x40284109U","sizeof(NativeVmImport)",
               "ImportNativeVm(parameters)"):
    assert marker in case9,marker
native_import=as_cpp.split("NvResult nvhost_as_gpu::ImportNativeVm(const NativeVmImport& params) {",1)[1].split("\n}",1)[0]
for marker in ("u32{params.version} != 5","u32{params.vm_id} == 0",
               "u64{params.user_first} != NativeNs2GpuVaStart",
               "u64{params.user_end} != NativeNs2GpuVaSmallSplit",
               "FindRegisteredVmOwner(","layout.big_page_size = 0x20000;",
               "layout.va_range_start = NativeNs2GpuVaStart;",
               "layout.va_range_end = NativeNs2GpuVaEnd;",
               "layout.va_range_split = NativeNs2GpuVaSmallSplit;",
               "AllocAsEx(layout)","BindImportedAddressSpace(",
               "m_native_vm_id = params.vm_id;"):
    assert marker in native_import,marker
assert native_import.index("FindRegisteredVmOwner(") < native_import.index("AllocAsEx(layout)") < native_import.index("BindImportedAddressSpace(") < native_import.index("m_native_vm_id = params.vm_id;")
for marker in ("AllocateVm(","RegisterVm(","FindRegisteredVmOwner(","RemoveVm("):
    assert marker in registry,marker
assert "first = ~u64{0};" in cpp and "last = 0;" in cpp
get_ranges=cpp.split("void IGpuSession::GetVaRange(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nvoid IGpuSession::CreateGraphicsDevice",1)[0]
for marker in (
    "std::array<u64_le, 9> payload{};",
    "payload[0] = 1;",
    "payload[1] = Nvidia::Devices::NativeNs2GpuVaStart;",
    "payload[2] = Nvidia::Devices::NativeNs2GpuVaSmallSplit - 1;",
    "IPC::ResponseBuilder rb{ctx, 20};",
    "rb.PushRaw(payload);",
):
    assert marker in get_ranges, marker
assert "u64 kind{};" not in get_ranges
assert "rb.Push(kind);" not in get_ranges
# The obsolete nine-u64 GetVARegions reply was a shifted older contract.
assert "std::array<u64, 9> values" not in cpp
print("NS2_GPU_PARTITION_NEW_SDK_SOURCE_PASS commands=200,201,202,203 vm_id=opaque import=40284109 sync_event=owned no_global_idle=1")
