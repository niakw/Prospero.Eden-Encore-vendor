#!/usr/bin/env python3
from pathlib import Path
import shutil, subprocess, tempfile
R=Path(__file__).resolve().parents[1]
hdr=(R/"encore-active/ns2-helpers/include/core/loader/ns2_svc_trace.h").read_text()
svc=(R/"encore-active/ns2-runtime/gpu_access_svc/core/hle/kernel/svc.cpp").read_text()
std=(R/"encore-active/eden-core/src/core/hle/kernel/physical_core_standard.cpp").read_text()
dev=(R/"encore-active/eden-core/src/core/hle/kernel/physical_core_dev.cpp").read_text()
for marker in ("RecentSamples = 64","RecordRecent(","DumpRecent(",
               "EDEN_NS2_RECENT_SVC_BEGIN","EDEN_NS2_RECENT_SVC_END"):
    assert marker in hdr, marker
assert svc.index("SaveSvcArguments(process, args)") < svc.index("SvcTrace::RecordRecent")
assert 'extern "C" void eden_ns2_dump_recent_svc' in svc
for src in (std,dev):
    assert "EDEN_GUEST_FAULT_REGS" in src
    assert "fault_context.r[7]" in src
    assert "eden_ns2_dump_recent_svc(process->GetProcessId())" in src

cc=shutil.which("clang++") or shutil.which("g++")
assert cc
program=r'''
#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include "core/loader/ns2_svc_trace.h"
int main(){
  using namespace Eden::NS2::SvcTrace;
  Register(9);
  std::array<std::uint64_t,8> a{};
  for(std::uint64_t i=1;i<=80;i++){
    a[0]=i; a[1]=i+100;
    RecordRecent(9, static_cast<std::uint32_t>(0x20+i), true, a);
  }
  assert(recent_sequence.load()==80);
  FILE* f=tmpfile(); assert(f);
  DumpRecent(9,f); fflush(f); rewind(f);
  std::string text; char buf[1024];
  while(fgets(buf,sizeof(buf),f)) text+=buf;
  fclose(f);
  assert(text.find("first=17 last=80 capacity=64")!=std::string::npos);
  assert(text.find("seq=17 ")!=std::string::npos);
  assert(text.find("seq=80 ")!=std::string::npos);
  assert(text.find("x0=0x50")!=std::string::npos);
  ResetForTitle();
  assert(!IsActive(9));
  assert(recent_sequence.load()==0);
}
'''
with tempfile.TemporaryDirectory(prefix="ns2-svc-ring-") as d:
    p=Path(d); src=p/"t.cpp"; exe=p/"t"; src.write_text(program)
    subprocess.run([cc,"-std=c++20","-O2","-Wall","-Wextra","-Werror","-pthread",
                    "-I",str(R/"encore-active/ns2-helpers/include"),str(src),"-o",str(exe)],
                   check=True,timeout=60)
    subprocess.run([str(exe)],check=True,timeout=10)
print("NS2_RECENT_SVC_FAULT_TRACE_HOST_PASS ring=64 regs=x0-x7 fatal_dump=1")
