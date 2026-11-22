#!/usr/bin/env python3
"""NS1 R23 #54 bounded IStorage paced-read and generic service sleep removal."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
root=r/"encore-active/eden-core"
pins={
"src/core/hle/service/filesystem/fsp/fs_i_storage.cpp":"3513150b1a39297207bc16ccaadec8f1a8fdeb96",
"include/core/hle/service/filesystem/fsp/fs_i_storage.h":"26dbf4505aa00ac17967e2f1bd6c32a2e856bc78",
"src/core/hle/service/service.cpp":"e388765fb2cf372adc01c2c3ec174955659ccc2a",
"include/core/hle/service/service.h":"b871694b3b865465ec8e08e358d6799449092498",
}
for path, expected in pins.items():
    raw=(root/path).read_bytes()
    actual=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
    assert actual==expected, (path, actual)
service_owner=(root/"src/core/hle/service/service.cpp").read_text()
service_header=(root/"include/core/hle/service/service.h").read_text()
# NS1 still has explicit fail-closed unknown-command handling and the
# reviewed optional no-op subset. NX2 placeholder stubs are process-scoped.
assert "IsSafeOptionalNoOp(" in service_owner
assert "Kernel::ResultNotImplemented" in service_owner
assert "IsNx2PlaceholderCaller(ctx, service_name)" in service_owner
assert "process->GetProgramId() >> 56" in service_owner
assert "void ServiceFrameworkBase::StubReplyZeroed(" in service_owner
assert "void ServiceFrameworkBase::StubReplyEvent(" in service_owner
assert "void ServiceFrameworkBase::StubReplyInterface(" in service_owner
assert "std::unique_ptr<KernelHelpers::ServiceContext> stub_context" in service_header

source=(root/"src/core/hle/service/filesystem/fsp/fs_i_storage.cpp").read_text()
service=(root/"src/core/hle/service/service.cpp").read_text()
assert "std::this_thread::sleep_for(std::chrono::microseconds{600})" not in service
assert "is_i_storage" not in service
assert "system.IsNVDECActiveForProcess" not in service
assert "next_read_tick" in source
assert "out_bytes.size()" in source
start="    R_UNLESS(length >= 0, FileSys::ResultInvalidSize);"
end="    // Read the data from the Storage backend"
block=start+source.split(start,1)[1].split(end,1)[0]
code=r"""
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
using u64=std::uint64_t;
using s64=std::int64_t;
using u8=std::uint8_t;
namespace FileSys { constexpr int ResultInvalidSize=1, ResultInvalidOffset=2;}
#define R_UNLESS(condition,error) if (!(condition)) return (error)
struct IStorage {
 static constexpr s64 ReadBytesPerSecond=40'000'000;
 static constexpr s64 MaxChargedReadSize=64*1024;
 std::chrono::steady_clock::time_point next_read_tick{};
 int Read(s64 offset,s64 length,std::span<u8> out_bytes) {
""" + block + r"""
 return 0;
 }
};
int main(){
 IStorage s;
 u8 bytes[4096]{};
 assert(s.Read(0,-1,bytes)==1);
 assert(s.Read(-1,1,bytes)==2);
 assert(s.Read(0,4097,bytes)==1);
 auto before=std::chrono::steady_clock::now();
 assert(s.Read(0,4096,bytes)==0);
 auto tick=s.next_read_tick;
 assert(tick>before);
 assert(s.Read(0,4096,bytes)==0);
 assert(s.next_read_tick>tick);
}
""";
with tempfile.TemporaryDirectory(prefix="ns1-storage-54-") as t:
 p=Path(t)
 (p/"test.cpp").write_text(code)
 subprocess.run([os.environ.get("CXX","clang++"),"-std=c++20","-g","-O1",
  "-fsanitize=address,undefined",str(p/"test.cpp"),"-o",str(p/"test")],check=True,timeout=70)
 subprocess.run([str(p/"test")],check=True,timeout=20,
  env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_54_STORAGE_PACING_ASAN_UBSAN_PASS bound=1 40MBps=1 global_sleep=0")
