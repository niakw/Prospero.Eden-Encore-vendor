#!/usr/bin/env python3
"""R23 #80: exact shader/Vulkan owners + extracted PositionXY shuffle predicate."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
files={
"encore-active/eden-shader/src/shader_recompiler/ir_opt/vendor_workaround_pass.cpp":"5842f6135b8db659764c52e25cd5ec577358e28b",
"encore-active/eden-shader/src/shader_recompiler/ir_opt/passes.h":"068d7fdfe8382de0934fb14597be980c0390f141",
"encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp":"71f536fe851f9543c7244f76e08bae694926739c"
}
for p,sha in files.items():
 d=(root/p).read_bytes()
 actual=hashlib.sha1(b"blob "+str(len(d)).encode()+bytes([0])+d).hexdigest()
 assert sha==actual,(p,actual)
shader=(root/"encore-active/eden-shader/src/shader_recompiler/ir_opt/vendor_workaround_pass.cpp").read_text()
pipeline=(root/"encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp").read_text()
assert "device.GetDriverID() != VK_DRIVER_ID_NVIDIA_PROPRIETARY" in pipeline
assert "Shader::Optimization::HasBrokenPattern(programs[index])" in pipeline
start="bool HasBrokenPattern(const IR::Program& program) {"
end="\n} // namespace Shader::Optimization"
assert shader.count(start)==1
method=start+shader.split(start,1)[1].split(end,1)[0]
fixture=r"""
#include <array>
#include <cassert>
#include <cstddef>
#include <vector>
namespace Shader {
enum class Stage {Fragment,Vertex,Compute};
namespace IR {
enum class Opcode {ShuffleIndex,ShuffleUp,ShuffleDown,ShuffleButterfly,BitCastU32F32,GetAttribute,Other};
enum class Attribute {PositionX,PositionY,Other};
struct Inst;
struct Value {
 const Inst* source=nullptr;
 bool immediate=true;
 IR::Attribute attr=IR::Attribute::Other;
 bool IsImmediate() const{return immediate;}
 const Inst* InstRecursive() const{return source;}
 IR::Attribute Attribute() const{return attr;}
};
struct Inst {
 Opcode op=Opcode::Other;
 std::array<Value,2> args{};
 Opcode GetOpcode() const{return op;}
 Value Arg(std::size_t i) const{return args.at(i);}
};
struct Block {
 std::vector<Inst> instructions;
 const std::vector<Inst>& Instructions() const{return instructions;}
};
struct Program {
 Stage stage=Stage::Fragment;
 std::vector<Block*> post_order_blocks;
};
}
namespace Optimization {
""" + method + r"""
}
}
int main(){
 using namespace Shader;
 IR::Inst attribute{IR::Opcode::GetAttribute};
 attribute.args[0].attr=IR::Attribute::PositionX;
 IR::Inst bitcast{IR::Opcode::BitCastU32F32};
 bitcast.args[0].source=&attribute;bitcast.args[0].immediate=false;
 IR::Inst shuffle{IR::Opcode::ShuffleIndex};
 shuffle.args[0].source=&bitcast;shuffle.args[0].immediate=false;
 IR::Block block;
 block.instructions.push_back(shuffle);
 IR::Program program{Stage::Fragment,{&block}};
 assert(Optimization::HasBrokenPattern(program));
 program.stage=Stage::Vertex;
 assert(!Optimization::HasBrokenPattern(program));
 program.stage=Stage::Fragment;
 attribute.args[0].attr=IR::Attribute::Other;
 assert(!Optimization::HasBrokenPattern(program));
 attribute.args[0].attr=IR::Attribute::PositionY;
 assert(Optimization::HasBrokenPattern(program));
 block.instructions[0].op=IR::Opcode::Other;
 assert(!Optimization::HasBrokenPattern(program));
 block.instructions[0].op=IR::Opcode::ShuffleButterfly;
 assert(Optimization::HasBrokenPattern(program));
 block.instructions[0].args[0].immediate=true;
 assert(!Optimization::HasBrokenPattern(program));
}
""";
with tempfile.TemporaryDirectory(prefix="r23-broken-pattern-") as tmp:
 p=Path(tmp)
 (p/"test.cpp").write_text(fixture)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
                 "-fsanitize=address,undefined",str(p/"test.cpp"),"-o",str(p/"test")],check=True,timeout=70)
 subprocess.run([str(p/"test")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_80_NON_NVIDIA_FRAGMENT_SHUFFLE_HOST_PASS three_sources=1 positive_negative=7")
