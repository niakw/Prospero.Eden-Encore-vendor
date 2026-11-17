#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
shared=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker_shared.h").read_text()
worker=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.cpp").read_text()
session=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_session.cpp").read_text()
proto=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/gpu_session_protocol.h").read_text()
for x in ("struct IdentityGpuMapping","identity_gpu_mappings","AddSpace","RemoveSpace"):
    assert x in shared,x
assert "Status Remove(U32 id)" in proto
reg=worker[worker.index("Result IGpuWorker::RegisterMemory"):worker.index("Result IGpuWorker::ReleaseMemory")]
for x in (
 "IdentityGpuMapping{id, begin, size",
 "as->MapNative4K(id, begin, 0, size, 0)",
 "identity.AddSpace(*owned)",
 "identity_gpu_mappings.pop_back()",
): assert x in reg,x
release=worker[worker.index("Result IGpuWorker::ReleaseMemory"):worker.index("void IGpuWorker::CreateGpuAccessibleMemory")]
for x in (
 "mapping.memory == id",
 "system.GPU().WaitForIdle()",
 "m_mappings.RecordUnmapped(unmap)",
 "identity_it->RemoveSpace(as_id)",
 "identity_gpu_mappings.erase(identity_it)",
 "handle->pins == 0",
): assert x in release,x
assert release.index("identity_gpu_mappings.erase(identity_it)") < release.index("handle->pins == 0")
create=session[session.index("void IGpuSession::CreateAddressSpace"):session.index("void IGpuSession::GetUserPartition")]
for x in (
 "for (auto& identity : m_shared->identity_gpu_mappings)",
 "as->MapNative4K(identity.handle, identity.address",
 "identity.AddSpace(candidate)",
 "m_shared->address_spaces.Remove(candidate)",
): assert x in create,x
print("NS2_GPU_IDENTITY_MAPPING_SOURCE_PASS sub101=identity replay=new_as sub102=all_owned_unmap")
