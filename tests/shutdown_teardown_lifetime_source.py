#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact-source proof for Lichii #1952/#1957 shutdown lifetime fixes."""
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
    # The selected NX container loader adds one verified pre-kernel
    # platform check after this snapshot. Undo its exact seven source
    # lines in the provenance COPY, not in the shipping runtime.
    if current_digest == "df35fcd9db52452902f0cbd218c63dff2fd70cc0":
        nx_include = '#include "core/loader/nx.h"\n'
        nx_block = """        // NX carries the program platform in the verified catalog; select the
        // matching NS2 guest layout before allocating any process memory.
        if (Loader::AppLoader_NX::IdentifyType(file) == Loader::FileType::NX) {
            const auto nx_program = Loader::AppLoader_NX::ReadProgramPlatform(file, program_id);
            return nx_program && nx_program->platform == 2;
        }
"""
        assert source.count(nx_include) == source.count(nx_block) == 1
        source = source.replace(nx_include, "", 1).replace(nx_block, "", 1)
        raw_prev = source.encode()
        current_digest = hashlib.sha1(
            b"blob " + str(len(raw_prev)).encode() + b"\0" + raw_prev
        ).hexdigest()
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

def reverse_1932(source: str, relative: str) -> str:
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
    for after, before, label in (
        (loader_after, loader_before, "failed-loader"),
        (shutdown_after, shutdown_before, "general-channel teardown"),
        (ensure_after, ensure_before, "general-channel recreate"),
        (pop_after, pop_before, "general-channel pop"),
    ):
        assert source.count(after) == 1, (relative, label)
        source = source.replace(after, before, 1)
    return source

for relative, predecessor in cores.items():
    source = reverse_1932(remove_native_gpu_teardown_fix(
        (ROOT / relative).read_text(), relative), relative)
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
