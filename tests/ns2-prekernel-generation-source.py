#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""NS2 pre-kernel generation selection must be independent from later NPDM validity.

The early probe chooses only the guest generation/memory layout. The actual
loader remains authoritative for full ACI/FAC/KAC validation.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

core = (ROOT / "encore-active/ns2-runtime/process/core/core.cpp").read_text()
metadata = (ROOT / "encore-active/ns2-runtime/metadata/core/file_sys/program_metadata.cpp").read_text()
loader = (ROOT / "encore-active/ns2-runtime/metadata/core/loader/deconstructed_rom_directory.cpp").read_text()

helper_begin = core.index("[[nodiscard]] bool IsSwitch2LaunchProgram(")
helper_end = core.index("\n} // namespace", helper_begin)
helper = core[helper_begin:helper_end]

probe = """const auto status = metadata.Load(npdm);"""
switch2 = """const bool switch2 = metadata.IsSwitch2();"""
diagnostic = """EDEN_NS2_PREKERNEL_NPDM status={} switch2={}"""

assert probe in helper
assert switch2 in helper
assert diagnostic in helper
assert helper.index(probe) < helper.index(switch2) < helper.index("return switch2;")
assert "metadata.Load(npdm) == Loader::ResultStatus::Success &&" not in helper

load_begin = metadata.index("Loader::ResultStatus ProgramMetadata::Load(VirtualFile file)")
load_end = metadata.index("Loader::ResultStatus ProgramMetadata::Reload", load_begin)
load = metadata[load_begin:load_end]

reset = "is_switch2 = false;"
acid = "is_switch2 = acid_location->extended;"
aci_validation = "if (aci_offset < sizeof(Header)"
fac_validation = "if (acid_header.fac_size < fac_read_size"
kac_validation = "if (aci_header.kac_size % sizeof(u32) != 0"

assert reset in load
assert acid in load
for later in (aci_validation, fac_validation, kac_validation):
    assert later in load
    assert load.index(acid) < load.index(later)

# The real application loader must still perform complete metadata admission.
full_load = "const ResultStatus result = metadata.Load(npdm);"
full_reload = "const ResultStatus result2 = metadata.Reload(npdm);"
assert full_load in loader and full_reload in loader
assert loader.index(full_load) < loader.index(
    "if (result != ResultStatus::Success)")
assert loader.index(full_reload) < loader.index(
    "if (result2 != ResultStatus::Success)")
assert "if (metadata.IsSwitch2() && !dir->GetFile(\"main\"))" in loader

print("NS2_PREKERNEL_GENERATION_SOURCE_PASS")
