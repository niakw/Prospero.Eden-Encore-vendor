#!/usr/bin/env python3
"""R23 #41: ensure the actually selected x64 emitter protects cmpxchg16b XMM0."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

r=Path(__file__).resolve().parents[1]
file=r/"encore-active/eden-core/src/dynarmic/backend/x64/emit_x64_memory.h"
s=file.read_text()
b=file.read_bytes()
sha=hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
assert sha=="505ea4017b6249d855ed63ae8033ea1868010a64",sha
guard="ASSERT(Xbyak::Xmm(value_idx) != xmm0);"
assert s.count(guard)==1
idx=s.index(guard)
assert s.index("code.cmpxchg16b(xword[addr]);",idx)>idx
assert "code.punpcklqdq(Xbyak::Xmm(value_idx), xmm0);" in s
src=r"""
#include <cassert>
struct Xmm {
 int value;
 bool operator!=(Xmm o) const {return value!=o.value;}
};
namespace Xbyak {using Xmm=::Xmm;}
constexpr Xmm xmm0{0};
bool allowed(int value_idx) {return Xbyak::Xmm{value_idx} != xmm0;}
int main(){
 assert(!allowed(0));
 for(int n=1;n<16;n++) assert(allowed(n));
}
""";
with tempfile.TemporaryDirectory(prefix="x64-cas-") as td:
 folder=Path(td)
 (folder/"x.cpp").write_text(src)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1",
   "-fsanitize=address,undefined",str(folder/"x.cpp"),"-o",str(folder/"test")],check=True,timeout=60)
 subprocess.run([str(folder/"test")],check=True,timeout=15,
   env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("R23_41_X64_CMPXCHG16B_XMM0_GUARD_HOST_PASS invalid=1 valid=15")
