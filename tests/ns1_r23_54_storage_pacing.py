#!/usr/bin/env python3
"""NS1 R23 #54 bounded IStorage paced-read and generic service sleep removal."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
root=r/"encore-active/eden-core"
pins={
"src/core/hle/service/filesystem/fsp/fs_i_storage.cpp":"3513150b1a39297207bc16ccaadec8f1a8fdeb96",
"include/core/hle/service/filesystem/fsp/fs_i_storage.h":"26dbf4505aa00ac17967e2f1bd6c32a2e856bc78",
"src/core/hle/service/service.cpp":"de718afada4741c2c9834c0269cd6e2a27de4138",
"include/core/hle/service/service.h":"1b4f515b541617b6b8f134ff21570f7fbdce910f",
}
for path,expect in pins.items():
 data=(root/path).read_bytes()
 got=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
 assert got==expect,(path,got)
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
