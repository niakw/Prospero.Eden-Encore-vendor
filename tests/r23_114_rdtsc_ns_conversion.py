#!/usr/bin/env python3
"""R23 #114: exact maintained WallClock nanoseconds -> native tick method.

This compiles the exact source body under Clang/ASan+UBSan; not a PS5 ELF.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"encore-active/eden-core/src/common/cpu_features.cpp"
HEADER=ROOT/"encore-active/eden-core/include/common/cpu_features.h"
s=SOURCE.read_text()
h=HEADER.read_text()
start="u64 WallClock::NsToTicks(std::chrono::nanoseconds ns) const {"
end="\n#elif defined(HAS_NCE)"
assert s.count(start)==1 and s.count(end)>=1
method=start+s.split(start,1)[1].split(end,1)[0]
original="    return invariant ? MultiplyHigh(ns.count(), rdtsc_ns_factor) : ns.count();"
assert "unsigned __int128" in method and "product / NsRatio::den" in method
old_init="    , rdtsc_ns_factor{invariant_ ? GetFixedPoint64Factor(rdtsc_frequency_, NsRatio::den) : 1}\n"
init="    , ms_rdtsc_factor{invariant_ ? GetFixedPoint64Factor(MsRatio::den, rdtsc_frequency_) : 0}\n"
assert s.count(init)==1 and old_init not in s
old=s.replace(method,start+"\n"+original+"\n}").replace(init,init+old_init,1)
def sha(raw):
    raw=raw.encode()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
assert sha(old)=="f00fba4d5b21be3fc3f3089678d7714a57ae660e",sha(old)
assert "rdtsc_ns_factor" not in h
old_header=h.replace("    u64 rdtsc_frequency;","    u64 rdtsc_frequency;\n    u64 rdtsc_ns_factor;",1)
# The old member was next to multiple factor fields; reconstruct at exact location.
if sha(old_header)!="4c3c975ec9af59bf07f0a2b2c37e0053546733bc":
    old_header=h.replace("    u64 ms_rdtsc_factor;","    u64 ms_rdtsc_factor;\n    u64 rdtsc_ns_factor;",1)
assert sha(old_header)=="4c3c975ec9af59bf07f0a2b2c37e0053546733bc",sha(old_header)
print("R23_114_REAL_NATIVE_SOURCE_BLOBS_PASS")
code=r"""
#include <cassert>
#include <chrono>
#include <cstdint>
#include <ratio>
using u64=std::uint64_t;
struct WallClock {
 using NsRatio=std::nano;
 bool invariant;
 u64 rdtsc_frequency;
 u64 NsToTicks(std::chrono::nanoseconds ns) const;
};
""" + method + r"""
int main() {
 using namespace std::chrono;
 for(u64 freq: {19'200'000ULL, 1'020'000'000ULL,2'000'000'000ULL,
                3'000'000'000ULL,4'000'000'000ULL}) {
   WallClock w{true,freq};
   auto oracle=[&](std::int64_t n) {
     if(n<=0) return 0ULL;
     return static_cast<u64>((static_cast<unsigned __int128>(n)*freq)/1000000000ULL);
   };
   u64 prior=0;
   for(auto n : {0LL,1LL,9LL,100LL,1000LL,1000000LL,123456789LL,
                 1000000000LL,3600000000000LL}) {
     auto result=w.NsToTicks(nanoseconds{n});
     assert(result==oracle(n));
     assert(result>=prior);
     prior=result;
   }
   assert(w.NsToTicks(nanoseconds{-1})==0);
 }
 WallClock fallback{false,0};
 assert(fallback.NsToTicks(nanoseconds{456})==456);
 assert(WallClock{true,3'000'000'000ULL}.NsToTicks(nanoseconds{1000000000})==3'000'000'000ULL);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-clock-") as temp:
    p=Path(temp)
    src=p/"clock.cpp"
    src.write_text(code.replace("#include <ratio>","#include <ratio>\n#include <initializer_list>"))
    subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
      "-fsanitize=address,undefined",str(src),"-o",str(p/"clock")],check=True,timeout=60)
    subprocess.run([str(p/"clock")],check=True,timeout=15,
       env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_114_HOST_CLOCK_PASS frequencies=5 signed_zero=1 monotonic=1")
