#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact-source proof for Lichii #1952/#1957 shutdown lifetime fixes."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
cores = {
    "encore-active/eden-core/src/core/core_standard.cpp":
        "2085b5e5751669e9d3885e80c95cab8bf28dd24e",
    "encore-active/eden-core/src/core/core_native.cpp":
        "ddfc45e5d66578728d13f468e668d2c136f92ef3",
    "encore-active/ns2-runtime/process/core/core.cpp":
        "ac37dcd3d33373d6f5189dc4963f3dfbce898391",
}
early = """        Settings::values.use_squashed_iterated_blend = false;

        // Lichii #1952: the cheat VM runs on CoreTiming. Stop it before
        // SyncPause(false), guest suspension, or service/process teardown.
        // Its destructor unschedules the timer event and joins an active run.
        cheat_engine.reset();
"""
original_flag = """        Settings::values.use_squashed_iterated_blend = false;
"""

def blob(text: str) -> str:
    raw = text.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()

for relative, predecessor in cores.items():
    source = (ROOT / relative).read_text()
    start = "    void ShutdownMainProcess() {"
    stop = "\n    bool IsShuttingDown()"
    assert source.count(start) == source.count(stop) == 1, relative
    before, rest = source.split(start, 1)
    body, after = rest.split(stop, 1)
    shutdown = start + body
    assert shutdown.count("cheat_engine.reset();") == 1, relative
    assert shutdown.count(early) == 1, relative
    order = [
        "cheat_engine.reset();",
        "core_timing.SyncPause(false);",
        "kernel.SuspendEmulation(true);",
        "kernel.CloseServices();",
        "kernel.ShutdownCores();",
        "services.reset();",
        "service_manager.reset();",
    ]
    positions = [shutdown.index(token) for token in order]
    assert positions == sorted(positions), (relative, positions)
    assert "fs_controller.Reset();\n        cheat_engine.reset();" not in shutdown

    old = shutdown.replace(early, original_flag, 1)
    if relative.endswith("core_standard.cpp"):
        anchor = "        kernel.SuspendEmulation(true);\n        kernel.CloseServices();"
        assert old.count(anchor) == 1
        old = old.replace(
            anchor,
            "        kernel.SuspendEmulation(true);\n"
            "        cheat_engine.reset();\n"
            "        kernel.CloseServices();", 1)
    else:
        anchor = (
            '        ::Eden::ReportStep("shutdown", '
            '"Completed kernel.SuspendEmulation(true)");\n'
            "        kernel.CloseServices();"
        )
        assert old.count(anchor) == 1, relative
        old = old.replace(
            anchor,
            '        ::Eden::ReportStep("shutdown", '
            '"Completed kernel.SuspendEmulation(true)");\n'
            "        cheat_engine.reset();\n"
            "        kernel.CloseServices();", 1)
    late = "        fs_controller.Reset();\n        core_timing.ClearPendingEvents();"
    assert old.count(late) == 1, relative
    old = old.replace(
        late,
        "        fs_controller.Reset();\n"
        "        cheat_engine.reset();\n"
        "        core_timing.ClearPendingEvents();", 1)
    reconstructed = before + old + stop + after
    assert blob(reconstructed) == predecessor, (relative, blob(reconstructed), predecessor)

maintained = (ROOT / "encore-active/eden-threading/src/core/hle/kernel/k_thread.cpp").read_text()
pinned = (ROOT / "sources/eden/src/core/hle/kernel/k_thread.cpp").read_text()
start = "    void KThread::FinishTermination(KernelCore& kernel) {"
end = "\n    void KThread::DoWorkerTaskImpl("
assert maintained.count(start) == maintained.count(end) == 1
assert pinned.count(start) == pinned.count(end) == 1
m = start + maintained.split(start, 1)[1].split(end, 1)[0]
p = start + pinned.split(start, 1)[1].split(end, 1)[0]
assert "Finalization may block; scheduling must be enabled here." in m
assert m.index("KScopedSchedulerLock sl{kernel};") < m.index(
    "KSynchronizationObject::NotifyAvailable(kernel);")
assert m.index("KSynchronizationObject::NotifyAvailable(kernel);") < m.index(
    "this->Close(kernel);")
assert "}\n\n        // Finalization may block; scheduling must be enabled here.\n        this->Close(kernel);" in m
assert "// Close the thread.\n        this->Close(kernel);" in p
assert p.index("KScopedSchedulerLock sl{kernel};") < p.index("this->Close(kernel);")
print("SHUTDOWN_LIFETIME_1952_1957_SOURCE_PASS core_variants=3 early_cheat_stop=1"
      " predecessors_exact=3 thread_close_outside_scheduler=1 pinned_thread_unsafe=1")
