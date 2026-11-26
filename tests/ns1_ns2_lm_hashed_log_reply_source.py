#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""LM cmd2 Lichii/Eglantine structural parity with NS1 exact-reply preservation."""
from pathlib import Path
import subprocess
import tempfile

R = Path(__file__).resolve().parents[1]
selected = (R / "encore-active/ns2-runtime/hle/core/hle/service/lm/lm.cpp").read_text()
shared = (R / "encore-active/eden-core/src/core/hle/service/service.cpp").read_text()
assert selected.count('{2, &ILogger::TransmitHashedLog, "TransmitHashedLog"}') == 1
assert selected.count('void TransmitHashedLog(HLERequestContext& ctx) {') == 1
assert 'return function_name == "TransmitHashedLog" || function_name == "DevNotify";' in shared
assert "bool IsNx2PlaceholderCaller" in shared
assert "void ServiceFrameworkBase::StubReplyZeroed(HLERequestContext& ctx)" in shared
start = selected.index("    void TransmitHashedLog(HLERequestContext& ctx) {")
end = selected.index("    u64 ReadLeb128(", start)
fn = selected[start:end]
for token in ("ctx.GetThread().GetOwnerProcess()",
              "(process->GetProgramId() >> 56) == 0x04",
              "StubReplyZeroed(ctx);",
              "IPC::ResponseBuilder rb{ctx, 2};",
              "rb.Push(ResultSuccess);"):
    assert token in fn, token
assert fn.index("StubReplyZeroed(ctx);") < fn.index("IPC::ResponseBuilder rb{ctx, 2};")
assert selected.index("GetProgramId() >> 56") > selected.index("void TransmitHashedLog")
code = """
#include <cassert>
#include <cstdint>
#include <vector>
using u64 = std::uint64_t;
static constexpr int ResultSuccess = 0;
struct Process { u64 id; u64 GetProgramId() const { return id; } };
struct Thread { const Process* process; const Process* GetOwnerProcess() const { return process; } };
struct HLERequestContext {
    Thread thread;
    int words{-1};
    std::vector<int> payload{};
    Thread& GetThread() { return thread; }
};
namespace IPC {
struct ResponseBuilder {
    HLERequestContext& ctx;
    ResponseBuilder(HLERequestContext& c, int count) : ctx(c) { ctx.words=count; }
    void Push(int x) { ctx.payload.push_back(x); }
};
}
struct ILogger {
    void StubReplyZeroed(HLERequestContext& ctx) {
        ctx.words=34;
        ctx.payload.assign(33, 0);
    }
""" + fn + """
};
int main() {
    ILogger logger;
    Process ns1{0x0100000000000001ULL}, ns2{0x0400000000000001ULL};
    HLERequestContext one{Thread{&ns1}}, two{Thread{&ns2}}, unknown{Thread{nullptr}};
    logger.TransmitHashedLog(one);
    logger.TransmitHashedLog(two);
    logger.TransmitHashedLog(unknown);
    assert(one.words==2 && one.payload.size()==1 && one.payload[0]==0);
    assert(unknown.words==2 && unknown.payload.size()==1 && unknown.payload[0]==0);
    assert(two.words==34 && two.payload.size()==33);
    for (int v : two.payload) assert(v==0);
}
"""
with tempfile.TemporaryDirectory(prefix="lm-ns1-ns2-") as dir:
    from pathlib import Path as P
    src=P(dir)/"lm.cpp"
    src.write_text(code)
    for opt in ("-O0","-O2"):
        exe=P(dir)/("lm"+opt)
        subprocess.run(["c++","-std=c++20",opt,"-Wall","-Wextra",
                        "-Werror",str(src),"-o",str(exe)],check=True,timeout=30)
        subprocess.run([str(exe)],check=True,timeout=15)
print("LM_NX2_HASHED_LOG_PARITY_PASS id=2 ns1_short_success=1 ns2_initialized_stub=1 null_process_short=1")
