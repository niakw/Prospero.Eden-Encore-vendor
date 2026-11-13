#!/usr/bin/env python3
"""Real PS5-selected GPU destructor ordering source and ASan/UBSan host proof.

Eglantine #462 found renderer touching destroyed Maxwell channel flags.
Keep the G21 GPU worker stopped and renderer gone before channel destruction.
This is host lifecycle proof, not a PS5 console exit acceptance.
"""
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OWNERS = {
    "vulkan": ROOT / "encore-active/eden-core/src/video_core/gpu_vulkan.cpp",
    "gl_native": ROOT / "encore-active/eden-core/src/video_core/gpu_gl_native.cpp",
    "gl_host": ROOT / "encore-active/eden-core/src/video_core/gpu_gl_host.cpp",
}
BASE = ROOT / "sources/eden/src/video_core/gpu.cpp"
historic=BASE.read_text()
assert "    ~Impl() = default;" in historic
assert historic.index("std::unique_ptr<VideoCore::RendererBase> renderer;") < historic.index(
    "::Common::unordered_map<s32, std::shared_ptr<Tegra::Control::ChannelState>> channels;")

mock = r"""
#include <cassert>
#include <memory>
#include <string>
#include <cstdio>
#include <cstdlib>
struct State {
    bool channels_alive=true, worker_running=true, notified=false;
    bool context_current=false, renderer_destroyed=false;
    unsigned stops=0, renderer_destroys=0, gl_finish=0;
};
static State state;
namespace Settings { bool IsOpenGL() {return true;} }
static void glFinish(){
    assert(!state.worker_running);
    assert(state.context_current && state.channels_alive);
    ++state.gl_finish;
}
struct ThreadManager {
    void Stop() {state.worker_running=false;++state.stops;}
    ~ThreadManager(){Stop();}
};
struct Channel {
    ~Channel(){assert(state.renderer_destroyed);state.channels_alive=false;}
};
struct Renderer {
    struct GraphicsContext {
        void MakeCurrent(){state.context_current=true;}
    };
    GraphicsContext context;
    GraphicsContext& Context(){return context;}
    ~Renderer(){
       assert(state.channels_alive && !state.worker_running && state.notified);
       state.renderer_destroyed=true;
       ++state.renderer_destroys;
    }
};
struct Impl {
    void NotifyShutdown(){state.notified=true;}
    // This mirrors the exact dangerous C++ member declaration order
    // from gpu_vulkan.cpp: renderer before channels, worker last.
    std::unique_ptr<Renderer> renderer{std::make_unique<Renderer>()};
    std::unique_ptr<Channel> channels{std::make_unique<Channel>()};
    ThreadManager gpu_thread{};
__EXTRACTED_DESTRUCTOR__
};
int main(){
   { Impl impl; }
   assert(!state.channels_alive && state.renderer_destroyed);
   assert(!state.worker_running && state.notified);
   assert(state.renderer_destroys==1 && state.stops>=1);
__FINISH_ASSERT__
}
"""
compilers = [x for x in ("clang++","g++") if shutil.which(x)]
assert compilers, "C++20 compiler unavailable"
with tempfile.TemporaryDirectory(prefix="ps5-gpu-owner-shutdown-") as td:
    root=Path(td)
    for name,path in OWNERS.items():
        source=path.read_text()
        token="    ~Impl() {"
        assert source.count(token)==1
        begin=source.index(token)
        end=source.index("    std::shared_ptr<Control::ChannelState> CreateChannel(",begin)
        destructor=source[begin:end].strip()
        assert destructor.startswith("~Impl() {") and destructor.endswith("}")
        assert destructor.count("NotifyShutdown();")==1
        assert destructor.count("gpu_thread.Stop();")==1
        assert destructor.count("renderer.reset();")==1
        assert destructor.index("NotifyShutdown();") < destructor.index(
            "gpu_thread.Stop();") < destructor.index("renderer.reset();")
        assert source.index("std::unique_ptr<VideoCore::RendererBase> renderer;") < source.index(
            "::Common::unordered_map<s32, std::shared_ptr<Tegra::Control::ChannelState>> channels;")
        assert source.index("::Common::unordered_map<s32, std::shared_ptr<Tegra::Control::ChannelState>> channels;") < source.index(
            "VideoCommon::GPUThread::ThreadManager gpu_thread;")
        if name=="vulkan":
            assert "glFinish()" not in destructor
            gl_clause="assert(state.gl_finish==0);"
        else:
            assert destructor.index("renderer->Context().MakeCurrent();") < destructor.index(
                "glFinish();") < destructor.index("renderer.reset();")
            gl_clause="assert(state.gl_finish==1 && state.context_current);"
        test=mock.replace("__EXTRACTED_DESTRUCTOR__",destructor).replace(
            "__FINISH_ASSERT__",gl_clause)
        file=root/(name+".cpp");file.write_text(test)
        for compiler in compilers:
            exe=root/(name+"-"+compiler.replace("+","p"))
            proc=subprocess.run([compiler,"-std=c++20","-O1","-Wall","-Wextra",
                                 "-Werror","-fsanitize=address,undefined",
                                 "-fno-sanitize-recover=all",str(file),"-o",str(exe)],
                                 capture_output=True,text=True,timeout=60)
            assert proc.returncode==0,(name,compiler,proc.stderr[-4000:])
            proc=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            assert proc.returncode==0,(name,compiler,proc.stderr[-2000:])
print("PS5_GPU_SHUTDOWN_RENDERER_BEFORE_CHANNELS_HOST_PASS "
      "real_selected_g21_owners=3 GPU_worker_stopped=1 "
      "OpenGL_context_preserved=1 channel_uaf_order=prevented "
      "console_exit=unverified")
