#!/usr/bin/env python3
"""R23 #58 shader conditional Phi: exact source and selected diamond detection."""
from pathlib import Path
import hashlib, os, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
header=root/"encore-active/eden-shader/include/shader_recompiler/frontend/ir/breadth_first_search.h"
source=root/"encore-active/eden-shader/src/shader_recompiler/ir_opt/texture_pass.cpp"
def gitsha(path):
 raw=path.read_bytes()
 return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
header_sha=gitsha(header)
assert header_sha in {"afe153f18075d00b8e822a1d992dcd8f5cad9a8c",
                      "194476705f21bd1c27f532fdd5cc04d99a3cd620",
                      "fa5e7572ef99bba3db17154c63f60dddba7a87c6"}
if header_sha in {"194476705f21bd1c27f532fdd5cc04d99a3cd620",
                  "05a5f9c9c99ca92530bb80f079ae045951c43dae"}:
    # The new header must preserve the exact predecessor's traversal and
    # conditional resolver behavior, not merely pass a new SHA inventory.
    subprocess.run(["python3","-B",str(root/"tests/ps5_ir_bfs_adaptive_host.py")],
                   check=True,timeout=180)
source_sha=gitsha(source)
assert source_sha in {"85c523e435ee8803ef3d2087567aa1a4b5d09369",
                      "3d9625f1981d6500e7b294018e0659464869d6f3",
                      "889456a31ddeb87e411ce4d0d2d41078b9593244"}
h=header.read_text()
s=source.read_text()
if source_sha=="889456a31ddeb87e411ce4d0d2d41078b9593244":
    # Lichii #1914 only adds the found-bit descriptor bound after the
    # conditional-Phi owner. Reverse that exact delta before accepting the
    # current source as the same reviewed Phi implementation.
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
    assert s.count(found_bit)==1 and s.count(post_count)==1
    predecessor=s.replace(found_bit,"",1).replace(post_count,pre_count,1).encode()
    predecessor_sha=hashlib.sha1(b"blob "+str(len(predecessor)).encode()+bytes([0])+predecessor).hexdigest()
    assert predecessor_sha=="3d9625f1981d6500e7b294018e0659464869d6f3",predecessor_sha
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
cxx = os.getenv("CXX","clang++")
apple_clang_without_lsan = (
    sys.platform == "darwin" and
    "Apple clang" in subprocess.check_output(
        [cxx, "--version"], text=True, stderr=subprocess.STDOUT)
)
with tempfile.TemporaryDirectory(prefix="r23-phi-") as td:
 p=Path(td)
 (p/"t.cpp").write_text(code)
 subprocess.run([cxx,"-std=c++20","-O1","-g",
    "-fsanitize=address,undefined",str(p/"t.cpp"),"-o",str(p/"test")],check=True,timeout=70)
 subprocess.run([str(p/"test")],check=True,timeout=20,
    env={**os.environ,"ASAN_OPTIONS":"detect_leaks=" + ("0" if apple_clang_without_lsan else "1") + ":halt_on_error=1"})
print("R23_58_CONDITIONAL_PHI_HEADER_ASAN_UBSAN_PASS diamond=1 wrong_header=3")
print("R23_58_FULL_SPIRV_GPU_AND_PHI_GOLDEN_UNQUALIFIED")
