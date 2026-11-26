#!/usr/bin/env python3
from pathlib import Path
import shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/"encore-active/eden-core/src/video_core/host1x/syncpoint_manager.cpp").read_text()
HDR=(ROOT/"encore-active/eden-core/overrides/video_core/host1x/syncpoint_manager.h").read_text()
assert "using ActionHandle = u64;" in HDR
assert "GuestMirror" in HDR and "WithGuestValuesStable" in HDR
assert "DueActions" in HDR and "action_done_cv" in HDR
assert "std::list<RegisteredAction>::iterator" not in HDR
assert "guest_mirror(*mirrored_id" in SRC
guest_inc=SRC.split("void SyncpointManager::IncrementGuest(u32 id) {",1)[1].split("\n}",1)[0]
host_inc=SRC.split("void SyncpointManager::IncrementHost(u32 id) {",1)[1].split("\n}",1)[0]
assert "guest_action_storage[id]);" in guest_inc
assert "guest_action_storage[id], id)" not in guest_inc
assert "host_action_storage[id], id)" in host_inc
assert "wait_cv.notify_all();" in SRC
assert "std::erase(due_actions, &due);" in SRC
assert SRC.count("action_done_cv.notify_all();") >= 2
assert SRC.index("wait_cv.notify_all();") < SRC.index("due.pending.splice")
compiler=shutil.which("clang++") or shutil.which("c++")
assert compiler
harness=r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <thread>
#include <stdexcept>
#include "video_core/host1x/syncpoint_manager.h"
using Tegra::Host1x::SyncpointManager;
using namespace std::chrono_literals;
int main(){
  {
    SyncpointManager m;
    std::promise<void> entered, release, waited;
    auto entered_f=entered.get_future(), waited_f=waited.get_future();
    m.RegisterGuestAction(5,1,[&]{entered.set_value(); release.get_future().wait();});
    std::thread w([&]{m.WaitGuest(5,1); waited.set_value();});
    std::this_thread::sleep_for(20ms);
    std::thread inc([&]{m.IncrementGuest(5);});
    assert(entered_f.wait_for(2s)==std::future_status::ready);
    assert(waited_f.wait_for(1s)==std::future_status::ready);
    release.set_value(); inc.join(); w.join();
  }
  {
    SyncpointManager m;
    std::atomic<int> calls{};
    auto h=m.RegisterGuestAction(7,2,[&]{++calls;});
    assert(h!=0);
    m.DeregisterGuestAction(7,h);
    m.IncrementGuest(7); m.IncrementGuest(7);
    assert(calls.load()==0);
  }
  {
    SyncpointManager m;
    std::promise<void> entered, release;
    auto entered_f=entered.get_future();
    auto h=m.RegisterGuestAction(9,1,[&]{entered.set_value(); release.get_future().wait();});
    std::thread inc([&]{m.IncrementGuest(9);});
    assert(entered_f.wait_for(2s)==std::future_status::ready);
    auto cancel=std::async(std::launch::async,[&]{m.DeregisterGuestAction(9,h);});
    assert(cancel.wait_for(100ms)==std::future_status::timeout);
    release.set_value(); inc.join();
    assert(cancel.wait_for(2s)==std::future_status::ready); cancel.get();
  }
  {
    SyncpointManager m;
    std::promise<void> entered, release;
    auto entered_f=entered.get_future();
    std::atomic<bool> caught{};
    auto h=m.RegisterGuestAction(11,1,[&]{
      entered.set_value();
      release.get_future().wait();
      throw std::runtime_error("injected syncpoint callback failure");
    });
    auto inc=std::async(std::launch::async,[&]{
      try { m.IncrementGuest(11); }
      catch (const std::runtime_error&) { caught=true; }
    });
    assert(entered_f.wait_for(2s)==std::future_status::ready);
    auto cancel=std::async(std::launch::async,[&]{m.DeregisterGuestAction(11,h);});
    assert(cancel.wait_for(100ms)==std::future_status::timeout);
    release.set_value();
    assert(inc.wait_for(2s)==std::future_status::ready); inc.get();
    assert(cancel.wait_for(2s)==std::future_status::ready); cancel.get();
    assert(caught.load());
  }
  {
    SyncpointManager m;
    std::atomic<u32> mirrored{}, observed{};
    m.SetGuestMirror([&](u32 id,u32 value){
      assert(id==3); mirrored=value; observed=m.GetGuestSyncpointValue(id);
    });
    m.IncrementGuest(3);
    // Native/GSV completion must stay behind the renderer/host retirement.
    assert(mirrored.load()==0);
    assert(m.GetGuestSyncpointValue(3)==1);
    assert(m.GetHostSyncpointValue(3)==0);
    assert(m.GetNativeGuestSyncpointValue(3)==0);
    m.IncrementHost(3);
    assert(mirrored.load()==1);
    assert(observed.load()==1);
    assert(m.GetHostSyncpointValue(3)==1);
    m.WithGuestValuesStable([&]{ assert(m.GetGuestSyncpointValue(3)==1); assert(m.GetNativeGuestSyncpointValue(3)==1); });
  }
  {
    SyncpointManager m;
    // Modular comparison recognizes a previously reached wrapped threshold.
    static_assert(SyncpointManager::IsNativeGuestActionReady(1,0xffffffffu,false));
    static_assert(!SyncpointManager::IsNativeGuestActionReady(0xfffffffeu,1,false));
    static_assert(!SyncpointManager::IsNativeGuestActionReady(1,0x100000001ULL,true));
    static_assert(SyncpointManager::IsNativeGuestActionReady(0x100000001ULL,0x100000001ULL,true));
    std::atomic<unsigned> modular{}, wide{}, legacy{}, cancelled{};
    assert(m.RegisterNativeGuestAction(4,0xffffffffu,false,[&]{++modular;})==0);
    assert(modular==1);
    const auto high=m.RegisterNativeGuestAction(4,0x100000001ULL,true,[&]{++wide;});
    assert(high!=0);
    const auto cancel=m.RegisterNativeGuestAction(4,2,false,[&]{++cancelled;});
    m.DeregisterNativeGuestAction(4,cancel);
    m.RegisterNativeGuestAction(4,2,true,[&]{++wide;});
    m.RegisterGuestAction(4,2,[&]{++legacy;});
    m.IncrementGuest(4);
    assert(wide==0 && legacy==0);
    m.IncrementGuest(4);
    assert(wide==0 && legacy==1 && cancelled==0);
    assert(m.GetNativeGuestSyncpointValue(4)==0);
    m.IncrementHost(4);
    assert(wide==0);
    m.IncrementHost(4);
    assert(wide==1 && cancelled==0);
    m.DeregisterNativeGuestAction(4,high);
    assert(m.GetNativeGuestSyncpointValue(4)==2);
  }
  {
    SyncpointManager m;
    std::promise<void> entered, release;
    auto entered_f=entered.get_future();
    auto h=m.RegisterNativeGuestAction(6,1,true,[&]{entered.set_value(); release.get_future().wait();});
    std::thread inc([&]{m.IncrementHost(6);});
    assert(entered_f.wait_for(2s)==std::future_status::ready);
    auto cancel=std::async(std::launch::async,[&]{m.DeregisterNativeGuestAction(6,h);});
    assert(cancel.wait_for(100ms)==std::future_status::timeout);
    release.set_value(); inc.join(); cancel.get();
  }
}
'''
with tempfile.TemporaryDirectory(prefix="host1x-action-") as d:
    t=Path(d); (t/"common").mkdir(parents=True)
    (t/"common/common_types.h").write_text("#pragma once\n#include <cstdint>\nusing u32=std::uint32_t; using u64=std::uint64_t;\n")
    (t/"common/logging.h").write_text("#pragma once\n#define LOG_ERROR(...) ((void)0)\n")
    (t/"test.cpp").write_text(harness)
    exe=t/"run"
    subprocess.run([compiler,"-std=c++20","-O2","-Wall","-Wextra","-Werror","-pthread",
                    "-fsanitize=undefined","-fno-sanitize-recover=all",
                    "-I",str(t),
                    "-I",str(ROOT/"encore-active/eden-core/overrides"),
                    "-I",str(ROOT/"sources/eden/src"),
                    str(ROOT/"encore-active/eden-core/src/video_core/host1x/syncpoint_manager.cpp"),
                    str(t/"test.cpp"),"-o",str(exe)],check=True,timeout=60)
    subprocess.run([str(exe)],check=True,timeout=15)
print("HOST1X_COUNTER_NOTIFY_PASS numeric_handles=1 running_cancel=1 throwing_action_cleanup=1 native_retire_mirror=1")
