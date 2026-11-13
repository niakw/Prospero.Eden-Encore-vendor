#!/usr/bin/env python3
"""Source contract only: PS5-selected loose ExeFS must expose update-only RomFS.

Run from a later host-only CI step once both game-generation P0 fixes are
ready. This checks selected source ownership and ordering, NOT game boot,
filesystem permissions, memory correctness or a PS5 build.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
owner = (root / "encore-active/ns2-runtime/metadata/core/loader/"
              "deconstructed_rom_directory.cpp").read_text()

def require(fragment: str) -> int:
    assert owner.count(fragment) == 1, (fragment, owner.count(fragment))
    return owner.index(fragment)

start = require("    // Loose ExeFS content can have most of its RomFS")
entry = require("    // Read meta to determine title ID")
end = require("ResultStatus AppLoader_DeconstructedRomDirectory::ReadRomFS(")
assert start < entry < end

overlay = owner[start:entry]
assert "if (standalone && file)" in overlay  # do not change NUP adapter
assert 'GetParentDirectory()' in overlay
assert 'parent->GetName() == "roms"' in overlay
assert 'bool within_library_title = false;' in overlay
assert 'within_library_title = true;' in overlay
assert 'if (const auto updates = within_library_title && !self_contained_update ?' in overlay
assert 'game_root->GetSubdirectory("updates")' in overlay
assert 'candidate.directory->GetSubdirectory("exefs")' in overlay
assert 'candidate.directory->GetSubdirectory("romfs")' in overlay
assert 'std::from_chars(' in overlay
assert "std::size_t visited = 0" in overlay
assert "visited++ < 128" in overlay
assert "candidate.depth >= 5" in overlay
assert "truncated = true" in overlay
assert "if (!pending.empty() || truncated)" in overlay
assert "bool launched_from_update = false;" in overlay
# An explicitly selected update version must never be replaced by an
# unrelated numerically newer installed update found in the scan.
assert 'newest = content_root;' in overlay
assert 'source=selected_version version={}' in overlay
assert 'pending.push_back({updates, std::nullopt, 0});' in overlay
assert overlay.index('newest = content_root;') < overlay.index(
    'while (!pending.empty() && visited++ < 128)')
assert 'cursor->GetName() == "updates"' in overlay
assert 'const auto nested_exefs = base_dir ? base_dir->GetSubdirectory("exefs")' in overlay
assert 'const auto root_exefs = game_root->GetSubdirectory("exefs");' in overlay
assert 'exefs->GetFile("main")' in overlay
assert 'exefs->GetFile("main.npdm")' in overlay
assert 'bootable_exefs(nested_exefs) ? nested_exefs :' in overlay
assert 'bootable_exefs(root_exefs) ? root_exefs :' in overlay
assert 'dir = base_exefs;' in overlay
assert 'else if (launched_from_update)' in overlay
# Eglantine #164/#431: a full selected update must boot with its OWN
# ExeFS + RomFS even if there is no base ExeFS. An incomplete update
# must still fail closed rather than borrow an unrelated version.
assert 'bool self_contained_update = false;' in overlay
assert 'const auto own_romfs = content_root->GetSubdirectory("romfs");' in overlay
assert 'FileSys::VirtualFile own_romfs_image;' in overlay
assert 'entry->GetExtension() == "romfs"' in overlay
assert 'if (!bootable_exefs(dir) || (!own_romfs && !own_romfs_image))' in overlay
assert 'status=base_missing_update_incomplete' in overlay
assert 'return {ResultStatus::ErrorMissingNPDM, {}};' in overlay
assert 'romfs = FileSys::CreateRomFS(own_romfs, nullptr);' in overlay
assert 'romfs = own_romfs_image;' in overlay
assert 'romfs_layout=directory' in overlay
assert 'romfs_layout=file rsdb_probe=not_applicable' in overlay
assert 'self_contained_update = true;' in overlay
assert 'source=selected_update_only' in overlay
assert 'within_library_title && !self_contained_update ?' in overlay
assert overlay.index('self_contained_update = true;') < overlay.index(
    'if (const auto updates = within_library_title && !self_contained_update ?')
assert 'base_dir ? base_dir->GetSubdirectory("romfs")' in overlay
assert 'if (!base_romfs && !launched_from_update)' in overlay
assert overlay.index("dir = base_exefs;") < overlay.index(
    'if (const auto updates = within_library_title && !self_contained_update ?')
assert "ambiguous = true" in overlay
assert "if (auto patch_exefs = newest->GetSubdirectory" in overlay
assert "if (auto patch_romfs = newest->GetSubdirectory" in overlay
assert overlay.index("if (auto patch_exefs") < overlay.index("if (auto patch_romfs")
assert 'std::vector<FileSys::VirtualDir> layers{std::move(patch_romfs)};' in overlay
assert "if (base_romfs) layers.push_back(base_romfs);" in overlay
# Eglantine #350 revised its report: System/HeapSize is not a valid
# mandatory resource for Bananza. Do not treat its absence as broken RomFS.
assert 'System/HeapSize' not in overlay
assert 'heap_file=' not in overlay
assert 'rsdb_dir={} probe=optional' in overlay
assert 'layered->GetSubdirectory("RSDB")' in overlay
assert 'own_romfs->GetSubdirectory("RSDB")' in overlay
assert "FileSys::CreateRomFS(std::move(layered), nullptr)" in overlay
assert "if (!romfs && base_romfs)" in overlay
assert "FileSys::CreateRomFS(std::move(base_romfs), nullptr)" in overlay
assert overlay.index('GetSubdirectory("romfs")') < overlay.index("CreateRomFS(")
assert "namespace std::filesystem" not in overlay
assert "std::ofstream" not in overlay
assert "std::filesystem::copy" not in overlay
assert "is_loaded = true;" in owner[entry:end]
assert 'RomFSFactory>(*this, system.GetContentProvider()' in owner[entry:end]
# The signal identifies the selected effective NSO/NPDM without pretending
# that identical Title IDs or size prove version compatibility.
identity = owner[entry:end]
assert 'EDEN_NS2_EXEFS_IDENTITY title={:016x} main_bytes={}' in identity
assert 'npdm_bytes={} romfs_staged={} identity=unverified' in identity
assert 'metadata.GetTitleID(), effective_main->GetSize(), npdm->GetSize()' in identity
assert identity.index('metadata.Reload(npdm)') < identity.index(
    'EDEN_NS2_EXEFS_IDENTITY') < identity.index('PreflightNso(')
assert identity.count('EDEN_NS2_EXEFS_IDENTITY') == 1
assert 'if (metadata.IsSwitch2()) {' in identity
assert 'ComputeSha256' not in identity and 'getenv(' not in identity

assert 'if (romfs == nullptr && standalone && file)' in owner[end:]

print("NS2_EXTRACTED_ROMFS_SOURCE_CONTRACT_PASS "
      "update_only=1 self_contained_update=1 image_or_directory=1 "
      "mixed_versions_rejected=1 source_identity_log=1 optional_rsdb_probe=1 "
      "base_fallback=1 bounded=1 nup_isolation=1 ps5_runtime=unverified")
