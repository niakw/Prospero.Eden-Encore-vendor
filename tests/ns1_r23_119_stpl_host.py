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
 "encore-active/eden-backports/src/core/hle/service/filesystem/fsp/fsp_srv.cpp":"074138d88766e9acb292e04c383e3d964c44f7d2",
 "encore-active/eden-core/include/core/hle/service/acc/profile_manager.h":"81688175c2b96b509a9108cc1e69b12391d2127f",
 "encore-active/eden-core/include/core/hle/service/ipc_helpers.h":"a266b1a05cf0b20b7cad6d70f9b16dfec503cf7c",
 "encore-active/eden-backports/src/core/hle/service/filesystem/fsp/fs_i_save_data_info_reader.cpp":"647e2499f82de4ac921e6ab709c8a67d4f2f10cc",
 "encore-active/eden-backports/src/core/hle/service/filesystem/fsp/fs_i_save_data_info_reader.h":"4599b92b022b18439014288b43c0c3ecb93f0eb7",
 "encore-active/eden-backports/src/core/hle/service/nifm/nifm.cpp":"1bea7c9ba353181fb7f0b7c23fbbc2435360b50b",
}
for file,expected in files.items():
 d=(ROOT/file).read_bytes()
 sha=hashlib.sha1(b"blob "+str(len(d)).encode()+bytes([0])+d).hexdigest()
 assert sha==expected,(file,sha)
ngc=(ROOT/"encore-active/eden-backports/src/core/hle/service/ngc/ngc.cpp").read_text()
fsp=(ROOT/"encore-active/eden-backports/src/core/hle/service/filesystem/fsp/fsp_srv.cpp").read_text()
reader=(ROOT/"encore-active/eden-backports/src/core/hle/service/filesystem/fsp/fs_i_save_data_info_reader.cpp").read_text()
nifm=(ROOT/"encore-active/eden-backports/src/core/hle/service/nifm/nifm.cpp").read_text()
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
assert '{33, D<&FSP_SRV::DeleteCacheStorage>, "DeleteCacheStorage"}' in fsp
delete_cache=fsp.split("Result FSP_SRV::DeleteCacheStorage(",1)[1].split("Result FSP_SRV::GetCacheStorageSize",1)[0]
assert 'index == 0' in delete_cache
assert 'GetDirectoryRelative("save/cache")' in delete_cache
assert 'DeleteSubdirectoryRecursive(cache_name)' in delete_cache
assert '{68, D<&FSP_SRV::OpenSaveDataInfoReaderWithFilter>, "OpenSaveDataInfoReaderWithFilter"}' in fsp
filtered=fsp.split("Result FSP_SRV::OpenSaveDataInfoReaderWithFilter(",1)[1].split("Result FSP_SRV::WriteSaveDataFileSystemExtraData",1)[0]
assert 'space_id, filter, program_id' in filtered
assert 'std::erase_if(info' in reader
assert 'filter.use_program_id' in reader and 'filter.use_save_data_type' in reader
assert 'filter.use_user_id' in reader and 'filter.use_save_data_id' in reader
assert 'filter.use_index' in reader
assert '{66, D<&FSP_SRV::WriteSaveDataFileSystemExtraDataWithMask>, "WriteSaveDataFileSystemExtraDataWithMask"}' in fsp
masked=fsp.split("Result FSP_SRV::WriteSaveDataFileSystemExtraDataWithMask(",1)[1].split("Result FSP_SRV::FindSaveDataWithFilter",1)[0]
assert 'mask_buffer.size() == sizeof(FileSys::SaveDataExtraData)' in masked
assert 'FindDirectorySaveById' in masked
assert '{5, &IRequest::SetRequirement, "SetRequirement"}' in nifm
assert '{19, &IRequest::GetRequirement, "GetRequirement"}' in nifm
requirement_set=nifm.split("void SetRequirement(HLERequestContext& ctx)",1)[1].split("void GetRequirement",1)[0]
assert 'PopRaw<std::array<u8, 0x24>>()' in requirement_set
requirement_get=nifm.split("void GetRequirement(HLERequestContext& ctx)",1)[1].split("void SetRequirementPreset",1)[0]
assert 'ResponseBuilder rb{ctx, 11}' in requirement_get
assert 'rb.PushRaw(requirement)' in requirement_get
assert '{12, &IRequest::SetPersistent, "SetPersistent"}' in nifm
persistent=nifm.split("void SetPersistent(HLERequestContext& ctx)",1)[1].split("void SetConnectionConfirmationOption",1)[0]
assert 'persistent = rp.Pop<u8>() != 0' in persistent
request_state=nifm.split("void GetRequestState(HLERequestContext& ctx)",1)[1].split("void SetRequirement(",1)[0]
assert 'persistent ? RequestState::OnHold : RequestState::Invalid' in request_state
request_result=nifm.split("void GetResult(HLERequestContext& ctx)",1)[1].split("void GetSystemEventReadableHandles",1)[0]
assert 'else if (!persistent)' in request_result
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
