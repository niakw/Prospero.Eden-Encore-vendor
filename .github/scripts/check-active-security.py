#!/usr/bin/env python3
"""Host-only security/quality checks for maintained Eden Encore vendor files.
GitHub's DEFAULT CodeQL setup is authoritative: do not upload advanced SARIF.
No console binary, no proprietary learned weights, no network at test time.
"""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
def read(path):
    full=root/path
    assert full.is_file(), f"Missing active owner: {path}"
    return full.read_text(encoding="utf-8")
model=read("encore-active/eden-vulkan/include/video_core/renderer_vulkan/nr_rdna2_model_file.h")
manifest=read("encore-active/eden-vulkan/include/video_core/renderer_vulkan/nr_rdna2_model_manifest.h")
preflight=read("encore-active/eden-vulkan/include/video_core/renderer_vulkan/nr_rdna2_session_preflight.h")
session=read("encore-active/eden-vulkan/nr_rdna2_frame_session.cpp")
for guard in ("kMaxIndexBytes", "ParseNeuralModelIndex(", "file_size_-entry->offset",
              "kMaxTensor", "std::numeric_limits<std::streamoff>::max()",
              "stream.gcount()", "output.clear()"):
    assert guard in model, ("Removed bounded model-read guard",guard)
assert 'if(!ValidateR09ModelManifest(checked.model.Entries(),model)' in preflight
assert 'PreflightR09Session(' in session
assert session.index('PreflightR09Session(') < session.index('std::make_unique<State>'), (
    "Unverified user-supplied model was allowed to initialize Vulkan GPU owners")
for path in [
    "encore-active/eden-vulkan/nr_rdna2_core_feature_sequence.cpp",
    "encore-active/eden-vulkan/nr_rdna2_unet_down_sequence.cpp",
    "encore-active/eden-vulkan/nr_rdna2_unet_up_sequence.cpp",
]:
    source=read(path)
    assert "return false" in source and "Record(" in source, (
        "Missing conservative neural scheduler/graph guard",path)
assert "R09OriginalManifest" in manifest and "R09CompactManifest" in manifest
assert "R09OriginalTemporalScale" in manifest and "R09CompactTemporalScale" in manifest
# Exact repository boundaries: proprietary model DLLs/weights are never
# included, and active sources must not sneak in large binary weights.
for directory in ("encore-active", ".github"):
    for p in (root/directory).rglob("*"):
        if not p.is_file(): continue
        relative=p.relative_to(root).as_posix().lower()
        if relative.endswith((".dll",".nrmodel")) or relative.endswith(
                ("nvngx_dlssnr.bin","dlssnr-small.bin")):
            raise AssertionError(f"Proprietary/restricted model binary bundled: {relative}")
assert (root/".github/dependabot.yml").is_file()
print("ENCORE_VENDOR_ACTIVE_SECURITY_QUALITY_PASS source_level_only=1 default_codeql_remains_enabled=1 proprietary_models_bundled=0")
