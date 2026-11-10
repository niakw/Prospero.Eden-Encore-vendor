#!/usr/bin/env python3
"""G10 QMD3: run the maintained pre-dispatch semaphore backing predicate.

Execute NativeQmdReleaseTargetsMapped and the exact callback supplied by
Puller::ProcessNativeComputeQmdAt, Clang/GCC, O0/O2, UBSan. Validate that
resident GMMU without host-backed writable pointer is rejected BEFORE
dispatch, for all three release slots. No vkCmdDispatch or console claim.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
PULLER=ROOT/"encore-active/ns2-runtime/gpu_dispatch/video_core/engines/puller.cpp"
source=PULLER.read_text()
method=source.split("void Puller::ProcessNativeComputeQmdAt(",1)[1].split(
    "\n/// Calls a GPU engine method.",1)[0]
begin="const bool release_targets_mapped ="
end="                        });"
assert method.count(begin)==method.count(end)==1
owner=begin+method.split(begin,1)[1].split(end,1)[0]+end
assert "GetPointer(address) != nullptr" in owner
assert owner.index("IsFullyMappedRange(address, bytes)") < owner.index(
    "GetPointer(address) != nullptr")
assert owner.index("GetPointer(address) != nullptr") < owner.index("});")
assert method.index(begin) < method.index("DispatchNativeCompute(*launch)")
release=method.split("auto release = [&](u64 addr, u32 payload, u32 flags) {",1)[1].split(
    "\n    std::string_view error;",1)[0]
assert "!pusher.memory_manager.GetPointer(addr)" in release
assert release.index("!pusher.memory_manager.GetPointer(addr)") < release.index(
    "pusher.rasterizer->SignalFence(")
assert "!memory->GetPointer(addr)" in release

fixture=r"""
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include "video_core/native_qmd.h"
using u8=std::uint8_t;
using u32=std::uint32_t;
using u64=std::uint64_t;
struct TestMemoryManager {
    std::array<bool,3> mapped{true,true,true};
    std::array<bool,3> cpu_backed{true,true,true};
    std::array<std::array<u8,4>,3> words{};
    unsigned mapping_reads{},pointer_reads{};
    static std::optional<std::size_t> Slot(u64 address) {
        if(address<0x1000 || address>0x3000 || (address&0xfff))
            return std::nullopt;
        return (address>>12)-1;
    }
    bool IsFullyMappedRange(u64 address,std::size_t bytes) {
        ++mapping_reads;
        auto idx=Slot(address);
        return idx && bytes==4 && mapped[*idx];
    }
    u8* GetPointer(u64 address) {
        ++pointer_reads;
        const auto idx=Slot(address);
        return idx && mapped[*idx] && cpu_backed[*idx]
            ?words[*idx].data():nullptr;
    }
};
struct DmaPusher {
    TestMemoryManager memory_manager;
};
[[nodiscard]] static bool Checked(const Tegra::NativeQmdScheduler::Descriptor& descriptor,
                                  DmaPusher& pusher) {
    __REAL_OWNER__
    return release_targets_mapped;
}
static void Slot(Tegra::NativeQmdScheduler::Descriptor& qmd,
                 std::size_t base, u32 address){
    qmd[base]=address;
    qmd[base+1]=(1U<<23)|(1U<<30);
}
int main(){
    Tegra::NativeQmdScheduler::Descriptor qmd{};
    DmaPusher pusher;
    auto& mem=pusher.memory_manager;
    assert(Checked(qmd,pusher));
    assert(mem.mapping_reads==0 && mem.pointer_reads==0);
    Slot(qmd,24,0x1000);
    assert(Checked(qmd,pusher));
    assert(mem.mapping_reads==1 && mem.pointer_reads==1);
    mem.cpu_backed[0]=false;
    assert(!Checked(qmd,pusher));
    assert(mem.mapping_reads==2 && mem.pointer_reads==2);
    mem.cpu_backed[0]=true;
    Slot(qmd,28,0x2000);
    Slot(qmd,52,0x3000);
    assert(Checked(qmd,pusher));
    mem.cpu_backed[1]=false;
    assert(!Checked(qmd,pusher)); // second target cannot strand completion
    mem.cpu_backed[1]=true;
    mem.cpu_backed[2]=false;
    assert(!Checked(qmd,pusher)); // third target cannot strand completion
    mem.cpu_backed[2]=true;
    assert(Checked(qmd,pusher));
    mem.mapped[2]=false;
    const unsigned before=mem.pointer_reads;
    assert(!Checked(qmd,pusher));
    assert(mem.pointer_reads==before); // GMMU refusal precedes CPU access
    mem.mapped[2]=true;
    qmd[52]=0;
    assert(!Checked(qmd,pusher));
    qmd[52]=0x3000;
    assert(Checked(qmd,pusher));
    std::puts("G10_QMD3_REAL_RELEASE_OWNER_PRE_DISPATCH_PASS"
              " valid_mapped=1 CPU_backing_missing=refused"
              " first_second_third_target=checked"
              " unmapped=refused zero_va=refused"
              " Vulkan_QMD_dispatch=not_exercised");
}
""".replace("__REAL_OWNER__",owner)
compilers=list(dict.fromkeys(x for x in
    (shutil.which("clang++"),shutil.which("g++")) if x))
assert compilers
with tempfile.TemporaryDirectory(prefix="eden-g10-qmd-preflight-") as tmp:
    src=Path(tmp)/"owner.cpp"
    src.write_text(fixture)
    for cc in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(tmp)/(Path(cc).name.replace("+","p")+opt)
            cmd=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                 "-fsanitize=undefined","-fno-sanitize-recover=all",
                 "-I"+str(ROOT/"encore-active/ns2-runtime/gpu_dispatch"),
                 str(src),"-o",str(exe)]
            built=subprocess.run(cmd,capture_output=True,text=True,timeout=60,
                                 env=os.environ.copy())
            assert built.returncode==0,(cc,built.stderr[-6000:])
            output=subprocess.run([str(exe)],capture_output=True,text=True,
                                  timeout=20,env=os.environ.copy())
            assert output.returncode==0,(cc,output.stderr[-6000:])
            print(Path(cc).name,opt,output.stdout.strip(),flush=True)
