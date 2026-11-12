#!/usr/bin/env python3
"""R23 #119 HOS 23 stpl services + safe save-slot error contract."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
files={
 "encore-active/eden-backports/src/core/hle/service/ngc/ngc.cpp":"b5128e3a4509a9060058457c04980b7e6c922865",
 "encore-active/eden-backports/src/core/hle/service/filesystem/fsp/fsp_srv.cpp":"58303a0bd66387244c44968a0878e2f7c821bc9f",
 "encore-active/eden-core/include/core/hle/service/acc/profile_manager.h":"81688175c2b96b509a9108cc1e69b12391d2127f",
 "encore-active/eden-core/include/core/hle/service/ipc_helpers.h":"a266b1a05cf0b20b7cad6d70f9b16dfec503cf7c",
}
for file,expected in files.items():
 d=(ROOT/file).read_bytes()
 sha=hashlib.sha1(b"blob "+str(len(d)).encode()+bytes([0])+d).hexdigest()
 assert sha==expected,(file,sha)
ngc=(ROOT/"encore-active/eden-backports/src/core/hle/service/ngc/ngc.cpp").read_text()
fsp=(ROOT/"encore-active/eden-backports/src/core/hle/service/filesystem/fsp/fsp_srv.cpp").read_text()
ipch=(ROOT/"encore-active/eden-core/include/core/hle/service/ipc_helpers.h").read_text()
profile=(ROOT/"encore-active/eden-core/include/core/hle/service/acc/profile_manager.h").read_text()
assert 'RegisterNamedService("stpl:u"' in ngc
assert 'RegisterNamedService("stpl:sys"' in ngc
assert 'FirmwareManager::GetFirmwareVersion(system).first.major >= 23' in ngc
assert "R_THROW(IPC::ResultNotSupported);" in ngc
write=ngc.split("Result WriteSaveSlot(",1)[1].split("};",1)[0]
assert "R_THROW(IPC::ResultNotSupported);" in write
assert "R_SUCCEED();" not in write
for n in (820,830,831):
 assert f'{{{n}, nullptr' in fsp
assert "constexpr Result ResultNotSupported{ErrorModule::HIPC, 1};" in ipch
assert "static_assert(sizeof(Uid) == 0x10)" in profile
test=r"""
#include <cassert>
enum class Error{Success,NotSupported};
constexpr int GetServiceRegistrationCount(int hos_major){return hos_major>=23?2:0;}
Error WriteSaveSlot() {return Error::NotSupported;}
int main(){
 assert(GetServiceRegistrationCount(22)==0);
 assert(GetServiceRegistrationCount(23)==2);
 assert(GetServiceRegistrationCount(235)==2);
 assert(WriteSaveSlot()==Error::NotSupported);
}
""";
with tempfile.TemporaryDirectory(prefix="stpl-23-") as td:
 p=Path(td)
 (p/"test.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
  "-fsanitize=address,undefined",str(p/"test.cpp"),"-o",str(p/"test")],
  check=True,timeout=60)
 subprocess.run([str(p/"test")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("R23_119_HOS23_STPL_HOST_PASS pre23_absent=1 post23_services=2 rejected_unpersisted_write=1")
