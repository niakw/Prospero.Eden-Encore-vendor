#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact-source proof for Lichii #1932 kernel-object shutdown lifetime."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
cores = {
    "encore-active/eden-core/src/core/core_standard.cpp":
        "5fbd4b90de30d20bff7a9fe07be85e4fc187fa93",
    "encore-active/eden-core/src/core/core_native.cpp":
        "ba5b2cf06c5885bafc2e16c424bc146e24455b00",
    "encore-active/ns2-runtime/process/core/core.cpp":
        "7e3159eb648e92cca3543cfd01bcfd26a0caa135",
}

loader_after = """            // The kernel was initialized before loader admission. Leaving it
            // alive retains lists into device memory already being destroyed,
            // and can crash during failed-launch teardown or next title.
            // Lichii #1932: the local KProcess must die while its kernel is alive.
            process.reset();
            ShutdownMainProcess();"""
loader_before = """            // The kernel was initialized before loader admission. Leaving it
            // alive retains lists into device memory already being destroyed,
            // and can crash during failed-launch teardown or next title.
            ShutdownMainProcess();"""
shutdown_after = """        debugger.reset();
        {
            // Lichii #1932: these are kernel-owned objects. Destroy them while
            // the kernel still exists; keep queued general-channel bytes for
            // the next kernel/session.
            std::scoped_lock lk{general_channel_mutex};
            general_channel_event.reset();
            general_channel_context.reset();
        }
        kernel.Shutdown();"""
shutdown_before = """        debugger.reset();
        kernel.Shutdown();"""
ensure_after = """            general_channel_context.emplace(system, "GeneralChannel");
            general_channel_event.emplace(*general_channel_context);
            if (!general_channel.empty()) {
                general_channel_event->Signal(kernel);
            }
        }"""
ensure_before = """            general_channel_context.emplace(system, "GeneralChannel");
            general_channel_event.emplace(*general_channel_context);
        }"""
pop_after = """bool System::TryPopGeneralChannel(std::vector<u8>& out_data) {
    std::scoped_lock lk{impl->general_channel_mutex};
    if (impl->general_channel.empty()) {
        return false;
    }
    // A queue can survive a kernel restart while its old Event cannot.
    impl->EnsureGeneralChannelInitialized(*this);
    out_data = std::move(impl->general_channel.back());"""
pop_before = """bool System::TryPopGeneralChannel(std::vector<u8>& out_data) {
    std::scoped_lock lk{impl->general_channel_mutex};
    if (!impl->general_channel_event || impl->general_channel.empty()) {
        return false;
    }
    out_data = std::move(impl->general_channel.back());"""

def blob(text: str) -> str:
    raw = text.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()

for relative, predecessor in cores.items():
    source = (ROOT / relative).read_text()
    load = source.split("    SystemResultStatus Load(", 1)[1].split(
        "\n    void ShutdownMainProcess()", 1)[0]
    assert load.count("process.reset();") == 3, (relative, "three failed-load owners")
    assert load.count(loader_after) == 1, (relative, "loader reset")

    shutdown = source.split("    void ShutdownMainProcess() {", 1)[1].split(
        "\n    bool IsShuttingDown()", 1)[0]
    assert shutdown.count(shutdown_after) == 1, (relative, "kernel-owned event teardown")
    assert "general_channel.clear()" not in shutdown, relative
    assert shutdown.index("general_channel_event.reset();") < shutdown.index(
        "general_channel_context.reset();") < shutdown.index("kernel.Shutdown();")

    ensure = source.split("    void EnsureGeneralChannelInitialized(System& system) {", 1)[1].split(
        "\n    }\n};", 1)[0] + "\n    }"
    assert ensure.count(ensure_after.strip()) == 1, (relative, "recreate signal")
    assert ensure.index("general_channel_event.emplace") < ensure.index(
        "general_channel_event->Signal(kernel)")

    assert source.count(pop_after) == 1, (relative, "TryPop queue-first")
    pop = source.split("bool System::TryPopGeneralChannel", 1)[1].split(
        "\n}\n\nService::Event&", 1)[0]
    assert pop.index("general_channel.empty()") < pop.index(
        "EnsureGeneralChannelInitialized(*this)") < pop.index("general_channel.back()")

    predecessor_text = source
    for after, before, label in (
        (loader_after, loader_before, "failed-loader"),
        (shutdown_after, shutdown_before, "teardown"),
        (ensure_after, ensure_before, "recreate"),
        (pop_after, pop_before, "pop"),
    ):
        assert predecessor_text.count(after) == 1, (relative, label)
        predecessor_text = predecessor_text.replace(after, before, 1)
    assert blob(predecessor_text) == predecessor, (
        relative, blob(predecessor_text), predecessor)

print("SHUTDOWN_GENERAL_CHANNEL_1932_SOURCE_PASS core_variants=3 failed_load_resets=3"
      " event_before_kernel=1 queue_survives=1 restart_signal=1 predecessors_exact=3")
