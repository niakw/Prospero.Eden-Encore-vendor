#!/usr/bin/env python3
"""R23 #58: exact maintained shader CFG methods with diamond/Phi host fixture."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
r=Path(__file__).resolve().parents[1]/"encore-active/eden-shader"
names={
 "src/shader_recompiler/ir_opt/texture_pass.cpp":{"85c523e435ee8803ef3d2087567aa1a4b5d09369","3d9625f1981d6500e7b294018e0659464869d6f3","889456a31ddeb87e411ce4d0d2d41078b9593244"}
}
for path,expected in names.items():
 b=(r/path).read_bytes()
 sha=hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
 assert sha in expected,(path,sha,expected)
 # Current vendor adds only Lichii #1914's found-bit descriptor bound after
 # the Phi owner this test validates. Reverse that exact successor to prove
 # the current hash is not an unrelated shader-pass drift.
 if path.endswith("texture_pass.cpp") and sha=="889456a31ddeb87e411ce4d0d2d41078b9593244":
  text=b.decode()
  found_bit="""// The elements a dynamic offset can select below the clamp to the last one: a bit index found by
// FLO (FindUMsb, or 31 minus it for FLO.SH) takes one of 32. Its -1 for no bit set is past any
// count, so it takes the last element as it does in a longer array.
// Upstream Lichii 5e33a765 / #1914.
u32 DynamicDescriptorIndexLimit(const IR::U32& dynamic_offset, u32 size_shift) {
    const IR::Inst* const offset = dynamic_offset.InstRecursive();
    if (!offset || offset->GetOpcode() != IR::Opcode::ShiftLeftLogical32 ||
        !offset->Arg(1).IsImmediate() || offset->Arg(1).U32() != size_shift) {
        return ~0U;
    }
    const auto is_bit_index = [](const IR::Value& value) {
        const IR::Inst* inst = value.IsImmediate() ? nullptr : value.InstRecursive();
        if (inst && inst->GetOpcode() == IR::Opcode::BitwiseXor32 && inst->Arg(1).IsImmediate() &&
            inst->Arg(1).U32() == 31) {
            inst = inst->Arg(0).IsImmediate() ? nullptr : inst->Arg(0).InstRecursive();
        }
        return inst && (inst->GetOpcode() == IR::Opcode::FindUMsb32 ||
                        inst->GetOpcode() == IR::Opcode::FindSMsb32);
    };
    const IR::Value index = offset->Arg(0);
    const IR::Inst* const select = index.IsImmediate() ? nullptr : index.InstRecursive();
    if (select && select->GetOpcode() == IR::Opcode::SelectU32) {
        return is_bit_index(select->Arg(1)) && is_bit_index(select->Arg(2)) ? 32U : ~0U;
    }
    return is_bit_index(index) ? 32U : ~0U;
}

"""
  post_count="""        .count = (std::min)(DynamicDescriptorCount(base_offset, size_shift,
                                                   (std::min)({DESCRIPTOR_MAX_COUNT, sampled_limit,
                                                               resource_limit})),
                            DynamicDescriptorIndexLimit(dynamic_offset, size_shift)),
"""
  pre_count="""        .count = DynamicDescriptorCount(base_offset, size_shift, (std::min)({DESCRIPTOR_MAX_COUNT, sampled_limit, resource_limit})),
"""
  assert text.count(found_bit)==1 and text.count(post_count)==1
  predecessor=text.replace(found_bit,"",1).replace(post_count,pre_count,1).encode()
  predecessor_sha=hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()
  assert predecessor_sha=="3d9625f1981d6500e7b294018e0659464869d6f3",predecessor_sha
header=r/"include/shader_recompiler/frontend/ir/breadth_first_search.h"
raw=header.read_bytes()
header_sha=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
assert header_sha in {
 "afe153f18075d00b8e822a1d992dcd8f5cad9a8c",
 "194476705f21bd1c27f532fdd5cc04d99a3cd620",
 "fa5e7572ef99bba3db17154c63f60dddba7a87c6",
},header_sha
if header_sha!="afe153f18075d00b8e822a1d992dcd8f5cad9a8c":
 subprocess.run(["python3","-B",str(Path(__file__).resolve().parents[1]/"tests/ps5_ir_bfs_adaptive_host.py")],
                check=True,timeout=180)
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
