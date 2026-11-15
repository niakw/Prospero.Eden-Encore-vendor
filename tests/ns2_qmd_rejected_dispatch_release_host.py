#!/usr/bin/env python3
"""Host-qualify QMD completion releases when native shader dispatch is rejected.

Upstream contract: Lichii e6e0304303ba / current Eglantine keep QMD releases
moving even if Eden cannot lower a native shader. Encore additionally freezes
the transaction after the first dispatch decision so retries cannot re-read a
mutated descriptor or duplicate dispatch/release work.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = root / "encore-active/ns2-runtime/gpu_dispatch/video_core/native_qmd.h"
source = header.read_text()
assert "Lichii e6e03043" in source
assert "state.dispatch_decided = true;" in source
assert 'error = "native shader dispatch required";' in source
assert 'return "native shader dispatch required";' in source  # RejectShaderDispatch overload remains strict.

cpp = r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>
#include "video_core/native_qmd.h"

using Scheduler = Tegra::NativeQmdScheduler;
using Descriptor = Scheduler::Descriptor;
using Address = Scheduler::Address;
using Word = Scheduler::Word;

static Descriptor MakeQmd(Word payload, bool has_shader=true) {
    Descriptor q{};
    q[18] = 0x30; // QMD 3.0
    if (has_shader) {
        q[12] = 1;
        q[13] = 1;
        q[14] = 1;
    }
    q[24] = 0x1000;
    q[25] = (Word{1} << 30) | (Word{1} << 23);
    q[26] = payload;
    return q;
}

int main() {
    constexpr Address qmd_addr = 0x4000;

    // 1) Rejected shader: report the error, but still publish completion release.
    {
        Scheduler s;
        Descriptor live = MakeQmd(0x12345678);
        unsigned reads=0, dispatches=0, releases=0;
        Word seen_payload=0;
        auto read = [&](Address a, Descriptor& out) {
            assert(a == qmd_addr); ++reads; out = live; return true;
        };
        auto release = [&](Address a, Word payload, Word flags) {
            assert(a == 0x1000); assert((flags >> 30) == 1);
            ++releases; seen_payload=payload; return true;
        };
        auto dispatch = [&](const Descriptor&) { ++dispatches; return false; };
        const std::string_view err = s.Signal(qmd_addr, 3, read, release, dispatch);
        assert(err == "native shader dispatch required");
        assert(reads == 2); // initial load + just-before-first dispatch decision
        assert(dispatches == 1);
        assert(releases == 1 && seen_payload == 0x12345678);

        // Completed QMD is not re-dispatched or re-released.
        const std::string_view retry = s.Signal(qmd_addr, 2, read, release, dispatch);
        assert(retry.empty());
        assert(dispatches == 1 && releases == 1);
    }

    // 2) Partial release failure after rejected dispatch: retry freezes descriptor
    // and payload, and never re-dispatches.
    {
        Scheduler s;
        Descriptor live = MakeQmd(0x11111111);
        unsigned reads=0, dispatches=0, release_calls=0;
        auto read = [&](Address, Descriptor& out) { ++reads; out = live; return true; };
        auto reject_release = [&](Address, Word payload, Word) {
            ++release_calls; assert(payload == 0x11111111); return false;
        };
        auto dispatch = [&](const Descriptor&) { ++dispatches; return false; };
        const std::string_view first = s.Signal(qmd_addr, 3, read, reject_release, dispatch);
        assert(first == "unmapped QMD release");
        assert(dispatches == 1 && release_calls == 1);
        const unsigned frozen_reads = reads;

        // Guest mutates the backing QMD after the failed release. Retry must use
        // the frozen transaction payload, not the new bytes.
        live = MakeQmd(0x22222222);
        Word seen_payload=0;
        auto good_release = [&](Address, Word payload, Word) {
            ++release_calls; seen_payload=payload; return true;
        };
        const std::string_view retry = s.Signal(qmd_addr, 2, read, good_release, dispatch);
        assert(retry == "native shader dispatch required");
        assert(reads == frozen_reads);
        assert(dispatches == 1);
        assert(release_calls == 2 && seen_payload == 0x11111111);
    }

    // 3) Real accepted dispatch keeps the normal success contract.
    {
        Scheduler s;
        Descriptor live = MakeQmd(0x33333333);
        unsigned dispatches=0, releases=0;
        auto read = [&](Address, Descriptor& out) { out=live; return true; };
        auto release = [&](Address, Word payload, Word) {
            ++releases; assert(payload==0x33333333); return true;
        };
        auto dispatch = [&](const Descriptor&) { ++dispatches; return true; };
        const std::string_view err=s.Signal(qmd_addr,3,read,release,dispatch);
        assert(err.empty());
        assert(dispatches==1 && releases==1);
    }

    // 4) No shader payload still completes without an artificial error.
    {
        Scheduler s;
        Descriptor live = MakeQmd(0x44444444, false);
        unsigned dispatches=0, releases=0;
        auto read = [&](Address, Descriptor& out) { out=live; return true; };
        auto release = [&](Address, Word payload, Word) {
            ++releases; assert(payload==0x44444444); return true;
        };
        auto dispatch = [&](const Descriptor&) { ++dispatches; return false; };
        const std::string_view err=s.Signal(qmd_addr,3,read,release,dispatch);
        assert(err.empty());
        assert(dispatches==0 && releases==1);
    }
}
"""

with tempfile.TemporaryDirectory(prefix="ns2-qmd-release-reject-") as td:
    td = Path(td)
    src = td / "test.cpp"
    src.write_text(cpp)
    seen = 0
    for cxx in filter(None, (os.getenv("CXX"), "clang++", "g++")):
        if subprocess.run(["which", cxx], capture_output=True).returncode:
            continue
        for opt in ("-O0", "-O2"):
            exe = td / (Path(cxx).name.replace("+", "p") + opt)
            build = subprocess.run(
                [cxx, "-std=c++20", opt, "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-fsanitize=undefined", "-fno-sanitize-recover=all",
                 "-I", str(root / "encore-active/ns2-runtime/gpu_dispatch"),
                 str(src), "-o", str(exe)],
                text=True, capture_output=True, timeout=90)
            if build.returncode:
                raise RuntimeError(build.stdout + build.stderr)
            run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=30)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            seen += 1
            print("NS2_QMD_REJECT_RELEASE_PASS", Path(cxx).name, opt,
                  "release_on_reject=1 retry_frozen=1 no_duplicate_dispatch=1")
    assert seen >= 2
