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
assert 'game_root->GetSubdirectory("updates")' in overlay
assert 'candidate.directory->GetSubdirectory("exefs")' in overlay
assert 'candidate.directory->GetSubdirectory("romfs")' in overlay
assert 'std::from_chars(' in overlay
assert "std::size_t visited = 0" in overlay
assert "visited++ < 128" in overlay
assert "candidate.depth >= 5" in overlay
assert "truncated = true" in overlay
assert "if (!pending.empty() || truncated)" in overlay
assert "ambiguous = true" in overlay
assert "if (auto patch_exefs = newest->GetSubdirectory" in overlay
assert "if (auto patch_romfs = newest->GetSubdirectory" in overlay
assert overlay.index("if (auto patch_exefs") < overlay.index("if (auto patch_romfs")
assert 'std::vector<FileSys::VirtualDir> layers{std::move(patch_romfs)};' in overlay
assert "if (base_romfs) layers.push_back(base_romfs);" in overlay
assert 'layered->GetFileRelative("System/HeapSize")' in overlay
assert 'layered->GetSubdirectory("RSDB")' in overlay
assert "FileSys::CreateRomFS(std::move(layered), nullptr)" in overlay
assert "if (!romfs && base_romfs)" in overlay
assert "FileSys::CreateRomFS(std::move(base_romfs), nullptr)" in overlay
assert overlay.index('GetSubdirectory("romfs")') < overlay.index("CreateRomFS(")
assert "namespace std::filesystem" not in overlay
assert "std::ofstream" not in overlay
assert "std::filesystem::copy" not in overlay
assert "is_loaded = true;" in owner[entry:end]
assert 'RomFSFactory>(*this, system.GetContentProvider()' in owner[entry:end]
assert 'if (romfs == nullptr && standalone && file)' in owner[end:]

print("NS2_EXTRACTED_ROMFS_SOURCE_CONTRACT_PASS "
      "update_only=1 base_fallback=1 bounded=1 nup_isolation=1 ps5_runtime=unverified")
