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
assert "rw->GetOwner() != process" in cpp and "ro->GetOwner() != process" in cpp
assert "std::array<Kernel::KEvent*, MaxSlots> m_events{};" in cpp
print("NS2_SYNCPT_BOOTSTRAP_SOURCE_PASS supervisor=202,204 syncpt=0,1,22,103,201,202 vi2_handshake=1 session_rollback=1 mirror=1 direct_host1x=1")
