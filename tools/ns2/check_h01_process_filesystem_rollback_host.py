#!/usr/bin/env python3
"""H01: execute maintained Process::Initialize/Finalize and filesystem rollback."""
from pathlib import Path
import os,shutil,subprocess,tempfile
R=Path(__file__).resolve().parents[2]
p=(R/"encore-active/ns2-runtime/process/core/hle/service/os/process.cpp").read_text()
f=(R/"encore-active/eden-core/src/core/hle/service/filesystem/filesystem.cpp").read_text()
def part(s,start,end):
    assert s.count(start)==s.count(end)==1,(start,end)
    return start+s.split(start,1)[1].split(end,1)[0]
init=part(p,"bool Process::Initialize(","\nvoid Process::Finalize() {")
final=part(p,"void Process::Finalize() {","\nbool Process::Run() {")
run=part(p,"bool Process::Run() {","\nvoid Process::Terminate() {")
# Late KProcess::Run failure must call the real Finalize owner. The
# filesystem unregister and SVC trace retirement now live in Finalize, not
# directly in Run; check the connected call chain and the cleanup ordering.
assert "this->Finalize();" in run
assert run.index("this->Finalize();") < run.index("m_process_started = true;")
assert "UnregisterProcessIfPresent(" in final
assert final.index("UnregisterProcessIfPresent(") < final.index("SvcTrace::Unregister(")
erase=part(f,"void FileSystemController::UnregisterProcessIfPresent(","\nResult FileSystemController::OpenProcess(")
assert "UnregisterProcessIfPresent(pid)" in init
# Failed Initialize retires the SVC registration by PID directly, even if
# the trace was never installed. The active-PID comparison belongs to Finalize.
assert "Eden::NS2::SvcTrace::Unregister(pid);" in init
assert "SvcTrace::active_pid.load" in final
assert "UnregisterProcessIfPresent(" in final
assert "registrations.erase(process_id)" in erase
assert "UnregisterProcessIfPresent" in (
    R/"encore-active/eden-core/overrides/h01-filesystem/core/hle/service/filesystem/filesystem.h").read_text()
cm=(R/"encore-active/eden-threading/native-hle-overrides.cmake").read_text()
assert "_encore_h01_fs_header_root" in cm and "if(PS5_NATIVE)" in cm
assert "UnregisterProcessIfPresent" not in (
    R/"sources/eden/src/core/hle/service/filesystem/filesystem.h").read_text()
cpp=r"""
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include "common/scope_exit.h"
using u64=std::uint64_t;using ProcessId=u64;
namespace Kernel {
struct KProcess;
struct KernelCore{
  bool fail_create{},fail_append{},fail_run{};
  int closes{},opens{},appends{},terminated{},removed{};
  KProcess* last{};
  void AppendNewProcess(KProcess* p);
  void RemoveProcess(KProcess*){++removed;}
};
struct KProcess{
 KernelCore* k;
 static KProcess* Create(KernelCore& k){
  if(k.fail_create)return nullptr;
  return k.last=new KProcess{&k};
 }
 static void Register(KernelCore&,KProcess*){}
 u64 GetProcessId()const{return 81;}
 void Close(KernelCore& k){++k.closes;}
 void Open(KernelCore& k){++k.opens;}
 void Terminate(KernelCore& k){++k.terminated;}
 struct RunResult{unsigned raw{};};
 RunResult Run(KernelCore& k,int,std::size_t){return {k.fail_run?1u:0u};}
};
void KernelCore::AppendNewProcess(KProcess*){
 if(fail_append)throw std::bad_alloc{};
 ++appends;
}
}
namespace Service::FileSystem{
class FileSystemController{
public:
 std::mutex registration_lock;
 std::map<ProcessId,int> registrations;
 void Register(ProcessId id){registrations.emplace(id,1);}
 void UnregisterProcessIfPresent(ProcessId);
 bool Has(ProcessId id)const{return registrations.contains(id);}
};
__FS__
}
namespace Eden::NS2{
struct SvcTrace{
 static inline std::atomic<u64> active_pid{0};
 static void Register(u64 id){active_pid.store(id);}
 static void Unregister(u64 id){if(active_pid.load()==id)active_pid.store(0);}
};
}
namespace Core{
struct System{
 Kernel::KernelCore& k;Service::FileSystem::FileSystemController& fs;
 Kernel::KernelCore& Kernel(){return k;}
 Service::FileSystem::FileSystemController& GetFileSystemController(){return fs;}
};
}
namespace Loader{
enum class ResultStatus{Success,ErrorNotInitialized,ErrorUnableToParseKernelMetadata,ErrorLoadingNSO};
struct LoadParameters{int main_thread_priority{};std::size_t main_thread_stack_size{};};
struct AppLoader{
 ResultStatus status{};
 std::optional<LoadParameters> parameters{};
 bool register_trace{true},throw_late{};
 std::pair<ResultStatus,std::optional<LoadParameters>> Load(Kernel::KProcess& p,Core::System& s){
  s.fs.Register(p.GetProcessId());
  if(register_trace)Eden::NS2::SvcTrace::Register(p.GetProcessId());
  if(throw_late)throw std::bad_alloc{};
  return {status,parameters};
 }
};
}
namespace Service{
struct Process{
 Core::System& m_system;
 Kernel::KProcess* m_process{};
 int m_main_thread_priority{};
 std::size_t m_main_thread_stack_size{};
 bool m_process_started{};
 void Terminate(){if(m_process)m_process->Terminate(m_system.Kernel());}
 bool Initialize(Loader::AppLoader&,Loader::ResultStatus&);
 void Finalize();
 bool Run();
};
#define EDEN_NS2_PROCESS_RUN_GUARD 1
#define R_FAILED(result) ((result).raw!=0)
#define LOG_ERROR(...) ((void)0)
__INIT__
__FINAL__
__RUN__
}
int main(){
 using Loader::ResultStatus;using Loader::AppLoader;using Loader::LoadParameters;
 for(int case_id=0;case_id<8;++case_id){
  Kernel::KernelCore k;
  Service::FileSystem::FileSystemController fs;
  fs.Register(99);
  Core::System sys{k,fs};
  Service::Process proc{sys};
  if(case_id==0)k.fail_create=true;
  if(case_id==3)k.fail_append=true;
  AppLoader loader;
  loader.status=case_id==1?ResultStatus::ErrorLoadingNSO:ResultStatus::Success;
  loader.parameters=(case_id==2)?std::nullopt:
     std::optional<LoadParameters>{LoadParameters{3,4096}};
  loader.throw_late=case_id==4;
  loader.register_trace=case_id!=6;
  ResultStatus result=ResultStatus::Success;
  bool committed=false,threw=false;
  try{committed=proc.Initialize(loader,result);}
  catch(const std::bad_alloc&){threw=true;}
  assert(fs.Has(99));
  assert(committed==(case_id==5||case_id==6||case_id==7));
  if(committed){
   assert(fs.Has(81));
   if(case_id==7){
     k.fail_run=true;
     assert(!proc.Run());
     assert(!proc.m_process && proc.m_process_started);
     assert(!fs.Has(81)&&Eden::NS2::SvcTrace::active_pid.load()==0);
     // Failure is terminal until a fresh Initialize; neither retry nor
     // a later redundant Finalize may resurrect/re-close retired owners.
     assert(!proc.Run());
     assert(k.removed==1 && k.terminated==1 && k.closes==2);
   }
   proc.Finalize();
   if(case_id==5||case_id==7)assert(!fs.Has(81));
   else assert(fs.Has(81));
   assert(k.removed==1&&k.terminated==1);
  }else{
   if(case_id!=0)assert(!fs.Has(81));
   assert(!proc.m_process&&k.removed==0);
   if(case_id==3||case_id==4)assert(threw);
   assert(Eden::NS2::SvcTrace::active_pid.load()==0);
  }
  assert(k.closes==(case_id==0?0:(committed?2:1)));
  delete k.last;
  Eden::NS2::SvcTrace::active_pid.store(0);
 }
 Service::FileSystem::FileSystemController fs;
 fs.Register(99);fs.UnregisterProcessIfPresent(81);fs.UnregisterProcessIfPresent(81);
 assert(fs.Has(99));
 std::puts("H01_REAL_FS_PROCESS_ROLLBACK_PASS"
    " malformed=retired absent_npdm=retired"
    " append_oom=retired loader_exception=retired run_failure=retired"
    " normal_close=retired foreign_pid=preserved ns1=unchanged"
    " real_guest_pages=not_exercised");
}
""".replace("__FS__",erase).replace("__INIT__",init).replace("__FINAL__",final).replace("__RUN__",run)
with tempfile.TemporaryDirectory(prefix="h01-fs-owner-") as d:
 file=Path(d)/"owner.cpp";file.write_text(cpp)
 for cc in dict.fromkeys(x for x in (shutil.which("clang++"),shutil.which("g++")) if x):
  for opt in ("-O0","-O2"):
   exe=Path(d)/(Path(cc).name.replace("+","p")+opt)
   cmd=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror","-pthread",
        "-fsanitize=undefined","-fno-sanitize-recover=all",
        "-I"+str(R/"sources/eden/src"),str(file),"-o",str(exe)]
   built=subprocess.run(cmd,capture_output=True,text=True,timeout=60,env=os.environ.copy())
   assert built.returncode==0,(cc,built.stderr[-5000:])
   ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15,env=os.environ.copy())
   assert ran.returncode==0,(cc,ran.stderr[-5500:])
   print(Path(cc).name,opt,ran.stdout.strip(),flush=True)
