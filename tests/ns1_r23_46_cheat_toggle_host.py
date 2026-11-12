#!/usr/bin/env python3
"""R23 #46: source-extracted live cheat toggles, master code, race and snapshot under Clang sanitizers."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
r=Path(__file__).resolve().parents[1]
cpp=(r/"encore-active/eden-core/src/core/memory/cheat_engine.cpp").read_text()
hdr=(r/"encore-active/eden-core/include/core/memory/cheat_engine.h").read_text()
source="void CheatEngine::Reload(std::vector<CheatEntry> reload_cheats) {"
end="\n} // namespace Core::Memory"
assert cpp.count(source)==1
assert cpp.count(end)==1
methods=source+cpp.split(source,1)[1].split(end,1)[0]
assert "std::scoped_lock lock{cheats_mutex};" in methods
assert hdr.count("mutable std::mutex cheats_mutex;")==1
assert hdr.count("std::vector<RuntimeCheatInfo> GetCheats() const;")==1
assert hdr.count("bool SetCheatEnabled(u32 cheat_id, bool enabled);")==1
fixture=r"""
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using u32=std::uint32_t;
struct CheatDefinition {
 std::array<char,64> readable_name{};
 u32 num_opcodes{};
 std::array<u32,256> opcodes{};
};
struct CheatEntry {
 bool enabled=false;
 u32 cheat_id{};
 CheatDefinition definition{};
};
struct RuntimeCheatInfo {
 u32 id{};
 std::string name;
 bool enabled{};
 bool is_master{};
};
struct Vm {
 std::vector<CheatEntry> active;
 int executes=0,loads=0;
 bool LoadProgram(const std::vector<CheatEntry>& source) {
  active=source;loads++;return true;
 }
 size_t GetProgramSize() const {
  size_t n=0;
  for (const auto& cheat:active) if(cheat.enabled) n+=cheat.definition.num_opcodes;
  return n;
 }
 void Execute(int) {executes++;}
};
struct CheatEngine {
 mutable std::mutex cheats_mutex;
 std::vector<CheatEntry> cheats;
 std::atomic_bool is_pending_reload{false};
 Vm vm;
 int metadata{};
 void Reload(std::vector<CheatEntry>);
 std::vector<RuntimeCheatInfo> GetCheats() const;
 bool SetCheatEnabled(u32,bool);
 void FrameCallback(std::chrono::nanoseconds);
};
""" + methods + r"""
CheatEntry MakeCheat(u32 id,bool enabled,std::string name) {
 CheatEntry c{};
 c.cheat_id=id;c.enabled=enabled;c.definition.num_opcodes=1;
 for (size_t i=0;i<name.size() && i<63;i++) c.definition.readable_name[i]=name[i];
 return c;
}
int main() {
 using namespace std::chrono_literals;
 CheatEngine e;
 e.Reload({MakeCheat(0,false,"Master"),MakeCheat(1,false,"Regular"),MakeCheat(2,true,"On")});
 auto list=e.GetCheats();
 assert(list.size()==3 && list[0].enabled && list[0].is_master);
 assert(!list[1].enabled && list[2].enabled && list[1].name=="Regular");
 assert(!e.SetCheatEnabled(0,false));
 assert(!e.SetCheatEnabled(9,true));
 assert(e.SetCheatEnabled(1,true));
 e.FrameCallback(0ns);
 assert(e.vm.loads==1 && e.vm.GetProgramSize()==3);
 list=e.GetCheats();
 assert(list[1].enabled);
 assert(e.SetCheatEnabled(2,false));
 e.FrameCallback(0ns);
 assert(e.vm.loads==2 && e.vm.GetProgramSize()==2);
 assert(e.SetCheatEnabled(2,false));
 e.FrameCallback(0ns);
 assert(e.vm.loads==2); // no duplicate reload on no-op
 std::atomic_bool run{true};
 std::thread writer([&] {
  for(int i=0;i<8000;i++) {
   assert(e.SetCheatEnabled(1,i%2==0));
   assert(e.SetCheatEnabled(2,i%2!=0));
  }
  run.store(false);
 });
 while(run.load()) {auto snapshot=e.GetCheats();assert(snapshot.size()==3);e.FrameCallback(0ns);}
 writer.join();
 e.FrameCallback(0ns);
 auto final=e.GetCheats();
 assert(final[0].enabled);
 assert(e.vm.active[0].enabled && e.vm.active[1].enabled==final[1].enabled);
 assert(e.vm.active[2].enabled==final[2].enabled);
 e.Reload({MakeCheat(0,false,"Master"),MakeCheat(3,true,"New")});
 e.FrameCallback(0ns);
 assert(e.GetCheats().size()==2 && e.GetCheats()[0].enabled);
}
""";
with tempfile.TemporaryDirectory(prefix="ns1-cheat-toggle-") as td:
 p=Path(td)
 (p/"test.cpp").write_text(fixture)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g","-pthread",
  "-fsanitize=address,undefined","-fno-omit-frame-pointer",str(p/"test.cpp"),"-o",str(p/"test")],
  check=True,timeout=90)
 subprocess.run([str(p/"test")],check=True,timeout=50,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_46_RUNTIME_CHEAT_TOGGLE_HOST_PASS master=1 reload=1 snapshot=1 concurrent_toggles=16000")
print("R23_46_PS5_FRONTEND_UI_AND_NATIVE_QUALIFICATION_OPEN")
