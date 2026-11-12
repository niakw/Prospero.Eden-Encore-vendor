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
assert h.count("Result Unknown240(u32 in_val);")==1
assert s.count('{2000, &ISelfController::GetGraphicsSupervisor')==1
original=s.replace(new,old,1).replace(body,"",1)
data=original.encode()
sha=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
assert sha=="ad2b043f972170651b61d2d33aea4082e95c47cf",sha
assert "Result ISelfController::Unknown240" not in original
print("R23_FW23_SELECTED_SELF_CONTROLLER_240_SOURCE_PASS graphics_supervisor_preserved=1")
