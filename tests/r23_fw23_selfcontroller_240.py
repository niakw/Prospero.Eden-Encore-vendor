#!/usr/bin/env python3
"""Exact maintained NS2/HLE SelfController FW23 cmd 240 source parity (no PS5 link)."""
from pathlib import Path
import hashlib
r=Path(__file__).resolve().parents[1]/"encore-active"
file=r/"ns2-runtime/self_controller/core/hle/service/am/service/self_controller.cpp"
header=r/"eden-backports/src/core/hle/service/am/service/self_controller.h"
s=file.read_text()
h=header.read_text()
# Lichii structural AM cmd-table successor; validate real selected slots
# and reconstruct the exact proven FW23 predecessor for frozen SHA tests.
blob=lambda raw: hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
if blob(s.encode()) == "4c0e972bb42d5f628d2ad70c4a3f974b67cfc616":
    for cmd in (22, 23, 24, 73, 200, 210, 211, 220, 221, 2100, 2200, 2300):
        assert f'{{{cmd}, &ISelfController::StubReplyZeroed, "Unknown{cmd}"}}' in s
    for previous, current in (
        ("        {17, nullptr, \"SetControllerFirmwareUpdateSection\"},\n        {18, nullptr, \"SetRequiresCaptureButtonShortPressedMessage\"},\n", "        {17, &ISelfController::StubReplyZeroed, \"SetControllerFirmwareUpdateSection\"},\n        {18, &ISelfController::StubReplyZeroed, \"SetRequiresCaptureButtonShortPressedMessage\"},\n"),
        ("        {20, nullptr, \"SetDesirableKeyboardLayout\"},\n        {21, nullptr, \"GetScreenShotProgramId\"},\n", "        {20, &ISelfController::StubReplyZeroed, \"SetDesirableKeyboardLayout\"},\n        {21, &ISelfController::StubReplyZeroed, \"GetScreenShotProgramId\"},\n"),
        ("        {45, nullptr, \"SetManagedDisplayLayerSeparationMode\"},\n        {46, nullptr, \"SetRecordingLayerCompositionEnabled\"},\n", "        {45, &ISelfController::StubReplyZeroed, \"SetManagedDisplayLayerSeparationMode\"},\n        {46, &ISelfController::StubReplyZeroed, \"SetRecordingLayerCompositionEnabled\"},\n"),
        ("        {64, nullptr, \"SetInputDetectionSourceSet\"},\n", "        {64, &ISelfController::StubReplyZeroed, \"SetInputDetectionSourceSet\"},\n"),
        ("        {66, nullptr, \"GetCurrentIlluminance\"},\n", "        {66, &ISelfController::StubReplyZeroed, \"GetCurrentIlluminance\"},\n"),
        ("        {70, nullptr, \"ReportMultimediaError\"},\n        {71, nullptr, \"GetCurrentIlluminanceEx\"},\n", "        {70, &ISelfController::StubReplyZeroed, \"ReportMultimediaError\"},\n        {71, &ISelfController::StubReplyZeroed, \"GetCurrentIlluminanceEx\"},\n"),
        ("        {80, nullptr, \"SetWirelessPriorityMode\"},\n", "        {80, &ISelfController::StubReplyZeroed, \"SetWirelessPriorityMode\"},\n"),
        ("        {110, nullptr, \"SetApplicationAlbumUserData\"},\n", "        {110, &ISelfController::StubReplyZeroed, \"SetApplicationAlbumUserData\"},\n"),
        ("        {1000, nullptr, \"GetDebugStorageChannel\"},\n", "        {1000, &ISelfController::StubReplyInterface, \"GetDebugStorageChannel\"},\n"),
        ("", "        {22, &ISelfController::StubReplyZeroed, \"Unknown22\"},\n        {23, &ISelfController::StubReplyZeroed, \"Unknown23\"},\n        {24, &ISelfController::StubReplyZeroed, \"Unknown24\"},\n        {73, &ISelfController::StubReplyZeroed, \"Unknown73\"},\n        {200, &ISelfController::StubReplyZeroed, \"Unknown200\"},\n        {210, &ISelfController::StubReplyZeroed, \"Unknown210\"},\n        {211, &ISelfController::StubReplyZeroed, \"Unknown211\"},\n        {220, &ISelfController::StubReplyZeroed, \"Unknown220\"},\n        {221, &ISelfController::StubReplyZeroed, \"Unknown221\"},\n        {2100, &ISelfController::StubReplyZeroed, \"Unknown2100\"},\n        {2200, &ISelfController::StubReplyZeroed, \"Unknown2200\"},\n        {2300, &ISelfController::StubReplyZeroed, \"Unknown2300\"},\n"),
    ):
        assert s.count(current)==1, ("Unexpected NS2 SelfController table", current[:80])
        s=s.replace(current,previous,1)
    assert blob(s.encode())=="7783c917d8b2f2e8d26c091d5507964fcf9d44dd"
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
