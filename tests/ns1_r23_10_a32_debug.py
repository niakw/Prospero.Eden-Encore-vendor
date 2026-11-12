#!/usr/bin/env python3
"""R23 #10: preserve A32 debugger memory halts on optimized native IR."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

p=Path(__file__).resolve().parents[1]/"encore-active/eden-core/src/dynarmic/ir/opt_passes.cpp"
s=p.read_text()
guard="conf.HasOptimization(OptimizationFlag::GetSetElimination) && !conf.check_halt_on_memory_access"
assert s.count(guard)==2 # already present A64, now also A32
old="    if (conf.HasOptimization(OptimizationFlag::GetSetElimination)) {\n        Optimization::A32GetSetElimination(block, {.convert_nzc_to_nz = true});"
new="    if ("+guard+") {\n        Optimization::A32GetSetElimination(block, {.convert_nzc_to_nz = true});"
assert s.count(new)==1
data=s.replace(new,old,1).encode()
sha=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
assert sha=="aaf6cc1de3995b2b648576b49007c26491a94c68",sha
test=r"""
#include <cassert>
struct OptimizationFlag {static constexpr int GetSetElimination=1;};
struct Config {
 bool enabled=false,check_halt_on_memory_access=false;
 bool HasOptimization(int) const {return enabled;}
};
bool allow(const Config& conf) {
 return """ + guard + r""";
}
int main(){
 assert(!allow({false,false}));
 assert(!allow({false,true}));
 assert(allow({true,false}));
 assert(!allow({true,true}));
}
""";
with tempfile.TemporaryDirectory(prefix="a32-halt-") as temp:
 folder=Path(temp)
 (folder/"test.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1",
  "-fsanitize=address,undefined",str(folder/"test.cpp"),"-o",str(folder/"test")],
  check=True,timeout=60)
 subprocess.run([str(folder/"test")],check=True,timeout=20,
  env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_A32_MEMORY_DEBUG_HALT_GUARD_SOURCE_PASS disabled_on_debug=1")
