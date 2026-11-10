#!/usr/bin/env python3
"""G10: execute actual QMD3 deferred guest semaphore write and fatal rollback.

Compile the source-extracted Puller::ProcessNativeComputeQmdAt release lambda.
A retired GMMU/CPU backing after an accepted GPU dispatch MUST record a GPU
worker failure, never silently consume a valid queue completion. A revoked
channel epoch is separately ignored, and a valid release writes only after
the simulated fence callback. This test is host-only, no vkCmdDispatch.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/"encore-active/ns2-runtime/gpu_dispatch/video_core/engines/puller.cpp"
text=SOURCE.read_text()
method=text.split("void Puller::ProcessNativeComputeQmdAt(",1)[1].split(
    "\n/// Calls a GPU engine method.",1)[0]
begin="auto release = [&](u64 addr, u32 payload, u32 flags) {"
end="    std::string_view error;"
assert method.count(begin)==method.count(end)==1
owner=begin+method.split(begin,1)[1].split(end,1)[0]
assert "SignalFence(" in owner
assert "RecordGpuFailure(std::current_exception())" in owner
assert "Native QMD3 release backing revoked after GPU submission" in owner
assert owner.index("if (!current.IsCurrent(channel, ticket)) return;") < owner.index(
    "throw std::runtime_error(")
assert owner.index("throw std::runtime_error(") < owner.index("memory->Write<u32>(addr, value);")
cpp=r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <algorithm>
using u8=std::uint8_t;
using u32=std::uint32_t;
using u64=std::uint64_t;
namespace Eden {
static unsigned failures{};
static std::exception_ptr last;
void RecordGpuFailure(std::exception_ptr e) {
    ++failures;last=std::move(e);
}
}
struct Channel {};
namespace Service::Nvidia::Devices {
class NativeGpuChannelRegistry {
public:
    struct Ticket{unsigned serial{7};};
    bool current{true};
    static NativeGpuChannelRegistry& Instance(){
        static NativeGpuChannelRegistry r;
        return r;
    }
    bool IsCurrent(const Channel*,const Ticket& ticket) const {
        return current&&ticket.serial==7;
    }
};
}
struct MemoryManager {
    bool mapped{true},cpu_backed{true};
    u32 word{3};
    unsigned reads{},writes{};
    bool IsFullyMappedRange(u64 address,std::size_t size) const {
        return mapped&&address==0x1000&&size==4;
    }
    u8* GetPointer(u64 address){
        return cpu_backed&&IsFullyMappedRange(address,4)
            ?reinterpret_cast<u8*>(&word):nullptr;
    }
    template<class T> T Read(u64 address){
        assert(IsFullyMappedRange(address,sizeof(T))&&cpu_backed);
        ++reads;return static_cast<T>(word);
    }
    template<class T> void Write(u64 address,T value){
        assert(IsFullyMappedRange(address,sizeof(T))&&cpu_backed);
        ++writes;word=value;
    }
};
struct Rasterizer {
    std::vector<std::function<void()>> callbacks;
    void SignalFence(std::function<void()> fn){
        callbacks.push_back(std::move(fn));
    }
    void RetireOne(){
        assert(!callbacks.empty());
        auto fn=std::move(callbacks.front());
        callbacks.erase(callbacks.begin());
        fn();
    }
};
struct DmaPusher {
    Channel channel_state{};
    MemoryManager memory_manager{};
    Rasterizer* rasterizer{};
};
int main() {
    using Registry=Service::Nvidia::Devices::NativeGpuChannelRegistry;
    auto ticket=std::optional<Registry::Ticket>{Registry::Ticket{}};
    DmaPusher pusher;
    Rasterizer renderer;
    pusher.rasterizer=&renderer;
    __EXTRACTED_OWNER__
    assert(release(0x1000,19,0));
    assert(renderer.callbacks.size()==1 && pusher.memory_manager.word==3);
    renderer.RetireOne();
    assert(renderer.callbacks.empty() && pusher.memory_manager.word==19);
    assert(Eden::failures==0 && pusher.memory_manager.writes==1);
    assert(release(0x1000,2,1U<<26)); // atomic add after GPU retire
    renderer.RetireOne();
    assert(pusher.memory_manager.word==21);
    assert(Eden::failures==0 && pusher.memory_manager.writes==2);
    assert(release(0x1000,30,1U<<19)); // max
    renderer.RetireOne();
    assert(pusher.memory_manager.word==30 && Eden::failures==0);
    const unsigned previously_queued=static_cast<unsigned>(renderer.callbacks.size());
    pusher.memory_manager.cpu_backed=false;
    assert(!release(0x1000,40,0));
    assert(renderer.callbacks.size()==previously_queued);
    pusher.memory_manager.cpu_backed=true;

    // A release was VALID when queued, then backing disappeared while
    // its actual GPU work was still in flight: never silently complete it.
    assert(release(0x1000,31,0));
    pusher.memory_manager.mapped=false;
    renderer.RetireOne();
    assert(Eden::failures==1 && pusher.memory_manager.word==30 &&
           pusher.memory_manager.writes==3);
    try {std::rethrow_exception(Eden::last);}
    catch(const std::runtime_error& e){
        assert(std::string_view(e.what())==
            "Native QMD3 release backing revoked after GPU submission");
    }
    pusher.memory_manager.mapped=true;
    assert(release(0x1000,32,0));
    pusher.memory_manager.cpu_backed=false;
    renderer.RetireOne();
    assert(Eden::failures==2 && pusher.memory_manager.word==30);
    pusher.memory_manager.cpu_backed=true;

    // Title/channel teardown revokes epoch ownership. Old completion is
    // deliberately suppressed rather than written to a new guest process.
    assert(release(0x1000,44,0));
    Registry::Instance().current=false;
    renderer.RetireOne();
    assert(Eden::failures==2 && pusher.memory_manager.word==30);
    Registry::Instance().current=true;
    assert(!release(0x1001,47,0));
    assert(!release(0,47,0));
    std::puts("G10_QMD3_REAL_OWNER_RETIRE_CALLBACK_PASS"
              " signal_after_fence=1 reduction_add=1 reduction_max=1"
              " unbacked_preflight=refused"
              " unmapped_late=GPU_failure_latched"
              " cpu_backing_lost_late=GPU_failure_latched"
              " revoked_epoch=no_write"
              " native_vkCmdDispatch=not_exercised");
}
""".replace("__EXTRACTED_OWNER__",owner)
compilers=list(dict.fromkeys(x for x in
    (shutil.which("clang++"),shutil.which("g++")) if x))
assert compilers
with tempfile.TemporaryDirectory(prefix="eden-g10-qmd-retire-") as tmp:
    file=Path(tmp)/"owner.cpp"
    file.write_text(cpp)
    for cc in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(tmp)/(Path(cc).name.replace("+","p")+opt)
            args=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                  "-DPS5_NATIVE","-fsanitize=undefined","-fno-sanitize-recover=all",
                  str(file),"-o",str(exe)]
            compiled=subprocess.run(args,capture_output=True,text=True,
                                    timeout=60,env=os.environ.copy())
            assert compiled.returncode==0,(cc,compiled.stderr[-8000:])
            ran=subprocess.run([str(exe)],capture_output=True,text=True,
                               timeout=20,env=os.environ.copy())
            assert ran.returncode==0,(cc,ran.stderr[-6000:])
            print(Path(cc).name,opt,ran.stdout.strip(),flush=True)
