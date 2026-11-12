#!/usr/bin/env python3
"""R23 #58: exact maintained shader CFG methods with diamond/Phi host fixture."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
r=Path(__file__).resolve().parents[1]/"encore-active/eden-shader"
names={
 "include/shader_recompiler/frontend/ir/breadth_first_search.h":"afe153f18075d00b8e822a1d992dcd8f5cad9a8c",
 "src/shader_recompiler/ir_opt/texture_pass.cpp":"85c523e435ee8803ef3d2087567aa1a4b5d09369"
}
for path,expected in names.items():
 b=(r/path).read_bytes()
 sha=hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
 assert sha==expected,(path,sha)
bfs=(r/"include/shader_recompiler/frontend/ir/breadth_first_search.h").read_text()
texture=(r/"src/shader_recompiler/ir_opt/texture_pass.cpp").read_text()
assert "template <typename Pred, typename Resolve = std::identity>" in bfs
assert "value = resolve(value);" in bfs
assert "resolve(inst->Arg(arg))" in bfs
start="const IR::Block* ConditionalHeader(const IR::Block* body) {"
end="constexpr u32 DESCRIPTOR_SIZE = 8;"
assert texture.count(start)==1 and texture.count(end)==1
extract=start+texture.split(start,1)[1].split(end,1)[0]
assert extract.count("std::ranges::find(visited, block)")==1
assert texture.count("env.track_cache.clear();")>=2

fixture=r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>
namespace boost::container {
 template<class T,std::size_t N> using small_vector=std::vector<T>;
}
namespace IR {
 struct Inst;
 struct Block;
 enum class Opcode {ConditionRef,LogicalNot,Phi,Copy};
 struct Value {
  const Inst* inst{};
  int imm{};
  constexpr Value()=default;
  constexpr Value(int n):inst(nullptr),imm(n){}
  constexpr Value(const Inst* p):inst(p),imm(0){}
  bool IsImmediate() const{return inst==nullptr;}
  const Inst* TryInstRecursive() const{return inst;}
  const Inst* InstRecursive() const{assert(inst);return inst;}
  Value Resolve() const {return *this;}
  bool IsPhi() const;
  friend bool operator==(Value a,Value b){return a.inst==b.inst && a.imm==b.imm;}
 };
 struct Inst {
  Opcode op{};
  std::vector<Value> args{};
  std::vector<const Block*> phi_block{};
  Opcode GetOpcode() const {return op;}
  Value Arg(std::size_t i) const{return args.at(i);}
  std::size_t NumArgs() const {return args.size();}
  const Block* PhiBlock(std::size_t i) const{return phi_block.at(i);}
 };
 bool Value::IsPhi() const {return inst && inst->op==Opcode::Phi;}
 struct Block {
  std::vector<const Block*> prev{},next{};
  std::vector<Inst> instructions{};
  const auto& ImmPredecessors() const {return prev;}
  const auto& ImmSuccessors() const{return next;}
  bool empty() const{return instructions.empty();}
  const Inst& back() const{return instructions.back();}
 };
}
""" + extract + r"""
int main(){
 using namespace IR;
 Block outer,body,join,inner,merge;
 // The immediate condition is a real singleton value for each branch.
 const Value cond{77};
 outer.next={&body,&join};
 outer.instructions.push_back({Opcode::ConditionRef,{cond},{}});
 body.prev={&outer};body.next={&join};
 join.prev={&body,&outer};join.next={&inner,&merge};
 join.instructions.push_back({Opcode::ConditionRef,{cond},{}});
 inner.prev={&join};inner.next={&merge};
 merge.prev={&inner,&join};
 assert(ConditionalHeader(&body)==&outer);
 assert(ConditionalHeader(&inner)==&join);
 assert(ConditionalHeader(&join)==nullptr);
 Inst phi{Opcode::Phi,{Value{10},Value{20}},{&body,&outer}};
 assert(ResolveConditionalPhi(Value{&phi},&inner)==Value{10});
 // Logical negation chooses the opposite incoming Phi edge.
 Inst notOp{Opcode::LogicalNot,{cond},{}};
 join.instructions.back().args={Value{&notOp}};
 assert(ResolveConditionalPhi(Value{&phi},&inner)==Value{20});
 // Non-Phi and non-conditional block remain unchanged.
 assert(ResolveConditionalPhi(Value{22},&inner)==Value{22});
 assert(ResolveConditionalPhi(Value{&phi},&outer)==Value{&phi});
}
""";
with tempfile.TemporaryDirectory(prefix="ns1-phi58-") as tmp:
 p=Path(tmp)
 (p/"test.cpp").write_text(fixture)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-g","-O1",
 "-fsanitize=address,undefined","-fno-omit-frame-pointer",
 str(p/"test.cpp"),"-o",str(p/"test")],check=True,timeout=90)
 subprocess.run([str(p/"test")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("R23_58_BINDLESS_PHI_CFG_ASAN_UBSAN_PASS diamond=1 negated=1 baseline=1")
