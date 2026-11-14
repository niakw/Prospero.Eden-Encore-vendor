#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact selected Host1x source/ABI, old-versus-new waiter notification test."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"encore-active/eden-core/src/video_core/host1x/syncpoint_manager.cpp"
HEADER=ROOT/"sources/eden/src/video_core/host1x/syncpoint_manager.h"
def blob(s):
    raw=s.encode()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
current=SOURCE.read_text()
old="    // Preserve the original contract: guest/host waiters are notified\n    // only after all due callbacks have completed, never before dispatch.\n    wait_cv.notify_all();\n"
moved="    // A syncpoint waiter observes the published counter, not callback completion.\n    // Wake it before dispatch: a display callback may block independently.\n    wait_cv.notify_all();\n"
anchor="    // Cancellation can prevent an action only before the claim above.\n"
end="        ready.action();\n    }\n"
assert current.count(moved)==1 and current.count(end)==1
assert current.index("ready_actions.splice(")<current.index(moved)<current.index(end)
previous=current.replace(moved,"",1).replace(end,end+old,1)
assert blob(previous)=="aed892831a31b2ca6691bf4f697bcc035ddaffa3"
assert blob(HEADER.read_text())=="847ed20c8dccbb0319d95ccbded460c78f661cac"
# Test-only hook executes under guard immediately before the condition waiter
# sleeps. It changes neither the predicate nor the notification/dispatch.
hook="    wait_cv.wait(lk, pred);"
harness=r"""
#include <cassert>
#include <chrono>
#include <thread>
#include <future>
#include <cstdio>
#include "video_core/host1x/syncpoint_manager.h"
using Tegra::Host1x::SyncpointManager;
using namespace std::chrono_literals;
std::atomic<bool> armed{};
void witness_wait_armed(){armed.store(true,std::memory_order_release);}
int main(int argc,char** argv){
    assert(argc==2);
    const bool early_expected=argv[1][0]=='1';
    for(bool guest:{false,true}){
       SyncpointManager m;
       std::promise<void> entered,release,early;
       auto entered_future=entered.get_future(),release_future=release.get_future(),early_future=early.get_future();
       auto callback=[&]{entered.set_value();release_future.wait();};
       if(guest)m.RegisterGuestAction(5,1,callback);else m.RegisterHostAction(5,1,callback);
       armed=false;
       auto wait=[&]{if(guest)m.WaitGuest(5,1);else m.WaitHost(5,1);};
       auto inc=[&]{if(guest)m.IncrementGuest(5);else m.IncrementHost(5);};
       std::thread waiter([&]{wait();early.set_value();});
       auto deadline=std::chrono::steady_clock::now()+3s;
       while(!armed.load(std::memory_order_acquire)){
           assert(std::chrono::steady_clock::now()<deadline);
           std::this_thread::yield();
       }
       std::thread dispatcher(inc);
       assert(entered_future.wait_for(3s)==std::future_status::ready);
       auto late=std::async(std::launch::async,wait);
       assert(late.wait_for(2s)==std::future_status::ready);
       bool early_ready=early_future.wait_for(early_expected?2s:200ms)==std::future_status::ready;
       release.set_value();
       dispatcher.join();waiter.join();late.get();
       assert(early_ready==early_expected);
       std::printf("%s early=%d late=1 blocked_callback=1\n",guest?"guest":"host",early_ready);
    }
}
"""
compiler=shutil.which("clang++") or shutil.which("c++")
assert compiler
with tempfile.TemporaryDirectory(prefix="host1x-notification-") as d:
    tmp=Path(d)
    (tmp/"common").mkdir()
    (tmp/"common/common_types.h").write_text("#pragma once\n#include <cstdint>\nusing u32=std::uint32_t;\n")
    (tmp/"test.cpp").write_text(harness)
    for label,src,good in (("historic",previous,"0"),("candidate",current,"1")):
        assert src.count(hook)==1
        (tmp/"owner.cpp").write_text("void witness_wait_armed();\n"+src.replace(hook,"    ::witness_wait_armed();\n"+hook))
        for opt in ("-O0","-O2"):
            exe=tmp/"run"
            subprocess.run([compiler,"-std=c++20",opt,"-Wall","-Wextra","-Werror","-pthread",
                            "-fsanitize=undefined","-fno-sanitize-recover=all","-I",str(tmp),
                            "-I",str(ROOT/"sources/eden/src"),str(tmp/"owner.cpp"),str(tmp/"test.cpp"),
                            "-o",str(exe)],check=True,timeout=60)
            subprocess.run([str(exe),good],check=True,timeout=15)
            print("HOST1X_COUNTER_NOTIFY_WITNESS",label,opt)
print("HOST1X_COUNTER_NOTIFY_PASS old_and_new=1 host_guest=2 full_owner=1 PS5_unverified")
