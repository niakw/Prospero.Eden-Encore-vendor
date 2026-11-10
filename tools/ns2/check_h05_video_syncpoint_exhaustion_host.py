#!/usr/bin/env python3
"""H05: run actual decoder EnsureSyncpoint/ReserveFence owner under exhaustion.

The shared SyncpointManager reserves IDs 1..191 and uses zero as its failure
sentinel. A rejected zero ID must not be published as a guest completion
fence, without reserving sentinel zero or invoking the legacy asserting allocator.
Clang/GCC at O0/O2 with UBSan. No real TIPC/Vulkan/PS5 is claimed.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OWNER=ROOT/"encore-active/ns2-runtime/video_decoder/core/hle/service/am/service/graphics_video_decoder_session.cpp"
MANAGER=ROOT/"sources/eden/src/core/hle/service/nvdrv/core/syncpoint_manager.cpp"
HEADER=ROOT/"sources/eden/src/core/hle/service/nvdrv/core/syncpoint_manager.h"
text=OWNER.read_text()
source=MANAGER.read_text()
assert "return 0;" in source.split("SyncpointManager::FindFreeSyncpoint()",1)[1].split(
    "\nu32 SyncpointManager::AllocateSyncpoint(",1)[0]
assert "SyncpointCount{192}" in HEADER.read_text()
start="bool IGraphicsVideoDecoderSession::EnsureSyncpoint() {"
end="\nvoid IGraphicsVideoDecoderSession::RegisterBuffer("
assert text.count(start)==text.count(end)==1
real=start+text.split(start,1)[1].split(end,1)[0]
assert "TryAllocateSyncpoint(false)" in real
assert "SyncpointManager::TryAllocateSyncpoint(bool client_managed)" in source
assert "if (id == 0 || id >= MaxSyncpoints) return false;" in real
assert real.index("if (id == 0 || id >= MaxSyncpoints)") < real.index("m_syncpoint = id;")
assert real.index("m_syncpoint = id;") < real.index("m_reserved_value =")
cpp=r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
using u32=std::uint32_t;
struct Pool {
    u32 next{5};
    unsigned allocated{};
    u32 TryAllocateSyncpoint(bool managed) {
        assert(!managed);++allocated;return next;
    }
};
struct Container {
    Pool pool{};
    Pool& GetSyncpointManager(){return pool;}
};
namespace Nvidia {
struct Module{
    Container c{};
    Container& GetContainer(){return c;}
};
}
struct Owner {
    std::mutex m_native_memory_mutex;
    std::shared_ptr<Nvidia::Module> m_native_module=
        std::make_shared<Nvidia::Module>();
};
struct GuestSyncpoints {
    unsigned reads{};
    u32 guest{12};
    u32 GetGuestSyncpointValue(u32 id){
        assert(id!=0 && id<192);++reads;return guest;
    }
};
struct Host1x {
    GuestSyncpoints points{};
    GuestSyncpoints& GetSyncpointManager(){return points;}
};
struct TestSystem {
    struct Host1x host{};
    struct Host1x& Host1x(){return host;}
};
struct IGraphicsVideoDecoderSession {
    static constexpr u32 NoSyncpoint=~u32{0};
    static constexpr u32 MaxSyncpoints=192;
    std::shared_ptr<Owner> m_owner=std::make_shared<Owner>();
    TestSystem system{};
    u32 m_syncpoint{NoSyncpoint};
    u32 m_reserved_value{};
    bool EnsureSyncpoint();
    std::optional<std::array<u32,4>> ReserveFence();
};
__REAL_OWNER__
static void Deny(u32 returned){
    IGraphicsVideoDecoderSession d;
    d.m_owner->m_native_module->c.pool.next=returned;
    assert(!d.ReserveFence());
    assert(d.m_syncpoint==d.NoSyncpoint && d.m_reserved_value==0);
    assert(d.system.host.points.reads==0);
}
int main(){
    Deny(0);        // Real pool-exhausted sentinel.
    Deny(192);      // Defensive upper bound for foreign/malformed owner.
    Deny(~u32{0});  // Never leak an unallocated MAX_UINT syncpoint.
    IGraphicsVideoDecoderSession unavailable;
    unavailable.m_owner->m_native_module.reset();
    assert(!unavailable.ReserveFence());
    assert(unavailable.m_reserved_value==0);
    IGraphicsVideoDecoderSession accepted;
    accepted.m_owner->m_native_module->c.pool.next=191;
    auto a=accepted.ReserveFence();
    const std::array<u32,4> expected_a{~u32{191},0,13,0};
    assert(a && *a==expected_a);
    auto b=accepted.ReserveFence();
    const std::array<u32,4> expected_b{~u32{191},0,14,0};
    assert(b && *b==expected_b);
    assert(accepted.system.host.points.reads==1);
    assert(accepted.m_owner->m_native_module->c.pool.allocated==1);
    accepted.m_reserved_value=std::numeric_limits<u32>::max();
    assert(!accepted.ReserveFence());
    assert(accepted.m_reserved_value==std::numeric_limits<u32>::max());
    assert(accepted.system.host.points.reads==1);
    std::puts("H05_REAL_SYNCPOINT_OWNER_EXHAUSTION_PASS"
              " pool_zero=refused invalid_high=refused"
              " unavailable_module=refused guest_fence_id=191"
              " values=13,14 overflow=refused"
              " no_false_guest_fence=1");
}
""".replace("__REAL_OWNER__",real)
compilers=list(dict.fromkeys(c for c in
     (shutil.which("clang++"),shutil.which("g++")) if c))
assert compilers
with tempfile.TemporaryDirectory(prefix="eden-h05-syncpoint-owner-") as tmp:
    file=Path(tmp)/"owner.cpp";file.write_text(cpp)
    for cc in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(tmp)/(Path(cc).name.replace("+","p")+opt)
            command=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                     "-fsanitize=undefined","-fno-sanitize-recover=all",
                     "-pthread",str(file),"-o",str(exe)]
            built=subprocess.run(command,capture_output=True,text=True,
                                 env=os.environ.copy(),timeout=45)
            assert built.returncode==0,(cc,built.stderr[-5000:])
            tested=subprocess.run([str(exe)],capture_output=True,text=True,
                                  timeout=15,env=os.environ.copy())
            assert tested.returncode==0,(cc,tested.stderr[-5000:])
            print(Path(cc).name,opt,tested.stdout.strip(),flush=True)
