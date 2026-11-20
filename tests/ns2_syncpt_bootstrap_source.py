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
    failure = body.split("if (result.IsError()) {", 1)[1].split("}", 1)[0]
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
print("NS2_SYNCPT_BOOTSTRAP_SOURCE_PASS supervisor=202,204 syncpt=0,1,22,103,201,202 vi2_handshake=1 vi2_capacity=1,1,7 session_rollback=1 mirror=1 direct_host1x=1")
