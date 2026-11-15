#!/usr/bin/env python3
"""Execute the selected Maxwell3D::ProcessCBBind body with valid and OOB slots."""
from pathlib import Path
import os, shutil, subprocess, tempfile

root=Path(__file__).resolve().parents[1]
src=(root/"encore-active/eden-core/src/video_core/engines/maxwell_3d.cpp").read_text()
marker="void Maxwell3D::ProcessCBBind(size_t stage_index) {"
start=src.index(marker)
brace=src.index("{",start)
depth=0
end=None
for i in range(brace,len(src)):
    if src[i]=="{": depth+=1
    elif src[i]=="}":
        depth-=1
        if depth==0:
            end=i+1
            break
assert end
method=src[start:end]
assert "Lichii e6e03043" in method
guard="if (bind_data.shader_slot >= Regs::MaxConstBuffers)"
index="state.shader_stages[stage_index].const_buffers[bind_data.shader_slot]"
assert method.count(guard)==1 and method.count(index)==1
assert method.index(guard) < method.index(index)
body=method[method.index("{"):]

cpp=r"""
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
using u32=std::uint32_t;
using u64=std::uint64_t;
using GPUVAddr=u64;
struct Field {
    u32 v{};
    u32 Value() const { return v; }
    operator std::size_t() const { return v; }
};
struct Maxwell3D {
    struct Regs {
        static constexpr std::size_t MaxConstBuffers=18;
    };
    struct Bind { Field valid{}, shader_slot{}; };
    struct ConstBufferRegs {
        GPUVAddr address{};
        u32 size{};
        GPUVAddr Address() const { return address; }
    };
    struct Registers {
        std::array<Bind,5> bind_groups{};
        ConstBufferRegs const_buffer{};
    } regs;
    struct Info { bool enabled{}; GPUVAddr address{}; u32 size{}; };
    struct Stage { std::array<Info,Regs::MaxConstBuffers> const_buffers{}; };
    struct State { std::array<Stage,5> shader_stages{}; } state;
    struct Rasterizer {
        unsigned binds{}, disables{};
        std::size_t last_stage{}, last_slot{};
        GPUVAddr last_addr{}; u32 last_size{};
        void DisableGraphicsUniformBuffer(std::size_t stage,std::size_t slot){
            ++disables;last_stage=stage;last_slot=slot;
        }
        void BindGraphicsUniformBuffer(std::size_t stage,std::size_t slot,GPUVAddr addr,u32 size){
            ++binds;last_stage=stage;last_slot=slot;last_addr=addr;last_size=size;
        }
    } rasterizer_storage;
    Rasterizer* rasterizer=&rasterizer_storage;
    void ProcessCBBind(std::size_t stage_index) __BODY__
};
int main(){
    Maxwell3D m;
    m.regs.const_buffer.address=0x123400;
    m.regs.const_buffer.size=0x80;

    // Highest legal slot updates state and binds.
    m.regs.bind_groups[2].valid.v=1;
    m.regs.bind_groups[2].shader_slot.v=17;
    m.ProcessCBBind(2);
    assert(m.rasterizer_storage.binds==1 && m.rasterizer_storage.disables==0);
    assert(m.state.shader_stages[2].const_buffers[17].enabled);
    assert(m.state.shader_stages[2].const_buffers[17].address==0x123400);
    assert(m.rasterizer_storage.last_slot==17);

    // First illegal slot and max 5-bit slot must be no-ops before indexing.
    for(unsigned bad: {18u,31u}) {
        auto before=m.state;
        const auto binds=m.rasterizer_storage.binds;
        const auto disables=m.rasterizer_storage.disables;
        m.regs.bind_groups[2].valid.v=1;
        m.regs.bind_groups[2].shader_slot.v=bad;
        m.ProcessCBBind(2);
        assert(m.rasterizer_storage.binds==binds);
        assert(m.rasterizer_storage.disables==disables);
        for(std::size_t i=0;i<Maxwell3D::Regs::MaxConstBuffers;++i) {
            assert(m.state.shader_stages[2].const_buffers[i].enabled==
                   before.shader_stages[2].const_buffers[i].enabled);
            assert(m.state.shader_stages[2].const_buffers[i].address==
                   before.shader_stages[2].const_buffers[i].address);
        }
    }

    // Legal disabled slot still reaches the normal disable path.
    m.regs.bind_groups[1].valid.v=0;
    m.regs.bind_groups[1].shader_slot.v=3;
    m.ProcessCBBind(1);
    assert(m.rasterizer_storage.disables==1);
    assert(m.rasterizer_storage.last_stage==1 && m.rasterizer_storage.last_slot==3);
}
""".replace("__BODY__",body)

with tempfile.TemporaryDirectory(prefix="maxwell-cbbind-oob-") as td:
    td=Path(td); source=td/"test.cpp"; source.write_text(cpp)
    seen=0
    for cc in dict.fromkeys(filter(None,(os.getenv("CXX"),shutil.which("clang++"),shutil.which("g++")))):
        if not cc: continue
        for opt in ("-O0","-O2"):
            exe=td/(Path(cc).name.replace("+","p")+opt)
            build=subprocess.run([cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror","-pedantic",
                                  "-fsanitize=address,undefined","-fno-sanitize-recover=all",
                                  str(source),"-o",str(exe)],capture_output=True,text=True,timeout=90)
            if build.returncode: raise RuntimeError(build.stdout+build.stderr)
            env=os.environ.copy()
            # Apple clang's runtime has no LeakSanitizer; ASan/UBSan remain active.
            if "Apple clang" in subprocess.check_output([cc,"--version"],text=True,stderr=subprocess.STDOUT):
                env["ASAN_OPTIONS"]="detect_leaks=0:halt_on_error=1"
            else:
                env["ASAN_OPTIONS"]="detect_leaks=1:halt_on_error=1"
            run=subprocess.run([str(exe)],capture_output=True,text=True,env=env,timeout=30)
            if run.returncode: raise RuntimeError(run.stdout+run.stderr)
            seen+=1
            print("MAXWELL_CBBIND_SLOT_OOB_PASS",Path(cc).name,opt,
                  "legal17=1 reject18=1 reject31=1 disable3=1")
    assert seen>=2
