#!/usr/bin/env python3
"""R23 #58 shader conditional Phi: exact source and selected diamond detection."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
header=root/"encore-active/eden-shader/include/shader_recompiler/frontend/ir/breadth_first_search.h"
source=root/"encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp"
def gitsha(path):
 raw=path.read_bytes()
 return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
assert gitsha(header)=="afe153f18075d00b8e822a1d992dcd8f5cad9a8c"
assert gitsha(source)=="fae69ab91b7d0ccd8fcb6d5a7a70f9083d278454"
h=header.read_text()
s=source.read_text()
assert "template <typename Pred, typename Resolve = std::identity>" in h
assert "value = resolve(value);" in h
assert "const Value arg_value{resolve(inst->Arg(arg))};" in h
assert s.count("env.track_cache.clear();")>=2
assert "ResolveConditionalPhi(operand, block)" in s
assert "std::ranges::find(visited, block)" in s
start="const IR::Block* ConditionalHeader(const IR::Block* body) {"
end="\nstd::pair<IR::Value, bool> Condition("
func=start+s.split(start,1)[1].split(end,1)[0].rstrip()
code=r"""
#include <cassert>
#include <vector>
#include <cstdint>
namespace IR {
enum class Opcode {ConditionRef,Other};
struct Inst {
 Opcode code=Opcode::Other;
 Opcode GetOpcode() const {return code;}
};
struct Block {
 std::vector<const Block*> preds,succs;
 std::vector<Inst> items;
 auto ImmPredecessors() const {return preds;}
 auto ImmSuccessors() const {return succs;}
 bool empty() const {return items.empty();}
 const Inst& back() const {return items.back();}
};
}
""" + func + r"""
int main(){
 IR::Block header,body,merge;
 header.items.push_back({IR::Opcode::ConditionRef});
 header.succs={&body,&merge};
 body.preds={&header};body.succs={&merge};
 merge.preds={&body,&header};
 assert(ConditionalHeader(&body)==&header);
 header.items.back().code=IR::Opcode::Other;
 assert(!ConditionalHeader(&body));
 header.items.back().code=IR::Opcode::ConditionRef;
 merge.preds={&body};
 assert(!ConditionalHeader(&body));
 merge.preds={&body,&header};
 body.preds.push_back(&merge);
 assert(!ConditionalHeader(&body));
}
""";
with tempfile.TemporaryDirectory(prefix="r23-phi-") as td:
 p=Path(td)
 (p/"t.cpp").write_text(code)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
    "-fsanitize=address,undefined",str(p/"t.cpp"),"-o",str(p/"test")],check=True,timeout=70)
 subprocess.run([str(p/"test")],check=True,timeout=20,
    env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_58_CONDITIONAL_PHI_HEADER_ASAN_UBSAN_PASS diamond=1 wrong_header=3")
print("R23_58_FULL_SPIRV_GPU_AND_PHI_GOLDEN_UNQUALIFIED")
