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
# A successful MapNative4K must never escape without a corresponding
# AddSpace entry. Reject registry saturation and reverse the just-mapped VA.
create_replay = create[create.index("for (auto& identity : m_shared->identity_gpu_mappings)"):
                       create.index("if (replay == Nvidia::NvResult::Success)")]
assert "if (!identity.AddSpace(candidate))" in create_replay
assert "replay = Nvidia::NvResult::InvalidState;" in create_replay
assert create_replay.index("as->UnmapNative4K(identity.address, identity.size)") < create_replay.index(
    "replay = Nvidia::NvResult::InvalidState;")
# OOM after try_emplace/slot allocation must unwind all published owners
# before closing the FD. Partial NS2 GPU setup must remain retryable.
oom = create[create.index("} catch (const std::bad_alloc&) {", create.index("const u32 candidate")):
             create.index("if (result.IsError()) m_module->Close(fd);")]
for needle in ("identity.HasSpace(candidate)", "as->UnmapNative4K(identity.address, identity.size)",
               "identity.RemoveSpace(candidate)", "m_shared->address_spaces.Remove(candidate)",
               "m_shared->opened_gpu_as.erase(candidate)",
               "result = Kernel::ResultOutOfResource;"):
    assert needle in oom, needle
reg_replay = reg[reg.index("for (const auto owned : m_shared->address_spaces.OwnedIds())"):
                 reg.index("if (identity_result.IsError())")]
assert "catch (const std::bad_alloc&)" in reg_replay
assert "if (!identity.AddSpace(*owned))" in reg_replay
assert reg_replay.index("as->UnmapNative4K(begin, size)") < reg_replay.index(
    "identity_result = Kernel::ResultInvalidState;", reg_replay.index("if (!identity.AddSpace(*owned))"))

print("NS2_GPU_IDENTITY_MAPPING_SOURCE_PASS sub101=identity replay=new_as sub102=all_owned_unmap")
