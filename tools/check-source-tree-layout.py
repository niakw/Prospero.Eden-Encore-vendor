#!/usr/bin/env python3
"""Pin PS5 third-party sources separately from maintained Encore overrides.

Host-only check: verifies exact upstream Git tree objects, model integrity,
license files and absence of nested submodules or old active-source aliases.
Does not claim device or PS5 build qualification.
"""
from pathlib import Path
import hashlib
import subprocess

root = Path(__file__).resolve().parents[1]
expected = {
    "ps5-mihawkgen": "aad67f4bd28c040db7aa0d78cfd3d0de85cfba7a",
    "ps5-fsr3-optical-flow": "85e34d638cd9e8b211e08fb19d1a57a588bb8d76",
    "libwebp": "3a7830b9713d9187cf1211862d4571ff58b0d507",
}
# The sole non-byte-for-byte upstream delta is a documented critical
# Pillow vulnerability remediation in MihawkGen's Python training-only list.
# All other upstream files, including models and licenses, remain original.
upstream_baseline = "a800f08eb13fd9b6aac84f1ec21f9ac917f57330"
relative_train = "sources/ps5-mihawkgen/train/requirements.txt"
history_changes = subprocess.check_output(
    ["git", "diff", "--name-only", upstream_baseline, "HEAD", "--",
     "sources/ps5-mihawkgen"], cwd=root, text=True).splitlines()
assert history_changes == [relative_train], history_changes
original_train = subprocess.check_output(
    ["git", "show", f"{upstream_baseline}:{relative_train}"], cwd=root, text=True)
assert original_train.count("pillow==12.3.0") == 1
assert (root / relative_train).read_text() == original_train.replace(
    "pillow==12.3.0", "pillow==13.0.0"
), "Unexpected MihawkGen upstream modification"

for name, sha in expected.items():
    path = f"sources/{name}"
    actual = subprocess.check_output(["git", "rev-parse", f"HEAD:{path}"],
                                     cwd=root, text=True).strip()
    assert actual == sha, (path, actual, sha)
    assert (root / path).is_dir(), (path, "not checked out")

for name in ("PS5_MihawkGen", "PS5_FSR3OpticalFlow", "libwebp"):
    assert not (root / "encore-active" / name).exists(), (
        "Third-party upstream incorrectly classified as Encore override", name)
assert not (root / ".gitmodules").exists(), "Nested Git submodules forbidden"
assert not subprocess.check_output(["git", "ls-files", "--stage"],
                                   cwd=root, text=True).startswith("160000 ")
assert not any(line.startswith("160000 ") for line in subprocess.check_output(
    ["git", "ls-files", "--stage"], cwd=root, text=True).splitlines())
for filename in (
    "sources/ps5-mihawkgen/LICENSE.md",
    "sources/ps5-mihawkgen/THIRD_PARTY_NOTICES.txt",
    "sources/ps5-fsr3-optical-flow/COPYING",
    "sources/ps5-fsr3-optical-flow/shaders/LICENSE.txt",
    "sources/libwebp/COPYING",
    "sources/libwebp/PATENTS",
):
    assert (root / filename).is_file(), ("Missing upstream license", filename)
model = root / "sources/ps5-mihawkgen/framegen/models/network.fgw"
assert hashlib.sha256(model.read_bytes()).hexdigest() == (
    "31ecc7d69af1e9defd62b9abad71bc3430cd8a0a4d2f4eafcabaafa48c34bf6b")
provenance = (root / "docs/PS5_INTERNAL_DEPENDENCY_ORIGINS_2026-11-10.md").read_text()
readme = (root / "README.md").read_text()
for name in expected:
    assert f"sources/{name}/" in provenance and f"sources/{name}/" in readme
print("VENDOR_SOURCE_LAYOUT_PASS upstream_sources=3 exact_git_trees=3 mihawk_training_security_patch=1 "
      "unchanged_models=1 original_licenses=1 no_nested_gitlinks=1")
