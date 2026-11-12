#!/usr/bin/env python3
"""NS1 R23 #1: exact FW23.5 qlaunch NS interface, 13 command IDs, event ABI."""
from pathlib import Path
import hashlib
import re
root=Path(__file__).resolve().parents[1]
base=root/"encore-active/eden-backports/src/core/hle/service/ns"
cpp=(base/"application_manager_interface.cpp").read_text()
header=(base/"application_manager_interface.h").read_text()
expected={
 "application_manager_interface.cpp":"094a3142efa2f10e384b7962721cc700e802313b",
 "application_manager_interface.h":"7ff9a94f4663cdb721056be2ae3a4af52b425441",
}
for name,sha in expected.items():
 b=(base/name).read_bytes()
 actual=hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
 assert actual==sha,(name,actual)
entries={}
for index,method in re.findall(r'\{(32\d\d),\s*(?:D<&IApplicationManagerInterface::(Unknown\d+)>|nullptr)',cpp):
 pass
for n in range(3250,3263):
 match=re.findall(r'\{'+str(n)+r',\s*([^}]+)\}',cpp)
 assert len(match)==1,(n,match)
 entries[n]=match[0]
 if n==3252:
  assert "nullptr" in entries[n]
 else:
  assert ("Unknown"+str(n)) in entries[n]
  assert cpp.count("Result IApplicationManagerInterface::Unknown"+str(n)+"(")==1
  assert header.count("Result Unknown"+str(n)+"(")==1
assert "present_event{service_context}" in cpp
assert header.count("Event present_event;")==1
assert "*out_event = present_event.GetHandle();" in cpp
assert "Unknown4022" in cpp and "GetApplicationControlData" in cpp
print("R23_FW235_Qlaunch_NATIVE_SOURCE_PASS commands=13 handlers=12 event=1 old_backports_retained=1")
