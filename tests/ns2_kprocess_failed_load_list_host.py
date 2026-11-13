#!/usr/bin/env python3
"""Test real selected PS5 KProcess registration order and slab-link teardown.

Source code contracts plus a compiled C++20 sanitized fixture using the
complete maintained slab_helpers.h. This is host-only, NOT a PS5 boot test.
"""
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
PROCESS=ROOT/"encore-active/ns2-runtime/process/core/hle/service/os/process.cpp"
SLAB=ROOT/"encore-active/eden-threading/src/core/hle/kernel/slab_helpers.h"
proc=PROCESS.read_text()
slab=SLAB.read_text()

begin="bool Process::Initialize(Loader::AppLoader& loader, Loader::ResultStatus& out_load_result) {"
end="\nvoid Process::Finalize() {"
assert proc.count(begin)==1 and proc.count(end)==1
method=begin+proc.split(begin,1)[1].split(end,1)[0]
assert method.count("Kernel::KProcess::Register(m_system.Kernel(), process);")==1
assert method.index("loader.Load(*process, m_system)") < method.index(
    "if (load_result != Loader::ResultStatus::Success)") < method.index(
    "if (!load_parameters)") < method.index(
    "Kernel::KProcess::Register(m_system.Kernel(), process);") < method.index(
    "m_system.Kernel().AppendNewProcess(process);")
assert "if (!process)" in method
assert "out_load_result = Loader::ResultStatus::ErrorNotInitialized;" in method
assert "UnregisterProcessIfPresent(pid);" in method
assert "SvcTrace::Unregister(pid);" in method
assert "process_committed = true;" in method
assert method.index("SCOPE_EXIT {") < method.index("loader.Load(*process, m_system)")
listed=slab.split("class KAutoObjectWithSlabHeapAndContainer",1)[1]
assert listed.count("if (this->is_linked()) {")==1
assert listed.count("kernel.ObjectListContainer().Unregister(this);")==1
assert listed.index("if (this->is_linked()) {") < listed.index(
    "if (is_initialized) {\\n            arg = this->GetPostDestroyArgument()")
assert listed.index("this->Finalize(kernel);") < listed.index(
    "Derived::PostDestroy(kernel, arg);")

stub=r"""
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace Kernel {
class KernelCore;
class KAutoObject {
public:
    explicit KAutoObject(KernelCore&) {}
    virtual ~KAutoObject()=default;
    virtual void Destroy(KernelCore&)=0;
    virtual void Finalize(KernelCore&)=0;
    static void Create(KAutoObject*) {}
};
class KAutoObjectWithList : public KAutoObject {
public:
    explicit KAutoObjectWithList(KernelCore& k) : KAutoObject(k) {}
    bool linked=false;
    bool is_linked() const { return linked; }
};
struct Container {
    unsigned registrations{},unregistrations{};
    void Initialize(){}
    void Register(KAutoObjectWithList* p) {
        assert(p && !p->linked);
        p->linked=true;
        ++registrations;
    }
    void Unregister(KAutoObjectWithList* p) {
        assert(p && p->linked);
        p->linked=false;
        ++unregistrations;
    }
};
template<class T> struct Heap {
    T* Allocate(KernelCore& k){return new T(k);}
    void Free(T* p){delete p;}
    void Initialize(void*, std::size_t){}
    std::size_t GetObjectIndex(const T*)const{return 7;}
    std::size_t GetObjectSize()const{return sizeof(T);}
    std::size_t GetSlabHeapSize()const{return 16;}
    std::size_t GetPeakIndex()const{return 0;}
    std::uintptr_t GetSlabHeapAddress()const{return 0;}
    std::size_t GetNumRemaining()const{return 3;}
};
class KernelCore {
    Container objects;
public:
    template<class T> Heap<T>& SlabHeap() {static Heap<T> h; return h;}
    Container& ObjectListContainer(){return objects;}
};
}
"""
test=r"""
#include "core/hle/kernel/slab_helpers.h"
struct State {
    bool destroyed{},finalized{},post{};
    unsigned arguments{};
};
struct Listed final
: Kernel::KAutoObjectWithSlabHeapAndContainer<Listed,Kernel::KAutoObjectWithList> {
    State& state;
    bool initialized;
    Listed(Kernel::KernelCore& k, State& s, bool init)
    : KAutoObjectWithSlabHeapAndContainer<Listed,Kernel::KAutoObjectWithList>(k),
      state(s), initialized(init){}
    ~Listed()override {assert(!linked);state.destroyed=true;}
    bool IsInitialized()const override{return initialized;}
    void Finalize(Kernel::KernelCore&)override{state.finalized=true;}
    std::uintptr_t GetPostDestroyArgument()const override{
       return reinterpret_cast<std::uintptr_t>(&state);
    }
    static void PostDestroy(Kernel::KernelCore&,std::uintptr_t arg){
       auto* state=reinterpret_cast<State*>(arg);
       assert(state && state->finalized && !state->destroyed);
       state->post=true;
    }
};
int main(){
    for (bool initialized : {false,true}) {
      for (bool linked : {false,true}) {
        Kernel::KernelCore k;
        State state;
        auto* p=new Listed(k,state,initialized);
        if(linked) k.ObjectListContainer().Register(p);
        p->Destroy(k);
        assert(state.destroyed);
        assert(state.finalized==initialized && state.post==initialized);
        assert(k.ObjectListContainer().registrations==static_cast<unsigned>(linked));
        assert(k.ObjectListContainer().unregistrations==static_cast<unsigned>(linked));
      }
    }
}
"""
compilers=[c for c in ("clang++","g++") if shutil.which(c)]
assert compilers,"host C++20 toolchain missing"
with tempfile.TemporaryDirectory(prefix="ps5-ns2-process-list-") as td:
    root=Path(td)
    headers=root/"core/hle/kernel"
    headers.mkdir(parents=True)
    (headers/"k_auto_object.h").write_text(stub)
    for name in ("k_auto_object_container.h","kernel.h"):
        (headers/name).write_text("#pragma once\n")
    (headers/"slab_helpers.h").write_text(slab)
    cpp=root/"test.cpp"
    cpp.write_text("#include <initializer_list>\n"+test)
    for compiler in compilers:
        exe=root/(compiler.replace("+","p")+"-test")
        compile_cmd=[compiler,"-std=c++20","-O1","-Wall","-Wextra","-Werror",
                     "-fsanitize=address,undefined","-fno-sanitize-recover=all",
                     "-I",str(root),str(cpp),"-o",str(exe)]
        checked=subprocess.run(compile_cmd,capture_output=True,text=True,timeout=80)
        assert checked.returncode==0,(compiler,checked.stderr[-5500:])
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=35)
        assert run.returncode==0,(compiler,run.stderr[-3500:])
print("NS2_KPROCESS_LIST_LIFETIME_HOST_PASS source_owner=1 "
      "uninitialized_linked=removed initialized_unlinked=preserved "
      "valid_registered=retired early_load_no_registration=1 PS5=unverified")
