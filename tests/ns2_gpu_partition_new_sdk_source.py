#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
cpp=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_session.cpp").read_text()
hdr=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_session.h").read_text()
proto=(R/"encore-active/ns2-runtime/hle/core/hle/service/am/service/gpu_session_protocol.h").read_text()
for marker in (
    '{201, &IGpuSession::GetUserPartition, "GetUserPartition"}',
    '{202, &IGpuSession::GetGsvPartition, "GetGsvPartition"}',
    '{203, &IGpuSession::GetVaRange, "GetVaRange"}',
):
    assert marker in cpp, marker
# Lichii nx2 GSV: TIPC 0 is a real token-echo handshake before graphics init.
# A missing handler in the PS5-selected session returned NotImplemented and
# could stop newer SDKs before a single NVDRV GPFIFO or guest frame.
worker=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.cpp").read_text()
worker_hdr=(R/"encore-active/ns2-runtime/gpu_session/core/hle/service/am/service/gpu_worker.h").read_text()
for owner, owner_hdr, klass in ((cpp,hdr,"IGpuSession"),(worker,worker_hdr,"IGpuWorker")):
    assert '{0, &'+klass+'::Handshake, "Handshake"}' in owner
    assert "void Handshake(HLERequestContext& ctx);" in owner_hdr
    sig="void "+klass+"::Handshake(HLERequestContext& ctx) {"
    assert owner.count(sig)==1
    body=owner.split(sig,1)[1].split("\n}\n",1)[0]
    for needle in ("ctx.CommandBuffer()", "IPC::ResponseBuilder rb{ctx, 4};",
                   "rb.PushRaw<u32>(ResultSuccess.raw);",
                   "const u32 token_lo = command[2];",
                   "const u32 token_hi = command[3];",
                   "rb.PushRaw<u32>(token_lo);",
                   "rb.PushRaw<u32>(token_hi);"):
        assert needle in body, (klass,needle)
    # The production ResponseBuilder constructor clears ctx.CommandBuffer().
    # Reading token words after it is constructed silently echoes zero.
    assert body.index("command[2]") < body.index("command[3]")
    assert body.index("const u32 token_lo = command[2];") < body.index("IPC::ResponseBuilder rb{ctx, 4};")
    assert body.index("const u32 token_hi = command[3];") < body.index("IPC::ResponseBuilder rb{ctx, 4};")
    assert body.index("IPC::ResponseBuilder rb{ctx, 4};") < body.index("rb.PushRaw<u32>(token_lo);")
    ipc=(R/"encore-active/eden-core/include/core/hle/service/ipc_helpers.h").read_text()
    assert "std::memset(cmdbuf, 0, sizeof(u32) * IPC::COMMAND_BUFFER_LENGTH);" in ipc
    assert "return ServiceFrameworkBase::HandleSyncRequest(session, ctx);" in owner
    assert "handlers_tipc.contains(ctx.GetCommand())" in owner
assert "GetAddressSpacePartition" not in cpp
assert "GetAddressSpacePartition" not in hdr
for cmd in ("201","202","203"):
    assert f"DecodeAddressSpacePartition(command, {cmd})" in cpp
assert "command != 201 && command != 202 && command != 203" in proto
assert "first = Nvidia::Devices::NativeNs2GpuVaStart;" in cpp
assert "size = Nvidia::Devices::NativeNs2GpuVaEnd - first;" in cpp
assert "first = ~u64{0};" in cpp and "last = 0;" in cpp
assert "kind = 1;" in cpp
# The obsolete nine-u64 GetVARegions reply was a shifted older contract.
assert "std::array<u64, 9> values" not in cpp
print("NS2_GPU_PARTITION_NEW_SDK_SOURCE_PASS commands=201,202,203 upstream=Lichii")
