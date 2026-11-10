#!/usr/bin/env python3
"""H05: execute the maintained NVDRV pool's safe atomic decoder reservation.

This NS2 opt-in method must never publish ID 0 or throw an assert under
exhaustion, even with simultaneous host clients. The existing AllocateSyncpoint
used by NS1 stays intact. Four threads reserve 191 IDs exactly once.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OWNER=ROOT/"encore-active/eden-sync/src/core/hle/service/nvdrv/core/syncpoint_manager.cpp"
HEADER=ROOT/"encore-active/eden-sync/overrides/h05-syncpoint/core/hle/service/nvdrv/core/syncpoint_manager.h"
source=OWNER.read_text()
start="u32 SyncpointManager::TryAllocateSyncpoint(bool client_managed) {"
end="\nvoid SyncpointManager::FreeSyncpoint("
assert source.count(start)==source.count(end)==1
real=start+source.split(start,1)[1].split(end,1)[0]
assert 'for (u32 id{1}; id < syncpoints.size(); ++id)' in real
assert real.index("std::lock_guard lock(reservation_lock)") < real.index("for (u32 id{1}")
assert 'return ReserveSyncpoint(id, client_managed)' in real
assert 'return 0;' in real
# Fail closed if upstream pins are ever mutated instead of using the overlay.
for rel in ("syncpoint_manager.h","syncpoint_manager.cpp"):
    assert "TryAllocateSyncpoint" not in (
        ROOT/"sources/eden/src/core/hle/service/nvdrv/core"/rel).read_text()
cmake=(ROOT/"encore-active/eden-threading/native-hle-overrides.cmake").read_text()
assert "_encore_h05_header_root" in cmake and "_encore_h05_source" in cmake
assert "if(PS5_NATIVE)" in cmake
assert 'TryAllocateSyncpoint(bool client_managed)' in HEADER.read_text()
legacy=source.split("u32 SyncpointManager::AllocateSyncpoint(",1)[1].split(
    "\nu32 SyncpointManager::TryAllocateSyncpoint(",1)[0]
assert "return ReserveSyncpoint(FindFreeSyncpoint(), client_managed);" in legacy

fixture=r"""
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
using u32=std::uint32_t;
namespace Service::Nvidia::NvCore {
struct SyncpointManager {
    struct Info{bool reserved{},client_managed{};};
    std::array<Info,192> syncpoints{};
    std::mutex reservation_lock;
    u32 ReserveSyncpoint(u32 id,bool client_managed){
        assert(id>0 && id<syncpoints.size() && !syncpoints[id].reserved);
        syncpoints[id].reserved=true;
        syncpoints[id].client_managed=client_managed;
        return id;
    }
    u32 TryAllocateSyncpoint(bool client_managed);
};
__REAL_OWNER__
}
int main(){
    using Service::Nvidia::NvCore::SyncpointManager;
    SyncpointManager pool;
    std::array<std::atomic<unsigned>,192> obtained{};
    auto reserve=[&]{
        while(auto id=pool.TryAllocateSyncpoint(false)){
            assert(id>0 && id<obtained.size());
            assert(obtained[id].fetch_add(1)==0);
        }
    };
    std::thread a(reserve),b(reserve),c(reserve),d(reserve);
    a.join();b.join();c.join();d.join();
    unsigned total{};
    for(u32 id=1;id<192;++id){
        assert(pool.syncpoints[id].reserved && obtained[id]==1);
        ++total;
    }
    assert(total==191);
    assert(!pool.syncpoints[0].reserved && obtained[0]==0);
    assert(pool.TryAllocateSyncpoint(true)==0);
    assert(!pool.syncpoints[0].reserved);
    pool.syncpoints[77].reserved=false;
    assert(pool.TryAllocateSyncpoint(true)==77);
    assert(pool.syncpoints[77].client_managed);
    assert(pool.TryAllocateSyncpoint(true)==0);
    std::puts("H05_REAL_SYNCPOINT_POOL_EXHAUSTION_PASS"
              " concurrent_unique=191 sentinel_zero=never_reserved"
              " exhausted=0 recycled=77 legacy_allocator=unchanged"
              " no_guest_fence_without_allocation=1");
}
""".replace("__REAL_OWNER__",real)
compilers=list(dict.fromkeys(x for x in
    (shutil.which("clang++"),shutil.which("g++")) if x))
assert compilers
with tempfile.TemporaryDirectory(prefix="eden-h05-atomic-syncpoints-") as tmp:
    sourcefile=Path(tmp)/"pool.cpp"
    sourcefile.write_text(fixture)
    for cc in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(tmp)/(Path(cc).name.replace("+","p")+opt)
            cmd=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                 "-fsanitize=undefined","-fno-sanitize-recover=all",
                 "-pthread",str(sourcefile),"-o",str(exe)]
            built=subprocess.run(cmd,capture_output=True,text=True,
                                 timeout=60,env=os.environ.copy())
            assert built.returncode==0,(cc,built.stderr[-5000:])
            ran=subprocess.run([str(exe)],capture_output=True,text=True,
                               timeout=20,env=os.environ.copy())
            assert ran.returncode==0,(cc,ran.stderr[-5000:])
            print(Path(cc).name,opt,ran.stdout.strip(),flush=True)
