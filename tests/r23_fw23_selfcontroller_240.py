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
ns2_2400 = '        {2400, D<&ISelfController::Unknown240>, "Unknown2400"}, // NS2 u32 input, no output'
assert s.count(ns2_2400)==1
assert s.index(ns2_2400) > s.index('{2000, &ISelfController::GetGraphicsSupervisor')
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

original=s.replace("\n"+ns2_2400,"",1).replace(new,old,1).replace(body,"",1)
data=original.encode()
sha=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
assert sha=="09c510db1b30362fabff4bf06ff4a3d38fa118fe",sha
assert "Result ISelfController::Unknown240" not in original
print("R23_FW23_SELECTED_SELF_CONTROLLER_240_SOURCE_PASS graphics_supervisor_preserved=1")
