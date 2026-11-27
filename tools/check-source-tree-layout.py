#!/usr/bin/env python3
"""Single vendor source-layout contract.

sources/ is an immutable imported dependency layer. encore-active/ is the only
maintained override layer. Dependency roots use the same name in both trees;
an override uses the same logical relative path as its upstream owner when one
exists. This check deliberately has no rolling list of runtime commit SHAs.
"""
from pathlib import Path
import hashlib
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "sources"
ACTIVE = ROOT / "encore-active"

assert SOURCES.is_dir() and ACTIVE.is_dir()
sources_readme = (SOURCES / "README.md").read_text()
active_readme = (ACTIVE / "README.md").read_text()
assert "dépendances upstream figées" in sources_readme
assert "override pur Eden Encore" in active_readme
assert "même nom de dépendance, même chemin relatif" in active_readme

# Imported dependency roots are the namespace. Maintained roots must reuse it;
# old feature/family names are migration debt and may never be recreated.
def git_tree_dirs(path: str) -> set[str]:
    """Return tracked child directories independent of sparse/materialized checkout."""
    out = subprocess.check_output(
        ["git", "ls-tree", "-d", "--name-only", f"HEAD:{path}"],
        cwd=ROOT, text=True,
    )
    return {line.strip() for line in out.splitlines() if line.strip()}

source_roots = git_tree_dirs("sources")
active_roots = git_tree_dirs("encore-active")
migration_debt_roots = {
    # Temporary migration debt. New files are forbidden here; these roots
    # are removed progressively and checked below for already-retired areas.
    "eden-core", "eden-vulkan",
    "ns2-runtime", "ns2-helpers", "ns2-astc", "amd-fsr31",
}
unknown_active_roots = active_roots - source_roots - migration_debt_roots
assert not unknown_active_roots, (
    "unknown encore-active dependency roots", sorted(unknown_active_roots))

for retired in (
    "eden-gpu", "eden-sync", "eden-threading", "eden-shader", "ns2-a64", "eden-build", "eden-backports",
):
    assert not (ACTIVE / retired).exists(), ("retired active root reintroduced", retired)

# These mechanically canonical Eden subtrees have completed migration.
for retired_subtree in (
    "eden-core/src", "eden-core/include",
):
    assert not (ACTIVE / retired_subtree).exists(), (
        "retired active subtree reintroduced", retired_subtree)

# Security fixes are overrides too. The imported MihawkGen tree stays upstream.
upstream_train = (SOURCES / "ps5-mihawkgen/train/requirements.txt").read_text()
override_train = (ACTIVE / "ps5-mihawkgen/train/requirements.txt").read_text()
assert "pillow==12.3.0" in upstream_train and "pillow==13.0.0" not in upstream_train
assert override_train == upstream_train.replace("pillow==12.3.0", "pillow==13.0.0")

# Keep the three externally imported graphics dependencies pinned as dependency
# snapshots. These are dependency-tree identities, not runtime-owner allowlists.
expected_trees = {
    "ps5-mihawkgen": "e3b42f5b89775469bec04795b93f048b74aff207",
    "ps5-fsr3-optical-flow": "85e34d638cd9e8b211e08fb19d1a57a588bb8d76",
    "libwebp": "3a7830b9713d9187cf1211862d4571ff58b0d507",
}
for name, expected in expected_trees.items():
    actual = subprocess.check_output(
        ["git", "rev-parse", f"HEAD:sources/{name}"], cwd=ROOT, text=True
    ).strip()
    assert actual == expected, (name, actual, expected)

assert not (ROOT / ".gitmodules").exists(), "vendor must not contain nested submodules"
for filename in (
    "sources/ps5-mihawkgen/LICENSE.md",
    "sources/ps5-mihawkgen/THIRD_PARTY_NOTICES.txt",
    "sources/ps5-fsr3-optical-flow/COPYING",
    "sources/ps5-fsr3-optical-flow/shaders/LICENSE.txt",
    "sources/libwebp/COPYING",
    "sources/libwebp/PATENTS",
):
    assert (ROOT / filename).is_file(), filename

model = SOURCES / "ps5-mihawkgen/framegen/models/network.fgw"
assert hashlib.sha256(model.read_bytes()).hexdigest() == (
    "31ecc7d69af1e9defd62b9abad71bc3430cd8a0a4d2f4eafcabaafa48c34bf6b"
)

print("VENDOR_SOURCE_LAYOUT_PASS sources_immutable=1 active_override_only=1 "
      "canonical_dependency_names=1 retired_subtrees=3 mihawk_security_override=1")
