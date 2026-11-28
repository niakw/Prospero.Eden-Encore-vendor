#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Lichii #2502: one system exit per session, with immutable NS1 z-order."""
from pathlib import Path
import hashlib
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
active = (root / "encore-active/eden/src/core/hle/service/am/window_system.cpp").read_text()
header = (root / "sources/eden/src/core/hle/service/am/window_system.h").read_text()
base = (root / "sources/eden/src/core/hle/service/am/window_system.cpp").read_text()
old = "    if (m_applets.empty()) {\n        m_system.Exit();\n    }"
once = "    if (m_applets.empty() && !m_exit_sent) {\n        m_exit_sent = true;\n        m_system.Exit();\n    }"
assert active.count(once) == 1
assert active.count("z_index = this->IsOverlayOpenLocked(*applet) ? Overlay : Background;") == 1
assert header.count("bool m_exit_sent{};") == 1
assert header.index("m_applets{};") < header.index("bool m_exit_sent{};")
assert base.count(old) == 1, "non-PS5 NS1 source should remain unchanged"
assert active.count("m_system.Exit();") == 1
# Treat only this small Lichii addition as a permitted delta from the
# previously qualified PS5 R23 #97 source: protects foreground/overlay NS1.
prior = active.replace(once, old, 1).encode()
assert hashlib.sha1(b"blob " + str(len(prior)).encode() + b"\0" + prior).hexdigest() == (
    "c4852c64bdcd491e9c7293db8a7e2dc055f78f5f")
# Exercise the actual statement lifted from the maintained C++ in a minimal
# host harness; a new WindowSystem must be able to exit on its own.
source = r"""
#include <cassert>
#include <vector>
struct ExitSink { unsigned exits{}; void Exit() { ++exits; } };
struct WindowSystem {
    ExitSink& m_system;
    std::vector<int> m_applets;
    bool m_exit_sent{};
    void Update() {
""" + once + r"""
    }
};
int main() {
    ExitSink sink{};
    for (int session = 1; session <= 2; ++session) {
        WindowSystem w{sink, {}, false};
        w.m_applets.push_back(7);
        for (int n = 0; n < 32; ++n) w.Update();
        assert(sink.exits == static_cast<unsigned>(session-1));
        w.m_applets.clear();
        for (int n = 0; n < 4096; ++n) w.Update();
        assert(sink.exits == static_cast<unsigned>(session));
    }
}
"""
with tempfile.TemporaryDirectory(prefix="ns1-ns2-am-exit-once-") as folder:
    p = Path(folder)
    (p / "w.cpp").write_text(source)
    for optimization in ("-O0", "-O2"):
        executable = p / ("w" + optimization)
        subprocess.run(["c++", "-std=c++20", optimization, "-Wall", "-Wextra",
                        "-Werror", str(p/"w.cpp"), "-o", str(executable)],
                       check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=20)
print("NS1_NS2_AM_WINDOW_EXIT_ONCE_PASS sessions=2 updates_per_session=4096 zorder_retained=1 native_only=1")
