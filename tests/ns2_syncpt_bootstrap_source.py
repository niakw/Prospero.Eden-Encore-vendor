#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
cpp=(R/"encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.cpp").read_text()
hdr=(R/"encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.h").read_text()
host=(R/"sources/eden/src/video_core/host1x/syncpoint_manager.h").read_text()
proto=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/graphics_supervisor_protocol.h").read_text()
for marker in (
    '{202, &IGraphicsSupervisor::OpenSyncptSession, "OpenSyncptSession"}',
    '{204, &IGraphicsSupervisor::AttachSyncptMemory, "AttachSyncptMemory"}',
    '{0, &INativeSyncptSession::Handshake, "Handshake"}',
    '{0,&IVi2Session::Handshake,"Handshake"}',
    '{103, &INativeSyncptSession::QueryCounter, "QueryCounter"}',
    '{201, &INativeSyncptSession::RequestSlotEvent, "RequestSlotEvent"}',
    '{202, &INativeSyncptSession::CancelSlotEvent, "CancelSlotEvent"}',
):
    assert marker in cpp, marker
assert "NativeSyncptState" in hdr
# The standard nn::gs boot runs graphics-frame management before the
# GPU worker boot. Commands 1101/1102 are an ordered context state, while
# 1111-1113 admit only SDK-defined policy values after initialization.
for marker in (
    '{1101, &IGraphicsSupervisor::ConfigureGraphicsFrameContext, "CreateGraphicsFrameContext"}',
    '{1102, &IGraphicsSupervisor::ConfigureGraphicsFrameContext, "InitializeGraphicsFrameContext"}',
    '{1111, &IGraphicsSupervisor::SetGraphicsFrameMode, "SetGraphicsFrameLengthMode"}',
    '{1112, &IGraphicsSupervisor::SetGraphicsFrameMode, "SetGraphicsFrameRestartMode"}',
    '{1113, &IGraphicsSupervisor::SetGraphicsFrameMode, "SetGraphicsFrameGpuTimesliceMode"}',
):
    assert marker in cpp, marker
for needle in (
    "IsGraphicsFrameContextCommand(command, command_id)",
    "m_bootstrap.CurrentPhase() == GB::Phase::Ready",
    "command_id == 1101 && !m_graphics_frame_created",
    "command_id == 1102 && m_graphics_frame_created",
    "!m_graphics_frame_initialized",
    "DecodeGraphicsFrameSetting(command, command_id)",
    "if (m_graphics_frame_initialized)",
    "m_graphics_frame_length = setting->value",
    "m_graphics_frame_restart_mode = setting->value",
    "m_graphics_frame_timeslice_mode = setting->value",
):
    assert needle in cpp, needle
for needle in (
    "command == 1101 || command == 1102",
    "value != 0 && value != 286 && value != 316 && value != 376",
    "command == 1112 && value >= 3",
    "command == 1113 && value >= 2",
):
    assert needle in proto, needle
frame_handler=cpp.split("void IGraphicsSupervisor::SetGraphicsFrameMode(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nvoid IGraphicsSupervisor::CreateNativeMemory",1)[0]
assert "WaitForIdle" not in frame_handler

# Native memory command 113 consumes a real process-owned TransferMemory.
# It must retain that kernel object instead of trying to stack a second
# DeviceShared lock on pages already locked by KTransferMemory.
assert '{113, &IGraphicsSupervisor::BindNativeTransferMemory, "CreateNativeMemoryWithTransferMemory"}' in cpp
assert 'void BindNativeTransferMemory(HLERequestContext& ctx);' in hdr
for needle in ("packet.size() != 9", "packet[0] != 0x81",
               "packet[1] != 0x80000005U", "packet[2] != 2",
               "packet[3] == 0", "packet[4] == 0"):
    assert needle in proto, needle
bind113=cpp.split("void IGraphicsSupervisor::BindNativeTransferMemory(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nResult IGraphicsSupervisor::BindNativeMemoryBacking",1)[0]
for needle in (
    "request->copied_handle != copy_handle",
    "transfer->GetOwner() != m_process",
    "GetInteger(transfer->GetSourceAddress()) != request->address",
    "transfer->GetSize() != request->size",
    "m_native_memory.ValidateBind(request->memory",
    "m_native_transfer_backings.try_emplace(request->memory, nullptr)",
    "transfer->Open(kernel);",
    "handle->Alloc(",
    "m_native_memory.Bind(request->memory",
):
    assert needle in bind113, needle
assert "LockForMapDeviceAddressSpace" not in bind113
assert "UnlockForDeviceAddressSpace" not in bind113
assert "std::map<u32, Kernel::KTransferMemory*> m_native_transfer_backings;" in hdr
# GSV 498 is the SDK's exact donated-work-buffer usage query: no input and
# three u64 outputs. The boot KTransferMemory must outlive the IPC handle.
assert '{498, &IGraphicsSupervisor::QueryDonatedTransferMemoryUsage, "TryQueryDonatedTransferMemoryUsage"}' in cpp
assert 'void QueryDonatedTransferMemoryUsage(HLERequestContext& ctx);' in hdr
assert 'Kernel::KTransferMemory* m_bootstrap_transfer_memory{};' in hdr
assert 'u64 m_bootstrap_transfer_memory_size{};' in hdr
init=cpp.split("void IGraphicsSupervisor::Initialize(HLERequestContext& ctx) {",1)[1].split(
    "\n}\nvoid IGraphicsSupervisor::BindProcess",1)[0]
for needle in (
    "donated_transfer = memory.GetPointerUnsafe();",
    "donated_size = request->size;",
    "donated_transfer->Open(kernel);",
    "m_bootstrap_transfer_memory = donated_transfer;",
    "m_bootstrap_transfer_memory_size = donated_size;",
):
    assert needle in init, needle
query498=cpp.split("void IGraphicsSupervisor::QueryDonatedTransferMemoryUsage(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nstatic Result NativeMemoryError",1)[0]
for needle in (
    "packet[0] != 16 + 498 || packet[1] != 0",
    "!m_bootstrap_transfer_memory || m_bootstrap_transfer_memory_size == 0",
    "static_cast<u32>(m_bootstrap_transfer_memory_size)",
    "static_cast<u32>(m_bootstrap_transfer_memory_size >> 32)",
    "0, 0, 0, 0",
):
    assert needle in query498, needle
assert "m_bootstrap_transfer_memory->Close(kernel);" in cpp
destroy=cpp.split("void IGraphicsSupervisor::DestroyNativeMemory(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nvoid IGraphicsSupervisor::BindNativeTransferMemory",1)[0]
assert "m_native_transfer_backings.find(packet[2])" in destroy
assert "transfer->second->Close(kernel);" in destroy
# Current libounce proves that the bootstrap child handles below are HIPC
# MOVE descriptors: root 202 sync, 305 VI2, 497 GPU and 1402 nvservices.
# AddMoveObject transfers the creator reference; no extra KClientSession Open
# is needed for these server-managed child sessions.
for service in ("OpenSyncptSession", "OpenDisplaySession", "OpenGpuSession",
                "OpenNvsrvSession"):
    begin = f"void IGraphicsSupervisor::{service}(HLERequestContext& ctx) {{"
    assert cpp.count(begin) == 1, service
    body = cpp.split(begin, 1)[1].split("\n}", 1)[0]
    assert "IPC::ResponseBuilder rb{ctx, 2, 0, 1};" in body, service
    assert "IPC::ResponseBuilder rb{ctx, 2, 1, 0};" not in body, service
    assert "ctx.AddMoveObject(client);" in body, ("HLE move descriptor", service)
    # The KClientSession creator reference must be released if the outgoing
    # MOVE handle vector cannot allocate; the success descriptor is never
    # emitted after host OOM. Applies to real 202/305/497/1402 bootstrap.
    for needle in ("catch (const std::bad_alloc&)",
                   "client->Close(system.Kernel());",
                   "result = Kernel::ResultOutOfResource;"):
        assert needle in body,(service,"move-oom",needle)
    assert body.index("ctx.AddMoveObject(client);") < body.index(
        "client->Close(system.Kernel());") < body.index(
        "IPC::ResponseBuilder rb{ctx, 2, 0, 1};")
# Feature/video children not used by the current P0 bootstrap are left on
# their existing contract until a client packet proves their descriptor ABI.
# WriteToOutgoingCommandBuffer consumes one queued move reference after
# installing the guest handle.
writer=(R/"sources/eden/src/core/hle/service/hle_ipc.cpp").read_text()
assert writer.index("for (auto& object : outgoing_copy_objects)") < writer.index(
    "for (auto& object : outgoing_move_objects)")
assert "object->Close(kernel);" in writer.split(
    "for (auto& object : outgoing_move_objects)", 1)[1].split("cmd_buf[current_offset++] = handle;", 1)[0]
# Sync child command 22 also moves the readable slot event. Unlike a
# child KSession, the service must keep its original event reference so it can
# arm/signal/cancel the same slot later; therefore one extra transfer ref is
# opened and consumed by the HLE move serializer.
sync_slot=cpp.split("void GetSlotEvent(HLERequestContext& ctx) {",1)[1].split(
    "\n    }\n    void RequestSlotEvent",1)[0]
for needle in (
    "readable->Open(system.Kernel())",
    "ctx.AddMoveObject(readable);",
    "readable->Close(system.Kernel());",
    "IPC::ResponseBuilder rb{ctx, 2, 0, 1};",
):
    assert needle in sync_slot,needle
assert "PushCopyObjects" not in sync_slot
assert "IPC::ResponseBuilder rb{ctx, 2, 1, 0};" not in sync_slot

for marker in ("ValuesOffset = 0x40","RoSummaryOffset = 0x1040","RoBitsOffset = 0x1100",
               "RwBitsOffset = 0x100","PollInterval{std::chrono::microseconds{500}}"):
    assert marker in cpp, marker
assert '#include "video_core/host1x/host1x.h"' in cpp, "graphics supervisor must own the complete Host1x type directly"
assert cpp.index('#include "video_core/host1x/host1x.h"') < cpp.index('#include "video_core/host1x/syncpoint_manager.h"')
assert "SetGuestMirror(" in cpp
assert "WithGuestValuesStable" in cpp
assert "using ActionHandle = u64;" in host
assert "m_syncpt->Shutdown();" in cpp
# Lichii nx2 VI2 explicitly echoes the two TIPC-0 token words. The current
# selected PS5 class must not fall into the NotImplemented transport fallback.
vi2_begin = cpp.index("class IVi2Session final")
vi2_end = cpp.index("};", vi2_begin)
# The class has nested types, so search only the handler definition substring.
vi2_sig = "void Handshake(HLERequestContext& ctx) {"
vi2_handler = cpp[vi2_begin:].split(vi2_sig, 1)[1].split("\n    }", 1)[0]
for needle in ('const u32 token_lo = packet[2];',
               'const u32 token_hi = packet[3];',
               'NativeTipcReplyWords(ctx, {token_lo, token_hi});'):
    assert needle in vi2_handler
assert vi2_handler.index("token_lo = packet[2]") < vi2_handler.index("NativeTipcReplyWords")
assert vi2_handler.index("token_hi = packet[3]") < vi2_handler.index("NativeTipcReplyWords")
# The shared VI2 reply builder supplies one result word plus exactly two
# token words. ResponseBuilder mutates the input in-place.
assert "IPC::ResponseBuilder rb{ctx, static_cast<u32>(2 + words.size())};" in cpp
assert "rb.PushRaw<u32>(ResultSuccess.raw);" in cpp
assert "std::memset(cmdbuf, 0, sizeof(u32) * IPC::COMMAND_BUFFER_LENGTH);" in (
    R/"encore-active/eden-core/include/core/hle/service/ipc_helpers.h").read_text()
# Session registration must not strand KSession and quota after a failed
# server RegisterSession, including host allocation failures.
meta = cpp.split("static Result CreateMetadataSession(", 1)[1].split("static void NativeTipcReplyWords(", 1)[0]
for needle in ("SCOPE_EXIT {", "session->GetClientSession().Close(kernel);",
               "session->GetServerSession().Close(kernel);",
               "transferred_to_server = true;",
               "catch (const std::bad_alloc&)",
               "R_TRY(registered);"):
    assert needle in meta
assert meta.index("SCOPE_EXIT {") < meta.index("server.RegisterSession(")
assert meta.index("R_TRY(registered);") < meta.index("transferred_to_server = true;")
# NS2 display (gsv 305) and GPU (gsv 497) session creation must
# reply with a MOVE handle only after a real KClientSession exists. The
# previous failure path wrote a null move object and a success-shaped TIPC
# handle descriptor. Guard both bootstrap rejection and host OOM.
for service in ("OpenDisplaySession", "OpenGpuSession"):
    sig = f"void IGraphicsSupervisor::{service}(HLERequestContext& ctx) {{"
    assert cpp.count(sig) == 1
    body = cpp.split(sig, 1)[1].split("\n}", 1)[0]
    assert "catch (const std::bad_alloc&)" in body
    assert "result = Kernel::ResultOutOfResource;" in body
    assert "if (result.IsError()) {" in body
    failure = body.split("if (result.IsError()) {", 1)[1].split("\n    }", 1)[0]
    assert "IPC::ResponseBuilder rb{ctx, 2};" in failure
    assert "rb.Push(result.raw);" in failure
    assert "return;" in failure
    assert body.index("if (result.IsError()) {") < body.index(
        "IPC::ResponseBuilder rb{ctx, 2, 0, 1};")
    assert body.count("ctx.AddMoveObject(client);") == 1
# The GPU weak-owner vector must be provisioned before a server-managed
# kernel session is registered, otherwise a bad_alloc can leak that session.
gpu_open = cpp.split("void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {", 1)[1].split("\n}", 1)[0]
assert gpu_open.index("m_gpu_sessions.reserve(m_gpu_sessions.size() + 1);") < gpu_open.index(
    "CreateMetadataSession(system, ctx, handler, &client)")

# A real default VI display exists. Native VI2 capacity commands 1 and 3
# must not report zero, which prevents some NS2 clients from opening a
# display/notification endpoint. Lichii nx2:vi2 returns 1,1,7 for 1,3,5.
# Native VI2 command 53 must record compositor-released buffers, not
# invent a success timestamp on an unconsumed QueueBuffer.
vi2 = cpp[vi2_begin:cpp.index("class INativeChatGsvSession final", vi2_begin)]
assert '{53,&IVi2Session::GetRecordedTimestamps,"GetRecordedTimestamps"}' in vi2
# All known Lichii nx2 VI2 IDs must have a named dispatch. Unknown
# firmware semantics stay fail-closed: no copied request or fake Success.
opaque_vi2_ids = (13, 107, 108, 113, 230, 231, 232, 301, 302,
                  353, 354, 355, 356, 358, 359)
for command in opaque_vi2_ids:
    assert f"{{{command},&IVi2Session::Unsupported," in vi2, command
unsupported = vi2.split("void Unsupported(HLERequestContext& ctx) {", 1)[1].split("\n    }", 1)[0]
assert "rb.Push(Kernel::ResultNotImplemented.raw);" in unsupported
history = vi2.split("void GetRecordedTimestamps(HLERequestContext& ctx) {",1)[1].split("\n    }",1)[0]
for evidence in ("recorder = p[5]", "W(p, 6) & 0x1f", "W(p, 8)",
                 "NativeTipcReplyWords(ctx, {0, 0, 1})",
                 "ctx.WriteBufferB(ticks.data(), requested, 0)"):
    assert evidence in history, evidence
release = vi2.split("void Release() {",1)[1].split("void Present(HLERequestContext& ctx) {",1)[0]
present = vi2.split("void Present(HLERequestContext& ctx) {",1)[1]
assert "m_recorded_ticks[recorder - 1] = system.CoreTiming().GetClockTicks()" in release
assert "c.recorders[slot] = 0;" in release
assert "c->recorders[slot]=static_cast<u8>(p[18] & 0xf);" in present
assert "std::array<std::optional<u64>, 15> m_recorded_ticks{};" in vi2

vi2_capacity = cpp[vi2_begin:].split("void Limit(HLERequestContext& ctx) {",1)[1].split(
    "\n    }",1)[0]
for check in ("const u32 command = ctx.GetCommand();",
              "p[0] == 16 + command && p[1] == 0",
              "const u32 available = command == 5 ? 7u : 1u;",
              "rb.Push<u32>(result.IsSuccess() ? available : 0u);"):
    assert check in vi2_capacity, check
for handler in ('{1,&IVi2Session::Limit,"GetNotificationLimit"}',
                '{3,&IVi2Session::Limit,"GetSessionLimit"}',
                '{5,&IVi2Session::Limit,"GetRequestGroupLimit"}'):
    assert handler in cpp
assert "rb.Push<u32>(ctx.GetCommand()==5?7:0);" not in cpp
assert "rw->GetOwner() != process" in cpp and "ro->GetOwner() != process" in cpp
assert "std::array<Kernel::KEvent*, MaxSlots> m_events{};" in cpp
# GSV 1402 is a *real* NVDRV-backed nvsrv child session, needed by
# newer NS2 graphics SDKs before they can reach any GPFIFO submit.
# Compare Lichii nx2 GSV 1402; do not substitute a zero-reply stub.
nvhdr=(R/"sources/eden/src/core/hle/service/nvdrv/nvdrv_interface.h").read_text()
nvcpp=(R/"sources/eden/src/core/hle/service/nvdrv/nvdrv_interface.cpp").read_text()
assert '{1402, &IGraphicsSupervisor::OpenNvsrvSession, "OpenNvsrvSession"}' in cpp
assert 'void OpenNvsrvSession(HLERequestContext& ctx);' in hdr
nvsrv=cpp.split("void IGraphicsSupervisor::OpenNvsrvSession(HLERequestContext& ctx) {",1)[1].split("\n}",1)[0]
for needle in (
    'packet[0] == 16 + 1402 && packet[1] == 0',
    'm_bootstrap.CurrentPhase() == GB::Phase::Ready',
    'GetService<Nvidia::NVDRV>("nvdrv:s", true)',
    'std::make_unique<Nvidia::NVDRV>(',
    'system, module, "nx2:nvsrv"',
    'handler->InitializeForProcess(m_process)',
    'CreateMetadataSession(system, ctx,',
    'catch (const std::bad_alloc&)',
    'IPC::ResponseBuilder rb{ctx, 2, 0, 1};',
    'ctx.AddMoveObject(client);',
):
    assert needle in nvsrv,needle
assert nvsrv.index('handler->InitializeForProcess(m_process)') < nvsrv.index(
    'CreateMetadataSession(system, ctx,')
assert nvsrv.index('if (result.IsError()) {') < nvsrv.index(
    'IPC::ResponseBuilder rb{ctx, 2, 0, 1};')
# Firmware boot can open native NvDrv2User (1402) BEFORE GPU session (497).
# A process is therefore already native for the first NVDRV ioctl. Tie the
# ticket to the actual child session lifetime, not GSV parent / program ID.
assert '#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"' in cpp
for marker in (
    'Nvidia::Devices::NativeGpuProcessRegistry::Instance();',
    'registry.Register(m_process)',
    'std::shared_ptr<Nvidia::NVDRV> handler{',
    'pending.release()',
    '[native_process = m_process](Nvidia::NVDRV* child) noexcept',
    'delete child;',
    '.Unregister(native_process);',
):
    assert marker in nvsrv,marker
assert nvsrv.index('registry.Register(m_process)') < nvsrv.index(
    'std::shared_ptr<Nvidia::NVDRV> handler{') < nvsrv.index(
    'handler->InitializeForProcess(m_process)') < nvsrv.index(
    'CreateMetadataSession(system, ctx, handler, &client)')
assert nvsrv.index('delete child;') < nvsrv.index('.Unregister(native_process);')
assert nvsrv.index('result = Kernel::ResultOutOfResource;') < nvsrv.index(
    'std::shared_ptr<Nvidia::NVDRV> handler{')
# The independent GPU session also keeps a ref to the same process-generation
# key. After this child closes, 497 may remain live with no lost epoch.
shared=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker_shared.cpp").read_text()
registry=(R/"encore-active/ns2-runtime/hle/core/hle/service/nvdrv/devices/native_gpu_process_registry.h").read_text()
assert "native_registered = Nvidia::Devices::NativeGpuProcessRegistry::Instance().Register(process);" in shared
assert "if (--it->second.references == 0) owners.erase(it);" in registry
# 1402 must reject a second active user session (firmware 0x200f3),
# while a closed child must not be kept alive by the parent supervisor.
assert 'std::weak_ptr<Nvidia::NVDRV> m_nvsrv_session;' in hdr
assert 'if (!m_nvsrv_session.expired()) {' in nvsrv
assert 'result = Result{0x200f3};' in nvsrv
assert nvsrv.index('result = Result{0x200f3};') < nvsrv.index(
    'handler->InitializeForProcess(m_process)')
assert nvsrv.index('CreateMetadataSession(system, ctx, handler, &client)') < nvsrv.index(
    'm_nvsrv_session = handler;')
assert 'std::move(handler)' not in nvsrv
assert 'bool InitializeForProcess(Kernel::KProcess* process);' in nvhdr
nvinitialize=nvcpp.split('bool NVDRV::InitializeForProcess(Kernel::KProcess* process) {',1)[1].split("\n}",1)[0]
assert nvinitialize.index('if (!process || is_initialized)') < nvinitialize.index(
    'nvdrv->GetContainer().OpenSession(process)')
assert nvinitialize.index('session_id = nvdrv->GetContainer().OpenSession(process);') < nvinitialize.index(
    'is_initialized = true;')
assert 'container.CloseSession(session_id);' in nvcpp
assert '{13, &NVDRV::SetGraphicsFirmwareMemoryMarginEnabled' in nvcpp


# GSV 902/905/906 opens a creator session; its TIPC 1 creates the
# actual decoder child. Both return session MOVE handles. A COPY header
# with AddMoveObject leaves the guest reading a bogus handle slot, and an
# allocation failure while queuing MOVE must close the creator reference.
for method in ("OpenVideoDecoderSession", "OpenVideoDecoderChildSession"):
    child=cpp.split(f"void IGraphicsSupervisor::{method}(HLERequestContext& ctx) {{",1)[1].split(
        "\n}",1)[0]
    for token in ("CreateMetadataSession(system, ctx,",
                  "ctx.AddMoveObject(client);",
                  "catch (const std::bad_alloc&)",
                  "client->Close(system.Kernel());",
                  "result = Kernel::ResultOutOfResource;",
                  "IPC::ResponseBuilder rb{ctx, 2, 0, 1};",
                  "IPC::ResponseBuilder rb{ctx, 2};"):
        assert token in child,(method,token)
    assert "IPC::ResponseBuilder rb{ctx, 2, 1, 0};" not in child
    assert child.index("ctx.AddMoveObject(client);") < child.index(
        "client->Close(system.Kernel());") < child.index(
        "IPC::ResponseBuilder rb{ctx, 2, 0, 1};")
    assert child.index("if (result.IsError()) {") < child.index(
        "IPC::ResponseBuilder rb{ctx, 2, 0, 1};")

# Lichii nx2 GSV 991 returns the real decoder pool backing size and
# 8192-byte alignment. The query and GSV 911 must use the same layout.
assert '{991, &IGraphicsSupervisor::CalculateDecoderImagePoolBuffer, "CalculateDecoderImagePoolBuffer"}' in cpp
assert 'void CalculateDecoderImagePoolBuffer(HLERequestContext& ctx);' in hdr
pool_query=cpp.split("void IGraphicsSupervisor::CalculateDecoderImagePoolBuffer(HLERequestContext& ctx) {",1)[1].split("\n}",1)[0]
for token in ("packet[0] == 16 + 991 && packet[1] == 17",
              "CalculateNativeVideoImageLayout(std::span<const u32>{packet + 2, 16}, packet[18])",
              "Kernel::ResultInvalidCombination", "IPC::ResponseBuilder rb{ctx, 6};",
              "DecoderPoolAlignment = 64ull * 128", "layout->total_bytes"):
    assert token in pool_query,token
pool_create=cpp.split("void IGraphicsSupervisor::CreateVideoImagePool(HLERequestContext& ctx) {",1)[1].split("\n}",1)[0]
assert "CalculateNativeVideoImageLayout(preference, count)" in pool_create


# SwitchBrew GSV 1001/1002 are VIDEO ENCODER, not decoder aliases.
# Without a native encoder implementation, both must fail closed and never
# construct a decoder child session or mutate the decoder admission state.
for cmd, name in ((1001, "CreateVideoEncoderContext"),
                  (1002, "InitializeVideoEncoderContext")):
    assert f'{{{cmd}, &IGraphicsSupervisor::UnavailableVideoEncoder, "{name}"}}' in cpp
    assert f'{{{cmd}, &IGraphicsSupervisor::InitializeVideoCodecContext' not in cpp
    assert f'{{{cmd}, &IGraphicsSupervisor::OpenVideoDecoderSession' not in cpp
stub = cpp.split("void IGraphicsSupervisor::UnavailableVideoEncoder(HLERequestContext& ctx) {", 1)[1].split("\n}", 1)[0]
assert "// not implemented yet on Encore" in stub
assert "Kernel::ResultNotImplemented.raw" in stub
assert "ctx.AddMoveObject" not in stub
assert "void UnavailableVideoEncoder(HLERequestContext& ctx);" in hdr

# Lichii nx2 graphics-session table parity: every named ID is either
# implemented by a real owner or explicitly rejected, never a fake Success.
known_lichii_gsv_ids = (
    5, 120, 121, 122, 123, 124, 125, 131, 133,
    311, 312, 321, 361, 424, 425, 436,
    461, 462, 463, 464, 465, 466, 467, 468, 494,
    801, 802, 803, 811, 812, 813, 814, 861, 891,
    1091, 1121, 1503, 1601, 1602, 1603, 1611, 1612
)
gsv_ctor = cpp.split("IGraphicsSupervisor::IGraphicsSupervisor(", 1)[1].split(
    "RegisterHandlersTipc(functions);", 1)[0]
for cmd in known_lichii_gsv_ids:
    owner = ("CalculateFeatureImagePoolRequirements" if cmd == 891
             else "OpenGpuTimeSession" if cmd == 1121
             else "UnavailableDocumentedFeature")
    assert f"{{{cmd}, &IGraphicsSupervisor::{owner}," in gsv_ctor, cmd
gsv_rejection = cpp.split(
    "void IGraphicsSupervisor::UnavailableDocumentedFeature(HLERequestContext& ctx) {", 1
)[1].split("\n}", 1)[0]
assert "rb.Push(Kernel::ResultNotImplemented.raw);" in gsv_rejection

# Lichii GSV 1121 is a MOVE gpu_time child, not a zeroed success.
assert '{1121, &IGraphicsSupervisor::OpenGpuTimeSession, "OpenGpuTimeSession"}' in cpp
assert "class INativeGpuTimeSession final" in cpp
assert '{0, &INativeGpuTimeSession::Handshake, "Handshake"}' in cpp
assert "QueryApplicationGpuTimeSpan" in cpp
gpu_time = cpp.split("void IGraphicsSupervisor::OpenGpuTimeSession(HLERequestContext& ctx) {",1)[1].split("\n}",1)[0]
for evidence in ("p[0] == 16 + 1121", "CreateMetadataSession(system, ctx,",
                 "ctx.AddMoveObject(client)", "IPC::ResponseBuilder rb{ctx, 2, 0, 1}",
                 "client->Close(system.Kernel())"):
    assert evidence in gpu_time, evidence
assert "rb.Push(Kernel::ResultNotImplemented.raw);" in cpp
assert "void OpenGpuTimeSession(HLERequestContext& ctx);" in hdr

print("NS2_SYNCPT_BOOTSTRAP_SOURCE_PASS graphics_frame=1101,1102,1111,1112,1113 native_memory=113-transfer-owner donated_usage=498 supervisor=202,204 syncpt=0,1,22,103,201,202 vi2_handshake=1 vi2_capacity=1,1,7 session_rollback=1 mirror=1 direct_host1x=1")

# NS2 PS5 ee5d6d57: SDK aborts with 0x4201 immediately after 1501.
# SwitchBrew identifies context create/init; Lichii 05412e6 routes both to
# permission checks. Encore tracks the authenticated context lifecycle.
for cmd, name in ((1501,"CreateInterModuleImageContext"),(1502,"InitializeInterModuleImageContext")):
    assert f'{{{cmd}, &IGraphicsSupervisor::InitializeInterModuleImageContext, "{name}"}}' in cpp
    assert f'{{{cmd}, &IGraphicsSupervisor::UnavailableDocumentedFeature' not in cpp
inter=cpp.split("void IGraphicsSupervisor::InitializeInterModuleImageContext(HLERequestContext& ctx) {",1)[1].split("\n}",1)[0]
assert "m_bootstrap.InitializeInterModuleImageContext(packet, ctx.GetCommand())" in inter
assert "TranslateBootstrap(" in inter and "rb.Push(result.raw);" in inter
assert "void InitializeInterModuleImageContext(HLERequestContext& ctx);" in hdr
print("NS2_GSV_INTER_MODULE_IMAGE_BOOTSTRAP_SOURCE_PASS")

# libounce 32e7ac41 _gsvBootstrapRoot requires 126 between 101 and
# 1501/1502; mode 0 skips 120. Both compared emulator upstreams return 0.
assert '{126, &IGraphicsSupervisor::QuerySessionMode, "GetSessionFlag"}' in cpp
query_mode=cpp.split("void IGraphicsSupervisor::QuerySessionMode(HLERequestContext& ctx) {",1)[1].split("\n}",1)[0]
assert "m_bootstrap.QuerySessionMode(packet, mode)" in query_mode
assert "NativeTipcReplyWords(ctx, {mode});" in query_mode
assert "rb.Push(result.raw);" in query_mode

# libounce 32e7ac41 _gsvSyncCreate calls 1 then 21(u32 configured).
# Bound the actual live event table, not just an acknowledgement.
assert '{21, &INativeSyncptSession::ConfigureSlots, "ConfigureSlots"}' in cpp
slots=cpp.split("void ConfigureSlots(HLERequestContext& ctx) {",1)[1].split("    void QueryCounter",1)[0]
assert "DecodeSyncptSlotCount(packet)" in slots
assert "m_events.begin() + *count" in slots
assert "m_configured_slots = *count;" in slots
assert "if (slot >= m_configured_slots) return nullptr;" in cpp

# Retain transfer objects for the full lifetime of weak async mirror callbacks.
mirror = cpp.split("struct NativeSyncptState :",1)[1].split("class IVi2Session",1)[0]
assert "rw.GetPointerUnsafe() == ro.GetPointerUnsafe()" in mirror
assert mirror.index("rw_backing->Open(system.Kernel())") < mirror.index("ro_backing->Open(system.Kernel())") < mirror.index("transfer_backings = {rw_backing, ro_backing}") < mirror.index("memory = &ro->GetOwner()->GetMemory()")
assert "rw_backing->Close(system.Kernel())" in mirror
assert "for (auto* backing : transfer_backings) if (backing) backing->Close(system.Kernel());" in mirror
assert "if (memory) system.Host1x().GetSyncpointManager().SetGuestMirror({});" in mirror

# libounce gsvSyncCheckNative explicitly treats invalid producer as UINT64_MAX.
slot_owner=cpp.split("class INativeSyncptSession",1)[1].split("struct NativeSyncptState",1)[0]
assert "std::numeric_limits<u64>::max();" in slot_owner
assert "GraphicsSupervisorProtocol::DecodeSyncpointWait(" in slot_owner
assert slot_owner.index("Cancel(request);",slot_owner.index("void RequestSlotEvent")) < slot_owner.index("if (syncpoint >= HostSyncpoints)")
cancel=slot_owner.split("bool Cancel(SlotRequest& request)",1)[1]
assert "if (request.handle != 0)" in cancel and "DeregisterGuestAction" in cancel
assert "if (!request.pending.exchange" not in cancel
