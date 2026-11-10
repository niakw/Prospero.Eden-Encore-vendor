#!/usr/bin/env python3
"""G01: execute real native SPIR-V NonUniform capability-owner failure path.

A runtime (non-immediate) texture/image descriptor index must be rejected if
the selected Vulkan profile does not support that resource class's non-uniform
descriptor indexing. Keep immediates and all four supported classes intact.
No console, Vulkan shader submission or dynamic index OOB proof is claimed.
"""
from pathlib import Path
import os,subprocess,shutil,tempfile
ROOT=Path(__file__).resolve().parents[2]
src=(ROOT/"encore-active/ns2-runtime/ampere/shader_recompiler/backend/spirv/emit_spirv_image.cpp").read_text()
a="enum class NonUniformKind {"
b="\nclass ImageOperands {"
assert src.count(a)==src.count(b)==1
actual=a+src.split(a,1)[1].split(b,1)[0]
for needle in ("throw LogicError(\"Native dynamic image descriptor requires non-uniform indexing support\")",
               "if (index.IsImmediate())", "IsNonUniformSupported(ctx.profile, kind)",
               "DecorateNonUniform(ctx, idx)"):
 assert needle in actual,needle
assert actual.index("if (!IsNonUniformSupported") < actual.index("DecorateNonUniform(ctx, idx)")
cpp=r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <set>
#include <stdexcept>
using u32=std::uint32_t;
namespace spv {enum class Decoration{NonUniform};}
namespace Shader::Backend::SPIRV {
struct Id{u32 value{};};
namespace IR {
struct Value {
 bool immediate{};
 bool IsImmediate()const{return immediate;}
};
}
struct LogicError:std::runtime_error {using std::runtime_error::runtime_error;};
struct Profile{
 bool support_sampled_image_array_nonuniform_indexing{};
 bool support_storage_image_array_nonuniform_indexing{};
 bool support_uniform_texel_buffer_array_nonuniform_indexing{};
 bool support_storage_texel_buffer_array_nonuniform_indexing{};
};
struct EmitContext{
 Profile profile;
 std::set<u32> non_uniform_ids;
 unsigned decorations{};
 bool uses_nonuniform_sampled_image{};
 bool uses_nonuniform_storage_image{};
 bool uses_nonuniform_uniform_texel_buffer{};
 bool uses_nonuniform_storage_texel_buffer{};
 void Decorate(Id x,spv::Decoration kind){
  assert(kind==spv::Decoration::NonUniform);
  assert(x.value);
  ++decorations;
 }
};
__REAL_OWNER__
static bool Flag(const EmitContext& c,NonUniformKind kind) {
 switch(kind){
 case NonUniformKind::SampledImage:return c.uses_nonuniform_sampled_image;
 case NonUniformKind::StorageImage:return c.uses_nonuniform_storage_image;
 case NonUniformKind::UniformTexelBuffer:return c.uses_nonuniform_uniform_texel_buffer;
 case NonUniformKind::StorageTexelBuffer:return c.uses_nonuniform_storage_texel_buffer;
 }
 return false;
}
static void Enable(Profile& p,NonUniformKind kind) {
 switch(kind){
 case NonUniformKind::SampledImage:
  p.support_sampled_image_array_nonuniform_indexing=true;break;
 case NonUniformKind::StorageImage:
  p.support_storage_image_array_nonuniform_indexing=true;break;
 case NonUniformKind::UniformTexelBuffer:
  p.support_uniform_texel_buffer_array_nonuniform_indexing=true;break;
 case NonUniformKind::StorageTexelBuffer:
  p.support_storage_texel_buffer_array_nonuniform_indexing=true;break;
 }
}
}
int main(){
 using namespace Shader::Backend::SPIRV;
 constexpr std::array kinds{NonUniformKind::SampledImage,
  NonUniformKind::StorageImage,NonUniformKind::UniformTexelBuffer,
  NonUniformKind::StorageTexelBuffer};
 for(unsigned i=0;i<kinds.size();++i){
  EmitContext ctx;
  const Id idx{static_cast<u32>(42+i)};
  assert(!MarkNonUniform(ctx,idx,IR::Value{true},kinds[i]));
  assert(ctx.decorations==0 && !Flag(ctx,kinds[i]));
  bool rejected=false;
  try{(void)MarkNonUniform(ctx,idx,IR::Value{false},kinds[i]);}
  catch(const LogicError&){rejected=true;}
  assert(rejected&&ctx.decorations==0&&!Flag(ctx,kinds[i]));
  Enable(ctx.profile,kinds[i]);
  assert(MarkNonUniform(ctx,idx,IR::Value{false},kinds[i]));
  assert(ctx.decorations==1&&Flag(ctx,kinds[i]));
  assert(MarkNonUniform(ctx,idx,IR::Value{false},kinds[i]));
  assert(ctx.decorations==1); // duplicate decoration idempotent
  for(unsigned other=0;other<kinds.size();++other){
    if(other==i)continue;
    rejected=false;
    try{(void)MarkNonUniform(ctx,Id{static_cast<u32>(99+other)},
                            IR::Value{false},kinds[other]);}
    catch(const LogicError&){rejected=true;}
    assert(rejected&&!Flag(ctx,kinds[other]));
  }
 }
 std::puts("G01_NONUNIFORM_REAL_OWNER_CAPABILITY_PASS"
  " sampled=checked storage=checked texel=checked storage_texel=checked"
  " dynamic_unsupported=rejected dynamic_supported=decorated"
  " constant_unsupported=allowed OOB_dynamic=not_exercised");
}
""".replace("__REAL_OWNER__",actual)
with tempfile.TemporaryDirectory(prefix="g01-nonuniform-owner-") as temp:
 path=Path(temp)/"owner.cpp";path.write_text(cpp)
 for cc in dict.fromkeys(x for x in (shutil.which("clang++"),shutil.which("g++")) if x):
  for opt in ("-O0","-O2"):
   exe=Path(temp)/(Path(cc).name.replace("+","p")+opt)
   cmd=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
        "-fsanitize=undefined","-fno-sanitize-recover=all",str(path),"-o",str(exe)]
   built=subprocess.run(cmd,capture_output=True,text=True,timeout=60,env=os.environ.copy())
   assert built.returncode==0,(cc,built.stderr[-6000:])
   run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20,env=os.environ.copy())
   assert run.returncode==0,(cc,run.stderr[-6000:])
   print(Path(cc).name,opt,run.stdout.strip(),flush=True)
