#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact-source proof for Lichii #1932 kernel-object shutdown lifetime."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]

# The PS5 Vulkan GPU worker must retire before CloseServices can close NVDRV
# address spaces. The historical #1932/#1952 provenance receipts predate
# this independent GPU/GMMU lifetime fix: undo only its exact source delta
# before comparing the original predecessor SHA.
def remove_native_gpu_teardown_fix(source: str, relative: str) -> str:
    if relative != "encore-active/ns2-runtime/process/core/core.cpp":
        return source
    # NUP pre-kernel selection is a verified post-#1932/#1952 runtime fix.
    # Undo its exact bytes on the audit copy, not the active C++ source.
    # The same source-selected NUP ExeFS view is needed before kernel init.
    raw_current = source.encode()
    current_digest = hashlib.sha1(
        b"blob " + str(len(raw_current)).encode() + b"\0" + raw_current).hexdigest()
    assert current_digest == "a2721a630b8b32222ff586ea2db19ee726d4d5b9", current_digest
    new_exefs = """            const auto exefs = mount
                ? Eden::NupV1::Native::ContentSet(mount).ExeFS()
                : FileSys::VirtualDir{};"""
    old_exefs = """            const auto exefs = mount ? mount->Effective(Eden::NupV1::ContentView::ExeFS)
                                     : FileSys::VirtualDir{};"""
    assert source.count(new_exefs) == 1
    source = source.replace(new_exefs, old_exefs, 1)
    assert source.count('#include "nup_v1_native_content.h"\n') == 1
    source = source.replace('#include "nup_v1_native_content.h"\n', "", 1)
    raw = source.encode()
    current_sha = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
    assert current_sha == "f37b975a0e251aa3f027b261f61d02fbf8506912", current_sha
    assert source.count('#include "nup_v1_native_vfs.h"\n') == 1
    source = source.replace('#include "nup_v1_native_vfs.h"\n', "", 1)
    nup_block = "        // NUP is a first-class NS1/NS2 container, but IdentifyFile() in the\n        // pinned vendor loader does not know it. The NUP bridge is selected\n        // by the native CMake graph and validates the same effective ExeFS.\n        // Probe its bounded main.npdm BEFORE allocating guest kernel memory,\n        // rather than silently using the smaller NS1 layout for an NS2 title.\n        if (Eden::NupV1::Native::HasNupV1Header(file)) {\n            const auto mount = Eden::NupV1::Native::Mount::Open(file);\n            const auto exefs = mount ? mount->Effective(Eden::NupV1::ContentView::ExeFS)\n                                     : FileSys::VirtualDir{};\n            const auto npdm = exefs ? exefs->GetFile(\"main.npdm\") : nullptr;\n            constexpr std::size_t MaxNupNpdmBytes = 4U * 1024U * 1024U;\n            return npdm && npdm->GetSize() > 0 &&\n                   npdm->GetSize() <= MaxNupNpdmBytes && check_npdm(npdm);\n        }\n"
    assert source.count(nup_block) == 1
    source = source.replace(nup_block, "", 1)
    old_raw = source.encode()
    old_sha = hashlib.sha1(b"blob " + str(len(old_raw)).encode() + b"\0" + old_raw).hexdigest()
    assert old_sha == "0110907a727b5d0ed15286298f851b59b30f1eb6", old_sha
    # The release PS5 Core::System must retire NVDRV-owned AS/pins and
    # KProcess references before ShutdownCores ends the guest process.
    # Reconstruct the qualified predecessor only for historical SHA checks.
    nvdrv_current = "        kernel.CloseServices();\n        ::Eden::ReportStep(\"shutdown\", \"Completed kernel.CloseServices()\");\n        // NVDRV's nvhost_as_gpu destructor can unpin NVMap pages and release\n        // its retained KProcess reference. Release those service owners while\n        // the process and scheduler are still alive; ShutdownCores terminates\n        // the guest processes and must come AFTER this cleanup. The GPU worker\n        // was joined above, so no GPU task can reuse the retired mappings.\n        services.reset();\n        ::Eden::ReportStep(\"shutdown\", \"Completed services.reset()\");\n        service_manager.reset();\n        kernel.ShutdownCores();\n        ::Eden::ReportStep(\"shutdown\", \"Completed kernel.ShutdownCores()\");"
    nvdrv_predecessor = "        kernel.CloseServices();\n        ::Eden::ReportStep(\"shutdown\", \"Completed kernel.CloseServices()\");\n        kernel.ShutdownCores();\n        ::Eden::ReportStep(\"shutdown\", \"Completed kernel.ShutdownCores()\");\n        services.reset();\n        ::Eden::ReportStep(\"shutdown\", \"Completed services.reset()\");\n        service_manager.reset();"
    assert source.count(nvdrv_current) == 1, (relative, "NVDRV before KProcess shutdown")
    shutdown = source.split("    void ShutdownMainProcess() {", 1)[1].split(
        "\n    bool IsShuttingDown()", 1)[0]
    assert shutdown.index("gpu_core.reset();") < shutdown.index("kernel.CloseServices();") < (
        shutdown.index("services.reset();")) < shutdown.index("service_manager.reset();") < (
        shutdown.index("kernel.ShutdownCores();"))
    source = source.replace(nvdrv_current, nvdrv_predecessor, 1)
    early = '''        ::Eden::ReportStep("shutdown", "Completed kernel.SuspendEmulation(true)");
        // GPU::Impl owns the Vulkan worker, scheduler, channel states and
        // renderer; native NVDRV address spaces own the GMMU/NvMap backing
        // those channel states reference. Once guest CPU cores are suspended,
        // join and destroy the GPU BEFORE CloseServices can release the
        // channel's address space. The old order could let the GPU worker or
        // renderer touch already-unpinned guest memory during exit/relaunch.
        // Keep Host1x and the NVDRV containers alive throughout GPU teardown.
        gpu_core.reset();
        ::Eden::ReportStep("shutdown", "Completed gpu_core.reset()");
        kernel.CloseServices();'''
    legacy = '''        ::Eden::ReportStep("shutdown", "Completed kernel.SuspendEmulation(true)");
        kernel.CloseServices();'''
    late = '''        app_loader.reset();
        audio_core.reset();
        host1x_core.reset();'''
    old_late = '''        app_loader.reset();
        audio_core.reset();
        gpu_core.reset();
        ::Eden::ReportStep("shutdown", "Completed gpu_core.reset()");
        host1x_core.reset();'''
    assert source.count(early) == 1 and source.count(late) == 1, relative
    shutdown = source.split("    void ShutdownMainProcess() {", 1)[1].split(
        "\n    bool IsShuttingDown()", 1)[0]
    assert shutdown.index("kernel.SuspendEmulation(true);") < shutdown.index(
        "gpu_core.reset();") < shutdown.index("kernel.CloseServices();")
    assert shutdown.index("gpu_core.reset();") < shutdown.index("services.reset();")
    return source.replace(early, legacy, 1).replace(late, old_late, 1)
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
    source = remove_native_gpu_teardown_fix((ROOT / relative).read_text(), relative)
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
