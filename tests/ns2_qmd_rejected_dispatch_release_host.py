#!/usr/bin/env python3
"""Regression: rejected preflight must not publish a native shader completion.

The #158 Lichii compatibility candidate conflated unsupported shaders,
unmapped release targets, and failed admission. Restore the previous exact
scheduler until distinct outcomes and full joined GPU tests are qualified.
The real rejected dispatch must not freeze an unaccepted transaction.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = root / "encore-active/eden/src/video_core/native_qmd.h"
source = header.read_text()
assert 'return "native shader dispatch required";' in source
assert "discard_uncommitted_dependent();" in source
assert "if (!dispatched)" in source

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

    // 1) Rejected preflight cannot publish completion, but a later valid
    // action 3 may retry after the missing backing is restored.
    {
        Scheduler s;
        Descriptor live = MakeQmd(0x12345678);
        unsigned dispatches=0, releases=0;
        bool ready=false;
        Word seen_payload=0;
        auto read=[&](Address a, Descriptor& out) {
            assert(a==qmd_addr); out=live; return true;
        };
        auto release=[&](Address a, Word payload, Word flags) {
            assert(a==0x1000 && (flags>>30)==1);
            ++releases; seen_payload=payload; return true;
        };
        auto dispatch=[&](const Descriptor&) {
            ++dispatches; return ready;
        };
        assert(s.Signal(qmd_addr,3,read,release,dispatch)=="native shader dispatch required");
        assert(dispatches==1 && releases==0);
        ready=true;
        live=MakeQmd(0x22222222);
        assert(s.Signal(qmd_addr,3,read,release,dispatch).empty());
        assert(dispatches==2 && releases==1 && seen_payload==0x22222222);
        assert(s.Signal(qmd_addr,2,read,release,dispatch).empty());
        assert(dispatches==2 && releases==1);
    }

    // 2) Once actual dispatch has been accepted, release retries do not
    // re-dispatch the shader or publish a release slot twice.
    {
        Scheduler s;
        Descriptor live=MakeQmd(0x11111111);
        unsigned dispatches=0, release_calls=0;
        auto read=[&](Address, Descriptor& out){ out=live; return true; };
        auto dispatch=[&](const Descriptor&){ ++dispatches; return true; };
        auto reject_release=[&](Address, Word, Word){ ++release_calls; return false; };
        assert(s.Signal(qmd_addr,3,read,reject_release,dispatch)=="unmapped QMD release");
        assert(dispatches==1 && release_calls==1);
        auto release=[&](Address, Word payload, Word){
            assert(payload==0x11111111); ++release_calls; return true;
        };
        assert(s.Signal(qmd_addr,2,read,release,dispatch).empty());
        assert(dispatches==1 && release_calls==2);
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
                 "-I", str(root / "encore-active/eden/src"),
                 str(src), "-o", str(exe)],
                text=True, capture_output=True, timeout=90)
            if build.returncode:
                raise RuntimeError(build.stdout + build.stderr)
            run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=30)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            seen += 1
            print("NS2_QMD_PREFLIGHT_RETRY_PASS", Path(cxx).name, opt,
                  "no_release_on_reject=1 valid_retry=1 no_duplicate_dispatch=1")
    assert seen >= 2
