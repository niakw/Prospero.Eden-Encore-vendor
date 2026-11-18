#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run real bounded SVC-entry/outcome ring code with synthetic NS2 crash cases.

Does not claim that the guest SVC, JIT or PS5 game is fixed. Both O0 and O2
exercise independently compiled production ring helpers, not reimplementations.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

R = Path(__file__).resolve().parents[1]
hdr = (R / "encore-active/ns2-helpers/include/core/loader/ns2_svc_trace.h").read_text()
svc = (R / "encore-active/ns2-runtime/gpu_access_svc/core/hle/kernel/svc.cpp").read_text()
std = (R / "encore-active/eden-core/src/core/hle/kernel/physical_core_standard.cpp").read_text()
dev = (R / "encore-active/eden-core/src/core/hle/kernel/physical_core_dev.cpp").read_text()

for marker in ("RecentSamples = 64", "RecordRecent(", "RecordOutcome(", "DumpRecent(",
               "outcome=completed", "outcome=pending", "EDEN_NS2_RECENT_SVC_BEGIN",
               "EDEN_NS2_RECENT_SVC_END", "outcome_sequence", "FormatCriticalRecent("):
    assert marker in hdr, marker
call = svc.split("void Call(Core::System& system, u32 imm) {", 1)[1]
assert call.index("SaveSvcArguments(process, args)") < call.index("SvcTrace::RecordRecent")
assert call.index("GetContext(svc_context)") < call.index("Call64(system, imm, args);")
assert call.index("Call64(system, imm, args);") < call.index("SvcTrace::RecordOutcome")
assert call.index("SvcTrace::RecordOutcome") < call.index("LoadSvcArguments(process, args)")
assert "process.Is64Bit() && (imm == 0x06 || imm == 0x29)" in call
assert 'extern "C" void eden_ns2_dump_recent_svc' in svc
assert 'eden_native_capture_ns2_svc_fault(pid, fault_pc, fault_lr, fault_sp, last_svc)' in svc
assert '"svc=not_observed"' in svc
assert "std::uint64_t pid, std::uint64_t fault_pc" in svc
assert svc.index("SvcTrace::DumpRecent(pid, stderr)") < svc.index(
    "SvcTrace::FormatCriticalRecent(pid, last_svc, sizeof(last_svc))")
for src in (std, dev):
    assert "EDEN_GUEST_FAULT_REGS" in src
    assert "fault_context.r[7]" in src
    assert "eden_ns2_dump_recent_svc(process->GetProcessId()," in src
    assert "fault_context.pc, fault_context.lr, fault_context.sp)" in src
    assert "fault_pc, std::uint64_t fault_lr" in src

cc = shutil.which("clang++") or shutil.which("g++")
assert cc, "C++ compiler required"
program = r"""
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
#include "core/loader/ns2_svc_trace.h"
#ifdef PS5_NATIVE
static bool logging_enabled = true;
extern "C" bool eden_native_detailed_logging() noexcept { return logging_enabled; }
#endif
std::string dump(std::uint64_t pid) {
    FILE* f = tmpfile(); assert(f);
    Eden::NS2::SvcTrace::DumpRecent(pid, f);
    fflush(f); rewind(f);
    std::string text;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) text += buf;
    fclose(f);
    return text;
}
int main() {
    using namespace Eden::NS2::SvcTrace;
    Register(9);
    std::array<std::uint64_t, 8> a{};
    assert(RecordRecent(7, 0x29, true, a) == 0); // foreign owner
    for (std::uint64_t i = 1; i <= 80; ++i) {
        a[0] = i;
        a[1] = i + 100;
        const auto imm = i == 78 || i == 80 ? 0x29u : i == 79 ? 0x06u : 0x30u;
        const auto seq = RecordRecent(9, imm, true, a);
        assert(seq == i);
        if (i == 78)
            RecordOutcome(9, seq, 0, 0x12345000, 0x1083d03f80, 0x80298004);
        if (i == 79)
            RecordOutcome(9, seq, 0xdead, 0, 0x1083d03f40, 0x80298008);
        // i=80 simulates pre-empted/faulted dispatcher: outcome is NOT success.
    }
    RecordOutcome(9, 10, 0, 0xfeed, 1, 1); // overwritten ring slot
    RecordOutcome(7, 80, 0, 0xfeed, 1, 1); // wrong process
    assert(recent_sequence.load() == 80);
    const auto text = dump(9);
    assert(text.find("first=17 last=80 capacity=64") != std::string::npos);
    assert(text.find("seq=17 ") != std::string::npos);
    assert(text.find("seq=80 ") != std::string::npos);
    assert(text.find("seq=16 ") == std::string::npos);
    assert(text.find("seq=78 imm=0x29") != std::string::npos);
    assert(text.find("outcome=completed result=0x0 value=0x12345000 svc_sp=0x1083d03f80 svc_pc=0x80298004") != std::string::npos);
    assert(text.find("seq=79 imm=0x6") != std::string::npos);
    assert(text.find("outcome=completed result=0xdead value=0x0 svc_sp=0x1083d03f40 svc_pc=0x80298008") != std::string::npos);
    assert(text.find("seq=80 imm=0x29") != std::string::npos);
    assert(text.find("outcome=pending") != std::string::npos);
    assert(text.find("value=0xfeed") == std::string::npos);
char recent_critical[240]{};
    assert(FormatCriticalRecent(9, recent_critical, sizeof(recent_critical)) != 0);
    assert(std::string(recent_critical).find("svc=0x29 pending") != std::string::npos);
    assert(std::string(recent_critical).find("svc=0x6 done r=0xdead") != std::string::npos);
#ifdef PS5_NATIVE
    logging_enabled = false;
    assert(RecordRecent(9, 0x30, true, a) == 0); // no quiet verbosity
    const auto quiet_seq = RecordRecent(9, 0x29, true, a);
    assert(quiet_seq == 81); // critical ABI witness always kept
    RecordOutcome(9, quiet_seq, 0x123, 0xbeef, 0x1083d03f00, 0x8029800c);
    assert(dump(9).empty()); // no verbose stdout/stderr
    assert(FormatCriticalRecent(9, recent_critical, sizeof(recent_critical)) != 0);
    assert(std::string(recent_critical).find("svc=0x29 done r=0x123 v=0xbeef") != std::string::npos);
    assert(std::string(recent_critical).find("svc=0x29 pending") != std::string::npos);
    logging_enabled = true;
    assert(dump(9).find("value=0xbeef") != std::string::npos);
#endif
    ResetForTitle();
    assert(!IsActive(9));
    assert(recent_sequence.load() == 0);
    Register(10);
    assert(dump(10).find("seq=78 ") == std::string::npos);
    assert(RecordRecent(9, 0x29, true, a) == 0);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="ns2-svc-ring-outcome-") as d:
    p = Path(d)
    src, exe = p / "t.cpp", p / "t"
    src.write_text(program)
    for opt in ("-O0", "-O2"):
        for defines in ([], ["-DPS5_NATIVE"]):
            subprocess.run(
                [cc, "-std=c++20", opt, "-Wall", "-Wextra", "-Werror",
                 "-pthread", *defines,
                 "-I", str(R / "encore-active/ns2-helpers/include"),
                 str(src), "-o", str(exe)],
                check=True, capture_output=True, text=True, timeout=60)
            subprocess.run([str(exe)], check=True, timeout=10)
print("NS2_RECENT_SVC_OUTCOME_HOST_PASS ring=64 contexts=pc_sp results=x0_x1 pending=1 wrap=1 reset=1 quiet_critical=1 fatal_snapshot=1 O0=1 O2=1")
print("Source/host only: no guest SVC correctness, native PS5 build or NS2 boot is claimed.")
