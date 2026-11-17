#!/usr/bin/env python3
from pathlib import Path
import shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/"encore-active/eden-core/src/video_core/host1x/syncpoint_manager.cpp").read_text()
HDR=(ROOT/"sources/eden/src/video_core/host1x/syncpoint_manager.h").read_text()
assert "using ActionHandle = u64;" in HDR
assert "GuestMirror" in HDR and "WithGuestValuesStable" in HDR
assert "DueActions" in HDR and "action_done_cv" in HDR
assert "std::list<RegisteredAction>::iterator" not in HDR
assert "guest_mirror(*mirrored_id" in SRC
assert "wait_cv.notify_all();" in SRC
assert SRC.index("wait_cv.notify_all();") < SRC.index("due.pending.splice")
compiler=shutil.which("clang++") or shutil.which("c++")
assert compiler
harness=r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <thread>
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
    std::atomic<u32> mirrored{}, observed{};
    m.SetGuestMirror([&](u32 id,u32 value){
      assert(id==3); mirrored=value; observed=m.GetGuestSyncpointValue(id);
    });
    m.IncrementGuest(3);
    assert(mirrored.load()==1);
    assert(observed.load()==0);
    assert(m.GetGuestSyncpointValue(3)==1);
    m.WithGuestValuesStable([&]{ assert(m.GetGuestSyncpointValue(3)==1); });
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
                    "-I",str(t),"-I",str(ROOT/"sources/eden/src"),
                    str(ROOT/"encore-active/eden-core/src/video_core/host1x/syncpoint_manager.cpp"),
                    str(t/"test.cpp"),"-o",str(exe)],check=True,timeout=60)
    subprocess.run([str(exe)],check=True,timeout=15)
print("HOST1X_COUNTER_NOTIFY_PASS numeric_handles=1 running_cancel=1 guest_mirror=1")
