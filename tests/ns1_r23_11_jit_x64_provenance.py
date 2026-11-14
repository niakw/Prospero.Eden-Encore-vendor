#!/usr/bin/env python3
"""NS1 R23 #11: exact four full maintained x64 source deltas + host contracts.

The portable positive/negative fixtures are not a complete native Dynarmic JIT.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"encore-active/eden-core/src/dynarmic/backend/x64"
def gitblob(s):
 b=s.encode()
 return hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
def source(file,sha,back):
 s=(BASE/file).read_text()
 original=back(s)
 assert gitblob(original)==sha,(file,gitblob(original),sha)
 print("R23_11_SELECTED_JIT_SOURCE_PARITY_PASS",file)
 return s
fp_old="    auto const exact = ((extra_args >> 16) & 0xff) != 0;\n    auto const rounding_mode = FP::RoundingMode((extra_args >> 8) & 0xff);"
fp_new="    auto const exact = ((extra_args >> 8) & 0xff) != 0;\n    auto const rounding_mode = FP::RoundingMode((extra_args >> 0) & 0xff);"
source("emit_x64_floating_point.cpp","756a6f0a36d00e07c8ef723d4466b475601a6c16",lambda s:s.replace(fp_new,fp_old,1))
sm4_old="    code.lea(tmp1, code.ptr[std::addressof(substitution_box)]);"
sm4_new="    code.mov(tmp1, std::bit_cast<u64>(substitution_box.data()));"
sm4=source("emit_x64_sm4.cpp","fc30c0e15853a75055561790221fc513d57cefeb",lambda s:s.replace(sm4_new,sm4_old,1).replace("#include <bit>\n", "",1))
assert sm4.count(sm4_new)==1
v_old="        /*4*/ code.vplzcntd(tmp0.cvt256(), tmp0.cvt256());\n        /*1*/ code.vpxor(tmp1, tmp1, tmp1);\n        /*1*/ code.vpackusdw(tmp0, tmp0, tmp1);"
v_new="        /*4*/ code.vplzcntd(tmp0.cvt256(), tmp0.cvt256());\n        // AVX-512 VPACKUSDW consumes low 128-bit halves; restore lanes 4-7.\n        /*3*/ code.vextracti128(tmp1, tmp0.cvt256(), 1);\n        /*1*/ code.vpackusdw(tmp0, tmp0, tmp1);"
# C04 independently qualified the lane-shift result type in this same TU.
# Reverse all three reviewed deltas before matching the frozen preimage.
shift_old = "auto const shifted = U(element) << U(T(shift_amount));"
shift_new = "U const shifted = U(U(element) << U(T(shift_amount)));"
result_old = "auto result = 0;"
result_new = "T result = 0;"
def recover_vector(s):
    assert s.count(v_new) == s.count(shift_new) == s.count(result_new) == 1
    return (s.replace(v_new,v_old,1).replace(shift_new,shift_old,1)
             .replace(result_new,result_old,1))
source("emit_x64_vector.cpp","4afc50c77b167d6e36d180e04c5cf58259db62b7",recover_vector)
round_old="    ROUNDING_MODE_CASE(CASE, 0x3f)\n"
round_new="    ROUNDING_MODE_CASE(CASE, 0x3f) " + chr(92) + "\n    ROUNDING_MODE_CASE(CASE, 0x40)\n"
source("emit_x64_vector_floating_point.cpp","0ad3b022d0e27769036310ec02522d9280f136a3",lambda s:s.replace(round_new,round_old,1))
test=r"""
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
using u32=std::uint32_t;
using u64=std::uint64_t;
struct Decoded{bool exact;u32 rounding;};
Decoded decode(u32 extra_args) {
 return {((extra_args >> 8)&0xff)!=0,(extra_args >> 0)&0xff};
}
int main(){
 auto a=decode(0x00000103);
 assert(a.exact && a.rounding==3);
 a=decode(0x00000004);
 assert(!a.exact && a.rounding==4);
 a=decode(0x00010000);
 assert(!a.exact && a.rounding==0);
 std::array<unsigned char,256> sbox{};
 auto addr=std::bit_cast<u64>(sbox.data());
 assert(addr==reinterpret_cast<u64>(sbox.data()));
 std::array<u32,8> cnt={1,2,3,4,5,6,7,8};
 std::array<u32,4> high{};
 for(int i=0;i<4;i++) high[i]=cnt[i+4];
 assert(high[0]==5 && high[3]==8);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-jit-") as td:
 d=Path(td)
 (d/"t.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
  "-fsanitize=address,undefined",str(d/"t.cpp"),"-o",str(d/"test")],check=True,timeout=60)
 subprocess.run([str(d/"test")],check=True,timeout=15,
  env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_11_JIT_HOST_CONTRACTS_PASS fp_bits=3 sm4_pointer=1 avx_lane_extract=4")
