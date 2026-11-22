#!/usr/bin/env python3
"""NS1 R23 #54 bounded IStorage paced-read and generic service sleep removal."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
root=r/"encore-active/eden-core"
pins={
"src/core/hle/service/filesystem/fsp/fs_i_storage.cpp":"3513150b1a39297207bc16ccaadec8f1a8fdeb96",
"include/core/hle/service/filesystem/fsp/fs_i_storage.h":"26dbf4505aa00ac17967e2f1bd6c32a2e856bc78",
"src/core/hle/service/service.cpp":"de718afada4741c2c9834c0269cd6e2a27de4138",
"include/core/hle/service/service.h":"1b4f515b541617b6b8f134ff21570f7fbdce910f",
}
for path,expect in pins.items():
 data=(root/path).read_bytes()
 got=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
 if path=="src/core/hle/service/service.cpp" and got=="5d5bbd9bbf8d7b7a483cef5d7395af2f571e1985":
  # Exact NS2 Lichii structural successor: reverse insertions only on the
  # audit copy, preserving the frozen NS1 IStorage pacing baseline.
  source_text=data.decode()
  for insert in ("#include <deque>\n#include <memory>\n#include <mutex>\n#include <string>\n",
    "#include \"core/hle/kernel/k_event.h\"\n",
    "#include \"core/hle/service/kernel_helpers.h\"\n",
    "    if (stub_event) {\n        stub_context->CloseEvent(stub_event);\n    }\n",
    "namespace {\nconstexpr u32 StubZeroedWords = 32;\nvoid PushStubZeroes(IPC::ResponseBuilder& rb) {\n    for (u32 i = 0; i < StubZeroedWords; ++i) rb.Push<u32>(0);\n}\n// Explicit NS2 AM getters return this placeholder interface instead of\n// an invalid null object; its unknown requests receive zeroed words.\nclass INx2PlaceholderInterface final : public ServiceFramework<INx2PlaceholderInterface> {\npublic:\n    explicit INx2PlaceholderInterface(Core::System& system, const char* name)\n        : ServiceFramework{system, name} {}\n};\nconst char* InternPlaceholderName(std::string_view owner, u32 command) {\n    static std::mutex names_mutex;\n    static std::deque<std::string> names;\n    const auto label = fmt::format(\"nx2:{}::{}\", owner, command);\n    std::scoped_lock lock{names_mutex};\n    for (const auto& name : names) {\n        if (name == label) return name.c_str();\n    }\n    return names.emplace_back(label).c_str();\n}\n} // namespace\n\nvoid ServiceFrameworkBase::StubReplyZeroed(HLERequestContext& ctx) {\n    LOG_WARNING(Service, \"(STUBBED) NS2 {}::{} -> zero reply\", service_name, ctx.GetCommand());\n    IPC::ResponseBuilder rb{ctx, 2 + StubZeroedWords};\n    rb.Push(ResultSuccess);\n    PushStubZeroes(rb);\n}\nvoid ServiceFrameworkBase::StubReplyEvent(HLERequestContext& ctx) {\n    if (!stub_event) {\n        stub_context = std::make_unique<KernelHelpers::ServiceContext>(system, service_name);\n        stub_event = stub_context->CreateEvent(fmt::format(\"{}:StubEvent\", service_name));\n    }\n    LOG_WARNING(Service, \"(STUBBED) NS2 {}::{} -> unsignaled event\", service_name, ctx.GetCommand());\n    IPC::ResponseBuilder rb{ctx, 2 + StubZeroedWords, 1, 0};\n    rb.Push(ResultSuccess);\n    PushStubZeroes(rb);\n    rb.PushCopyObjects(ctx, stub_event->GetReadableEvent());\n}\nvoid ServiceFrameworkBase::StubReplyInterface(HLERequestContext& ctx) {\n    const char* name = InternPlaceholderName(service_name, ctx.GetCommand());\n    LOG_WARNING(Service, \"(STUBBED) NS2 {}::{} -> interface {}\", service_name, ctx.GetCommand(), name);\n    IPC::ResponseBuilder rb{ctx, 2, 0, 1};\n    rb.Push(ResultSuccess);\n    rb.PushIpcInterface<INx2PlaceholderInterface>(ctx, system, name);\n}\n\n",
    "    // Only interfaces created by an explicit NS2 stub getter may use\n    // this zero-filled unknown-command fallback; all real services stay closed.\n    if (std::string_view{service_name}.starts_with(\"nx2:\")) {\n        StubReplyZeroed(ctx);\n        return;\n    }\n"):
   assert source_text.count(insert)==1,("NS2 structural insertion drift",path,insert[:80])
   source_text=source_text.replace(insert,"",1)
  data=source_text.encode()
  got=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
  assert got=="e2d52974d419b9ed4089da8baed104dd1dbd282d",("NS2 structural reverse failed",path,got)
 if path=="include/core/hle/service/service.h" and got=="2cc24bdab5d0de33878fed005e406e7c3fad35a4":
  # Exact NS2 Lichii structural successor: reverse insertions only on the
  # audit copy, preserving the frozen NS1 IStorage pacing baseline.
  source_text=data.decode()
  for insert in ("#include <memory>\n",
    "class KEvent;\n",
    "namespace KernelHelpers { class ServiceContext; }\n\n",
    "\n    // Lichii NS2 structural reply shapes. Only explicitly registered\n    // commands reach these methods; unknown services remain fail-closed.\n    void StubReplyZeroed(HLERequestContext& ctx);\n    void StubReplyEvent(HLERequestContext& ctx);\n    void StubReplyInterface(HLERequestContext& ctx);\n",
    "    // Lazy service-owned event, with the normal kernel resource lifetime.\n    Kernel::KEvent* stub_event{};\n    std::unique_ptr<KernelHelpers::ServiceContext> stub_context;\n\n"):
   assert source_text.count(insert)==1,("NS2 structural insertion drift",path,insert[:80])
   source_text=source_text.replace(insert,"",1)
  data=source_text.encode()
  got=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
  assert got=="1b4f515b541617b6b8f134ff21570f7fbdce910f",("NS2 structural reverse failed",path,got)
 if path=="src/core/hle/service/service.cpp" and got=="e2d52974d419b9ed4089da8baed104dd1dbd282d":
  # NS2 unknown IPC IDs now produce a fail-closed reply. Reconstruct only
  # that exact successor on an audit copy before the frozen NS1 R23 proof.
  new="""    } else {
        // Unknown IDs need a complete IPC reply too. Previously, only known
        // IDs with null handlers were fail-closed; an unregistered NS2 command
        // could fall through without building any response at all.
        LOG_WARNING(Service, "Unsupported IPC command: {}::{} ({})", service_name,
                    ctx.GetCommand(), function_name);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultNotImplemented);
    }"""
  old="""    } else if (info != nullptr) {
        // not implemented yet on Encore: this command ID is known, but no handler contract exists.
        // Fail closed instead of leaving request words in the outgoing IPC buffer or faking success.
        LOG_WARNING(Service, "Known command has no Encore implementation: {}::{} ({})", service_name,
                    ctx.GetCommand(), function_name);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultNotImplemented);
    }"""
  text=data.decode()
  assert text.count(new)==1
  text=text.replace(new,old,1)
  data=text.encode()
  got=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
  assert got=="be358b0db916b14ab3e92b22a67ec56505b3d86e",got
 if path=="src/core/hle/service/service.cpp" and got=="be358b0db916b14ab3e92b22a67ec56505b3d86e":
  # Exact four reviewed NS1 service-side safety deltas, not a generic success stub.
  text=data.decode()
  assert text.count("IsSafeOptionalNoOp(")==2
  assert "rb.Push(Kernel::ResultNotImplemented);" in text
  assert "if (info != nullptr && IsSafeOptionalNoOp(service_name, function_name))" in text
  lines=text.split("\n")
  inverse=(
   (19, ["#include \"core/hle/kernel/svc_results.h\"","#include \"core/hle/service/ipc_helpers.h\"","#include \"core/hle/service/service.h\"","#include \"core/hle/service/sm/sm.h\""], ["#include \"core/hle/service/ipc_helpers.h\"","#include \"core/hle/service/service.h\"","#include \"core/hle/service/sm/sm.h\""]),
   (27, ["[[nodiscard]] static bool IsSafeOptionalNoOp(std::string_view service_name,","                                             std::string_view function_name) {","    // not implemented yet on Encore: these commands only publish optional","    // notifications/telemetry. Eden does not require their side effects for","    // gameplay, so acknowledge them without inventing a larger subsystem.","    if (service_name == \"pdm:ntfy\") {","        return function_name == \"NotifyAppletEvent\" ||","               function_name == \"NotifyOperationModeChangeEvent\" ||","               function_name == \"NotifyPowerStateChangeEvent\" ||","               function_name == \"NotifyClearAllEvent\" ||","               function_name == \"NotifyEventForDebug\" ||","               function_name == \"SuspendUserAccountEventService\" ||","               function_name == \"ResumeUserAccountEventService\" ||","               function_name == \"NotifyLibraryAppletEvent\";","    }","    if (service_name == \"ILogger\") {","        return function_name == \"TransmitHashedLog\" || function_name == \"DevNotify\";","    }","    if (service_name == \"erpt:c\") {","        return function_name == \"SetInitialLaunchSettingsCompletionTime\" ||","               function_name == \"ClearInitialLaunchSettingsCompletionTime\" ||","               function_name == \"UpdatePowerOnTime\" ||","               function_name == \"UpdateApplicationLaunchTime\" ||","               function_name == \"ClearApplicationLaunchTime\" ||","               function_name == \"RegisterRunningApplet\" ||","               function_name == \"UnregisterRunningApplet\" ||","               function_name == \"UpdateAppletSuspendedDuration\" ||","               function_name == \"InvalidateForcedShutdownDetection\";","    }","    if (service_name == \"eupld:c\") {","        return function_name == \"SetUrl\" || function_name == \"ImportCrt\" ||","               function_name == \"ImportPki\" || function_name == \"SetAutoUpload\";","    }","    if (service_name == \"ICommonStateGetter\") {","        return function_name == \"AllowToEnterSleep\" ||","               function_name == \"DisallowToEnterSleep\";","    }","    if (service_name == \"ISelfController\") {","        return function_name == \"SetDesirableKeyboardLayout\" ||","               function_name == \"ReportMultimediaError\";","    }","    return false;","}","","/// @brief Creates a function string for logging, complete with the name (or header code, depending","/// on what's passed in) the port name, and all the cmd_buf arguments.","[[maybe_unused]] static std::string MakeFunctionString(std::string_view name, std::string_view port_name, const u32* cmd_buf) {"], ["/// @brief Creates a function string for logging, complete with the name (or header code, depending","/// on what's passed in) the port name, and all the cmd_buf arguments.","[[maybe_unused]] static std::string MakeFunctionString(std::string_view name, std::string_view port_name, const u32* cmd_buf) {"]),
   (120, ["    if (info != nullptr && IsSafeOptionalNoOp(service_name, function_name)) {","        // not implemented yet on Encore: optional service with no guest-visible","        // output contract needed by Eden. Deliberately succeed as a no-op.","        LOG_WARNING(Service, \"(STUBBED) optional no-op {}::{} ({})\", service_name,","                    ctx.GetCommand(), function_name);","        IPC::ResponseBuilder rb{ctx, 2};","        rb.Push(ResultSuccess);","        return;","    }","","    system.GetReporter().SaveUnimplementedFunctionReport(ctx, ctx.GetCommand(), function_name, service_name);","    UNIMPLEMENTED_MSG(\"Unknown / unimplemented {}\", fmt::to_string(buf));","    if (Settings::values.use_auto_stub) {"], ["    system.GetReporter().SaveUnimplementedFunctionReport(ctx, ctx.GetCommand(), function_name, service_name);","    UNIMPLEMENTED_MSG(\"Unknown / unimplemented {}\", fmt::to_string(buf));","    if (Settings::values.use_auto_stub) {"]),
   (136, ["    } else if (info != nullptr) {","        // not implemented yet on Encore: this command ID is known, but no handler contract exists.","        // Fail closed instead of leaving request words in the outgoing IPC buffer or faking success.","        LOG_WARNING(Service, \"Known command has no Encore implementation: {}::{} ({})\", service_name,","                    ctx.GetCommand(), function_name);","        IPC::ResponseBuilder rb{ctx, 2};","        rb.Push(Kernel::ResultNotImplemented);","    }","}",""], ["    }","}",""]),
  )
  for index,new,old in reversed(inverse):
   assert lines[index:index+len(new)]==new, "Unreviewed optional IPC source delta"
   lines[index:index+len(new)]=old
  historical="\n".join(lines).encode()
  previous=hashlib.sha1(b"blob "+str(len(historical)).encode()+bytes([0])+historical).hexdigest()
  assert previous==expect, ("NS1 optional no-op reverse failed",previous)
 else:
  assert got==expect,(path,got)
source=(root/"src/core/hle/service/filesystem/fsp/fs_i_storage.cpp").read_text()
service=(root/"src/core/hle/service/service.cpp").read_text()
assert "std::this_thread::sleep_for(std::chrono::microseconds{600})" not in service
assert "is_i_storage" not in service
assert "system.IsNVDECActiveForProcess" not in service
assert "next_read_tick" in source
assert "out_bytes.size()" in source
start="    R_UNLESS(length >= 0, FileSys::ResultInvalidSize);"
end="    // Read the data from the Storage backend"
block=start+source.split(start,1)[1].split(end,1)[0]
code=r"""
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
using u64=std::uint64_t;
using s64=std::int64_t;
using u8=std::uint8_t;
namespace FileSys { constexpr int ResultInvalidSize=1, ResultInvalidOffset=2;}
#define R_UNLESS(condition,error) if (!(condition)) return (error)
struct IStorage {
 static constexpr s64 ReadBytesPerSecond=40'000'000;
 static constexpr s64 MaxChargedReadSize=64*1024;
 std::chrono::steady_clock::time_point next_read_tick{};
 int Read(s64 offset,s64 length,std::span<u8> out_bytes) {
""" + block + r"""
 return 0;
 }
};
int main(){
 IStorage s;
 u8 bytes[4096]{};
 assert(s.Read(0,-1,bytes)==1);
 assert(s.Read(-1,1,bytes)==2);
 assert(s.Read(0,4097,bytes)==1);
 auto before=std::chrono::steady_clock::now();
 assert(s.Read(0,4096,bytes)==0);
 auto tick=s.next_read_tick;
 assert(tick>before);
 assert(s.Read(0,4096,bytes)==0);
 assert(s.next_read_tick>tick);
}
""";
with tempfile.TemporaryDirectory(prefix="ns1-storage-54-") as t:
 p=Path(t)
 (p/"test.cpp").write_text(code)
 subprocess.run([os.environ.get("CXX","clang++"),"-std=c++20","-g","-O1",
  "-fsanitize=address,undefined",str(p/"test.cpp"),"-o",str(p/"test")],check=True,timeout=70)
 subprocess.run([str(p/"test")],check=True,timeout=20,
  env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_54_STORAGE_PACING_ASAN_UBSAN_PASS bound=1 40MBps=1 global_sleep=0")
