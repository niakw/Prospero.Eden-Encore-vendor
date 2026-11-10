#!/usr/bin/env python3
"""H05: execute ReleaseBuffer and ReserveFence owners with real guest fence IDs.

Reject a fabricated external syncpoint, a recycled ID, zero or an invalid
NoSyncpoint sentinel before any image ownership transfer. Host fixture only.
"""
from pathlib import Path
import os,subprocess,shutil,tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/"encore-active/ns2-runtime/video_decoder/core/hle/service/am/service/graphics_video_decoder_session.cpp").read_text()
def cut(first,after):
 assert source.count(first)==source.count(after)==1,(first,after)
 return first+source.split(first,1)[1].split(after,1)[0]
real_fences=cut("bool IGraphicsVideoDecoderSession::EnsureSyncpoint() {",
                "\nvoid IGraphicsVideoDecoderSession::RegisterBuffer(")
real_release=cut("void IGraphicsVideoDecoderSession::ReleaseBuffer(",
                 "\nvoid IGraphicsVideoDecoderSession::UnregisterBuffer(")
for predicate in ("release_id == NoSyncpoint && release_value == 0",
                  "release_id == 0 || release_id >= MaxSyncpoints",
                  "IsSyncpointAllocated(release_id)"):
 assert predicate in real_release,predicate
assert real_release.index("const bool no_release_fence") < real_release.index("IsReadyGuest(")
assert real_release.index("IsSyncpointAllocated(release_id)") < real_release.index("ReserveFence()")
assert real_release.index("ReserveFence()") < real_release.index("m_guest_owned.erase(image);")
cpp=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>
using u32=std::uint32_t;
namespace Kernel {
constexpr u32 ResultInvalidHandle=6,ResultBusy=8,ResultOutOfResource=7;
}
constexpr u32 ResultSuccess=0;
struct HLERequestContext {
 std::array<u32,8> words{};
 std::vector<u32> response;
 const u32* CommandBuffer()const{return words.data();}
};
namespace IPC {
struct ResponseBuilder {
 HLERequestContext& ctx;
 ResponseBuilder(HLERequestContext& x,int):ctx(x){ctx.response.clear();}
 void Push(u32 v){ctx.response.push_back(v);}
};
}
struct SyncpointPool {
 std::array<bool,192> reserved{};
 u32 next{5};
 u32 TryAllocateSyncpoint(bool client){
  assert(!client);
  const u32 ret=next;
  if(ret<reserved.size())reserved[ret]=true;
  return ret;
 }
 bool IsSyncpointAllocated(u32 id)const{return id<reserved.size()&&reserved[id];}
};
struct Container{
 SyncpointPool pool{};
 SyncpointPool& GetSyncpointManager(){return pool;}
};
namespace Nvidia {
struct Module {
 Container c{};
 Container& GetContainer(){return c;}
};
}
struct FakeOwner {
 std::mutex m_native_memory_mutex;
 std::shared_ptr<Nvidia::Module> m_native_module=std::make_shared<Nvidia::Module>();
 bool has_surface{true};
 std::optional<int> LookupVideoSurface(u32)const{
  return has_surface?std::optional<int>{1}:std::nullopt;
 }
};
struct GuestSyncpoints {
 std::array<u32,192> ticks{};
 unsigned readiness_calls{},value_reads{};
 u32 GetGuestSyncpointValue(u32 id){
  assert(id<192);++value_reads;return ticks[id];
 }
 bool IsReadyGuest(u32 id,u32 value) {
  assert(id<192);++readiness_calls;return ticks[id]>=value;
 }
};
struct FakeHost{
 GuestSyncpoints sync{};
 GuestSyncpoints& GetSyncpointManager(){return sync;}
};
struct FakeSystem {
 FakeHost host{};
 FakeHost& Host1x(){return host;}
};
struct IGraphicsVideoDecoderSession {
 static constexpr u32 NoSyncpoint=~u32{0};
 static constexpr u32 MaxSyncpoints=192;
 struct ReleaseFence{u32 id{},value{};};
 struct AvailableImage{u32 id{};std::optional<ReleaseFence> release;};
 struct Output{u32 image{};};
 std::shared_ptr<FakeOwner> m_owner=std::make_shared<FakeOwner>();
 FakeSystem system{};
 std::set<u32> m_registered{7},m_guest_owned{7};
 std::deque<AvailableImage> m_available;
 std::deque<Output> m_outputs;
 u32 m_syncpoint{NoSyncpoint},m_reserved_value{};
 bool m_creator{},m_finalized{};
 unsigned pump_calls{};
 bool EnsureSyncpoint();
 std::optional<std::array<u32,4>> ReserveFence();
 void ReleaseBuffer(HLERequestContext&);
 void Acknowledge(HLERequestContext& ctx,u32 result){
  IPC::ResponseBuilder rb{ctx,2};rb.Push(result);
 }
 void Pump(){++pump_calls;}
};
__REAL_FENCES__
__REAL_RELEASE__
static HLERequestContext Input(u32 encoded_id,u32 value){
 HLERequestContext c;c.words[2]=7;c.words[4]=encoded_id;c.words[6]=value;return c;
}
static void Invalid(u32 encoded_id,u32 value){
 IGraphicsVideoDecoderSession d;
 auto c=Input(encoded_id,value);
 d.ReleaseBuffer(c);
 assert(c.response.size()==1 && c.response[0]==Kernel::ResultInvalidHandle);
 assert(d.m_guest_owned.contains(7)&&d.m_available.empty());
 assert(d.m_reserved_value==0 && d.pump_calls==0);
 assert(d.system.host.sync.value_reads==0);
}
int main(){
 // ~0 => NoSyncpoint. A no-fence release must never query syncpoint UINT_MAX.
 {
  IGraphicsVideoDecoderSession d;
  auto c=Input(0,0);
  d.ReleaseBuffer(c);
  assert(c.response.size()==5 && c.response[0]==ResultSuccess);
  assert(!d.m_guest_owned.contains(7)&&d.m_available.size()==1);
  assert(!d.m_available.front().release);
  assert(d.system.host.sync.readiness_calls==0);
  assert(d.m_syncpoint==5 && d.pump_calls==1);
 }
 Invalid(~u32{0},1);  // release_id == 0
 Invalid(0,1);        // nonzero value with NoSyncpoint sentinel
 Invalid(~u32{192},1);
 Invalid(~u32{15},5); // unallocated foreign id
 {
  IGraphicsVideoDecoderSession d;
  d.m_owner->m_native_module->c.pool.reserved[15]=true;
  auto c=Input(~u32{15},77);
  d.ReleaseBuffer(c);
  assert(c.response.size()==5 && c.response[0]==ResultSuccess);
  assert(d.m_available.size()==1 && d.m_available.front().release);
  assert(d.m_available.front().release->id==15);
  assert(d.m_available.front().release->value==77);
  assert(d.system.host.sync.readiness_calls==0); // external waits in Pump
 }
 {
  IGraphicsVideoDecoderSession d;
  assert(d.EnsureSyncpoint()&&d.m_syncpoint==5);
  auto c=Input(~u32{5},10);d.ReleaseBuffer(c);
  assert(c.response.size()==1 && c.response[0]==Kernel::ResultInvalidHandle);
  assert(d.m_guest_owned.contains(7) && d.m_reserved_value==0);
  d.system.host.sync.ticks[5]=10;c.response.clear();d.ReleaseBuffer(c);
  assert(c.response.size()==5 && c.response[0]==ResultSuccess);
 }
 {
  IGraphicsVideoDecoderSession d;
  d.m_owner->m_native_module.reset();
  auto c=Input(~u32{14},0);d.ReleaseBuffer(c);
  assert(c.response.size()==1 && c.response[0]==Kernel::ResultInvalidHandle);
 }
 {
  IGraphicsVideoDecoderSession d;
  d.m_outputs.push_back({7});
  auto c=Input(0,0);d.ReleaseBuffer(c);
  assert(c.response.size()==1 && c.response[0]==Kernel::ResultBusy);
  assert(d.m_guest_owned.contains(7)&&d.m_reserved_value==0);
 }
 std::puts("H05_REAL_RELEASE_FENCE_OWNER_PASS"
           " none_sentinel=no_lookup zero=refused unallocated=refused"
           " foreign_valid=deferred owner_fence_unready=refused"
           " owner_fence_ready=accepted invalid_module=refused"
           " no_fabricated_image_release=1");
}
""".replace("__REAL_FENCES__",real_fences).replace("__REAL_RELEASE__",real_release)
with tempfile.TemporaryDirectory(prefix="h05-release-fence-") as temp:
 path=Path(temp)/"owner.cpp";path.write_text(cpp)
 for cc in dict.fromkeys(x for x in (shutil.which("clang++"),shutil.which("g++")) if x):
  for opt in ("-O0","-O2"):
   exe=Path(temp)/(Path(cc).name.replace("+","p")+opt)
   cmd=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
        "-pthread","-fsanitize=undefined","-fno-sanitize-recover=all",
        str(path),"-o",str(exe)]
   result=subprocess.run(cmd,capture_output=True,text=True,timeout=60,env=os.environ.copy())
   assert result.returncode==0,(cc,result.stderr[-6000:])
   run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20,env=os.environ.copy())
   assert run.returncode==0,(cc,run.stdout[-1000:],run.stderr[-6000:])
   print(Path(cc).name,opt,run.stdout.strip(),flush=True)
