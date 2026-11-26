#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""NS2 NUP/ExeFS post-load tracing and release termination evidence, source gate."""
from pathlib import Path
R=Path(__file__).resolve().parents[1]
core=(R/"encore-active/ns2-runtime/process/core/core.cpp").read_text()
app=(R/"encore-active/ns2-runtime/am/core/hle/service/am/service/application_functions.cpp").read_text()
exe=(R/"encore-active/ns2-runtime/metadata/core/loader/deconstructed_rom_directory.cpp").read_text()
os=(R/"encore-active/ns2-runtime/process/core/hle/service/os/process.cpp").read_text()
block=core.split("        // Create the application process",1)[1].split(
    '        if (app_loader->ReadProgramId(',1)[0]
for tok in ("if (load_result != Loader::ResultStatus::Success)",
            "if (!app_loader)", "is_ns2_program && process",
            "process->GetProcessId() != 0",
            "!Eden::NS2::SvcTrace::IsActive(process->GetProcessId())",
            "Eden::NS2::SvcTrace::Register(process->GetProcessId());",
            "EDEN_NS2_PROCESS_TRACE pid=%llu classified=%u active=%u"):
    assert tok in block,tok
assert block.index("if (load_result != Loader::ResultStatus::Success)") < block.index(
    "if (!app_loader)") < block.index("Eden::NS2::SvcTrace::Register(")
assert block.index("if (!app_loader)") < block.index("EDEN_NS2_PROCESS_TRACE")
assert 'Eden::NS2::SvcTrace::Register(process.GetProcessId());' in exe
assert "Eden::NS2::SvcTrace::Unregister(pid);" in os
terminate=app.split("Result IApplicationFunctions::SetTerminateResult(",1)[1].split(
    "\n}\n",1)[0]
for tok in ("EDEN_APP_TERMINATE_RESULT title=%016llX",
            "terminate_reports.fetch_add(1, std::memory_order_relaxed) < 32",
            "terminate_result.GetInnerValue()",
            "m_applet->terminate_result = terminate_result;"):
    assert tok in terminate,tok
assert terminate.index("EDEN_APP_TERMINATE_RESULT") < terminate.index(
    "m_applet->terminate_result = terminate_result;")
print("NS2_CONTAINER_TRACE_AND_TERMINATE_RESULT_SOURCE_PASS postload=1 no_failed_load=1 exefs_idempotent=1 bounded=32")
