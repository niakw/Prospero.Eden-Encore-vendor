#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Require bounded pre-kernel and effective-loader generation receipts.

Source-only qualification; success here does NOT establish guest execution.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
core = (root / "encore-active/ns2-runtime/process/core/core.cpp").read_text()
loader = (root / "encore-active/ns2-runtime/metadata/core/loader/deconstructed_rom_directory.cpp").read_text()

start = core.index("[[nodiscard]] bool IsSwitch2LaunchProgram(")
end = core.index("\n} // namespace", start)
probe = core[start:end]

assert probe.count("EDEN_NS2_PREKERNEL_PROBE file=0 directory=0 extracted_npdm=0") == 1
assert probe.count("EDEN_NS2_PREKERNEL_PROBE file=1 directory={} extracted_npdm={}") == 1
assert probe.count("EDEN_NS2_PREKERNEL_NPDM status={} switch2={}") == 1
assert probe.count(
    "EDEN_NS2_PREKERNEL_PROBE file={} directory=1 extracted_npdm=1 direct=1"
) == 1
assert "if (direct_extracted_npdm)" in probe
assert probe.index("if (direct_extracted_npdm)") < probe.index("const auto check_program_nca")
assert 'OpenDirectory(filepath, FileSys::OpenMode::Read)' in core
assert 'exefs_dir->GetFile("main.npdm")' in core
assert probe.index('directory->GetFile("main.npdm")') < probe.index(
    "EDEN_NS2_PREKERNEL_PROBE file=1"
) < probe.index("if (extracted) return check_npdm(extracted);")
assert "metadata.IsSwitch2()" in probe
assert 'metadata.Load(npdm) == Loader::ResultStatus::Success &&' not in probe

receipt = "EDEN_EXEFS_GENERATION title={:016x} switch2={} extended_acid={}"
assert loader.count(receipt) == 1
reload = "const ResultStatus result2 = metadata.Reload(npdm);"
late_admission = "if (metadata.IsSwitch2() && !dir->GetFile(\"main\"))"
assert loader.index(reload) < loader.index(receipt) < loader.index(late_admission)
assert "int(metadata.IsSwitch2())" in loader
assert "int(metadata.UsesExtendedAcid())" in loader
assert "int(bool(dir->GetFile(\"main\")))" in loader
# Classification is still based on verified metadata, never the offline ID.
assert "metadata.GetTitleID(), int(metadata.IsSwitch2())" in loader

print("NS2_GENERATION_BOUNDARY_RECEIPT_SOURCE_PASS")
