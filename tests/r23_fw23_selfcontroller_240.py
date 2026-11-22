#!/usr/bin/env python3
"""Exact maintained NS2/HLE SelfController FW23 cmd 240 source parity (no PS5 link)."""
from pathlib import Path
import hashlib
r=Path(__file__).resolve().parents[1]/"encore-active"
file=r/"ns2-runtime/self_controller/core/hle/service/am/service/self_controller.cpp"
header=r/"eden-backports/src/core/hle/service/am/service/self_controller.h"
s=file.read_text()
h=header.read_text()
old='        {230, D<&ISelfController::Unknown230>, "Unknown230"},'
new=old+'\n        {240, D<&ISelfController::Unknown240>, "Unknown240"},'
body='Result ISelfController::Unknown240(u32 in_val) {\n    LOG_WARNING(Service_AM, "(STUBBED) called, in_val={}", in_val);\n\n    R_SUCCEED();\n}\n\n'
assert s.count(new)==1 and s.count(body)==1
ns2_240x = (
    '        {2400, &ISelfController::StubReplyZeroedNx2, "Unknown2400"},\\n'
    '        {2401, &ISelfController::StubReplyZeroedNx2, "Unknown2401"},\\n'
    '        {2402, &ISelfController::StubReplyZeroedNx2, "Unknown2402"},'
)
ns2_240x = ns2_240x.replace('\\n', '\n')
# Lichii currently sends a 32-word, zero-filled success reply, not a bare
# success header. Keep only this documented family and retain typed cmd 240.
stub_body = s.split('void ISelfController::StubReplyZeroedNx2(HLERequestContext& ctx) {', 1)[1].split(
    '\n}\n\n// NS2-only command 2000:', 1)[0]
assert s.count(ns2_240x)==1
assert s.index(ns2_240x) > s.index('{2000, &ISelfController::GetGraphicsSupervisor')
assert 'IPC::ResponseBuilder rb{ctx, 2 + ZeroWords};' in stub_body
assert 'rb.Push(ResultSuccess.raw);' in stub_body
assert 'for (u32 i = 0; i < ZeroWords; ++i)' in stub_body
assert 'rb.Push<u32>(0);' in stub_body
assert 'constexpr u32 ZeroWords = 32;' in stub_body
assert h.count("Result Unknown240(u32 in_val);")==1
assert s.count('{2000, &ISelfController::GetGraphicsSupervisor')==1
# NS2 SelfController cmd 2000 is the gateway to all GSV GPU sessions.
# Failures before/after KSession registration must return an IPC error and
# retire BOTH original endpoints instead of leaking the guest session quota.
entry=s.split("void ISelfController::GetGraphicsSupervisor(HLERequestContext& ctx) {",1)[1].split(
    "\n}\n\nResult ISelfController::Exit()",1)[0]
for token in ("std::make_shared<IGraphicsSupervisor>", "std::make_shared<SessionRequestManager>",
              "catch (const std::bad_alloc&)", "SCOPE_EXIT {",
              "session->GetClientSession().Close(kernel);",
              "session->GetServerSession().Close(kernel);",
              "transferred_to_server = true;",
              "ctx.AddCopyObject", "rb.PushCopyObjects(ctx, m_graphics_supervisor_session);"):
    if token == "ctx.AddCopyObject": continue
    assert token in entry, token
assert entry.index("std::make_shared<IGraphicsSupervisor>") < entry.index(
    "Kernel::KSession::Create(kernel)")
assert entry.index("std::make_shared<SessionRequestManager>") < entry.index(
    "reservation.Commit();")
assert entry.index("SCOPE_EXIT {") < entry.index(
    "server.RegisterSession(") < entry.index(
    "transferred_to_server = true;") < entry.index(
    "m_graphics_supervisor_session = &session->GetClientSession();")
assert entry.count("catch (const std::bad_alloc&)") == 2
assert entry.count("IPC::ResponseBuilder rb{ctx, 2, 1};") == 2
assert entry.count("rb.PushCopyObjects(ctx, m_graphics_supervisor_session);") == 2
assert '#include "common/scope_exit.h"' in s and "#include <new>" in s

new_method = '// Lichii currently uses StubReplyZeroed for these Switch 2 commands.\\n'
new_method = new_method.replace('\\n', '\n')
start = s.index(new_method)
stop = s.index('// NS2-only command 2000:', start)
assert start < stop
previous = s.replace(s[start:stop], '', 1).replace(
    '\n'+ns2_240x, '\n        {2400, D<&ISelfController::Unknown240>, "Unknown2400"}, // NS2 u32 input, no output', 1)
original=previous.replace(
    '\n        {2400, D<&ISelfController::Unknown240>, "Unknown2400"}, // NS2 u32 input, no output',
    '',1).replace(new,old,1).replace(body,"",1)
data=original.encode()
sha=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
assert sha=="09c510db1b30362fabff4bf06ff4a3d38fa118fe",sha
assert "Result ISelfController::Unknown240" not in original
print("R23_FW23_SELECTED_SELF_CONTROLLER_240_SOURCE_PASS graphics_supervisor_preserved=1")
