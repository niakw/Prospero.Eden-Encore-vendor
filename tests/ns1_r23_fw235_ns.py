#!/usr/bin/env python3
"""FW23.5 source parity: 13 declared NS commands, 12 exact method contracts."""
from pathlib import Path
import hashlib,re

root=Path(__file__).resolve().parents[1]/"encore-active/eden-backports/src/core/hle/service/ns"
source=root/"application_manager_interface.cpp"
header=root/"application_manager_interface.h"
def blob(path):
 b=path.read_bytes()
 return hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
assert blob(source)=="094a3142efa2f10e384b7962721cc700e802313b"
assert blob(header)=="7ff9a94f4663cdb721056be2ae3a4af52b425441"
s=source.read_text()
h=header.read_text()
commands=list(range(3250,3263))
for n in commands:
 a=re.findall(r"\{"+str(n)+r",\s*([^,]+),",s)
 assert len(a)==1,(n,a)
 if n==3252:
  assert a[0].strip()=="nullptr"
 else:
  assert "Unknown"+str(n) in a[0]
  assert len(re.findall(r"Result IApplicationManagerInterface::Unknown"+str(n)+r"\(",s))==1,n
  assert len(re.findall(r"Result Unknown"+str(n)+r"\(",h))==1,n
assert "present_event{service_context}" in s
assert "Event present_event;" in h
assert "*out_event = present_event.GetHandle();" in s
assert "Result IApplicationManagerInterface::Unknown3250" in s
print("R23_FW235_NS_13_COMMAND_EVENT_SOURCE_PASS 3252_unimplemented=1")
print("R23_FW235_NATIVE_PS5_IPC_UNPROVEN")
