#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
cpp=(R/"encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.cpp").read_text()
hdr=(R/"encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.h").read_text()
host=(R/"sources/eden/src/video_core/host1x/syncpoint_manager.h").read_text()
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
# Native GSV child replies are one COPY descriptor (not MOVE) on the wire:
# Lichii nx2 ReturnRawSession uses ResponseBuilder(ctx, 2, 1, 0).
# Internally the original KSession client reference must be transferred to
# the guest handle to avoid leaking it after guest CloseHandle.
for service in ("OpenSyncptSession", "OpenDisplaySession", "OpenGpuSession",
                "OpenNvsrvSession", "OpenCameraSession", "OpenChatSession",
                "OpenVideoDecoderSession", "OpenVideoDecoderChildSession"):
    begin = f"void IGraphicsSupervisor::{service}(HLERequestContext& ctx) {{"
    assert cpp.count(begin) == 1, service
    body = cpp.split(begin, 1)[1].split("\n}", 1)[0]
    assert "IPC::ResponseBuilder rb{ctx, 2, 1, 0};" in body, service
    assert "IPC::ResponseBuilder rb{ctx, 2, 0, 1};" not in body, service
    assert "ctx.AddMoveObject(client);" in body, ("HLE copy-slot + transfer-ref", service)
# The selected HLE interface advertises COPY count in the TIPC descriptor;
# WriteToOutgoingCommandBuffer then writes the sole queued handle and releases
# its creator reference for the internal move queue.
writer=(R/"sources/eden/src/core/hle/service/hle_ipc.cpp").read_text()
assert writer.index("for (auto& object : outgoing_copy_objects)") < writer.index(
    "for (auto& object : outgoing_move_objects)")
assert "object->Close(kernel);" in writer.split(
    "for (auto& object : outgoing_move_objects)", 1)[1].split("cmd_buf[current_offset++] = handle;", 1)[0]
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
        "IPC::ResponseBuilder rb{ctx, 2, 1, 0};")
    assert body.count("ctx.AddMoveObject(client);") == 1
# The GPU weak-owner vector must be provisioned before a server-managed
# kernel session is registered, otherwise a bad_alloc can leak that session.
gpu_open = cpp.split("void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {", 1)[1].split("\n}", 1)[0]
assert gpu_open.index("m_gpu_sessions.reserve(m_gpu_sessions.size() + 1);") < gpu_open.index(
    "CreateMetadataSession(system, ctx, handler, &client)")

# A real default VI display exists. Native VI2 capacity commands 1 and 3
# must not report zero, which prevents some NS2 clients from opening a
# display/notification endpoint. Lichii nx2:vi2 returns 1,1,7 for 1,3,5.
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
    'std::make_shared<Nvidia::NVDRV>(',
    'system, module, "nx2:nvsrv"',
    'handler->InitializeForProcess(m_process)',
    'CreateMetadataSession(system, ctx,',
    'catch (const std::bad_alloc&)',
    'IPC::ResponseBuilder rb{ctx, 2, 1, 0};',
    'ctx.AddMoveObject(client);',
):
    assert needle in nvsrv,needle
assert nvsrv.index('handler->InitializeForProcess(m_process)') < nvsrv.index(
    'CreateMetadataSession(system, ctx,')
assert nvsrv.index('if (result.IsError()) {') < nvsrv.index(
    'IPC::ResponseBuilder rb{ctx, 2, 1, 0};')
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

print("NS2_SYNCPT_BOOTSTRAP_SOURCE_PASS supervisor=202,204 syncpt=0,1,22,103,201,202 vi2_handshake=1 vi2_capacity=1,1,7 session_rollback=1 mirror=1 direct_host1x=1")
