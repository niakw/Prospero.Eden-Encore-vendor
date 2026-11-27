#!/usr/bin/env python3
"""Unique vendor source-layout contract.

This check enforces ownership only:
  * sources/ is the imported dependency namespace;
  * encore-active/ is the maintained override namespace;
  * dependency roots and upstream-relative paths stay canonical;
  * retired functional roots cannot be recreated.

It deliberately contains no rolling commit-SHA, tree-SHA or blob-hash allowlist.
Dependency updates are explicit manual vendor updates, not CI receipt churn.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "sources"
ACTIVE = ROOT / "encore-active"

assert SOURCES.is_dir() and ACTIVE.is_dir()

sources_readme = (SOURCES / "README.md").read_text(encoding="utf-8")
active_readme = (ACTIVE / "README.md").read_text(encoding="utf-8")
for marker in ("vendor upstream pur et figé", "même chemin relatif"):
    assert marker in sources_readme, ("sources contract drift", marker)
for marker in ("arbre de code maintenu", "exactement le même chemin relatif", "un seul owner actif"):
    assert marker in active_readme, ("encore-active contract drift", marker)

def git_tree_dirs(path: str) -> set[str]:
    out = subprocess.check_output(
        ["git", "ls-tree", "-d", "--name-only", f"HEAD:{path}"],
        cwd=ROOT, text=True,
    )
    return {line.strip() for line in out.splitlines() if line.strip()}

def git_tree_files(path: str) -> set[str]:
    out = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", f"HEAD:{path}"],
        cwd=ROOT, text=True,
    )
    return {line.strip() for line in out.splitlines() if line.strip()}

source_roots = git_tree_dirs("sources")
active_roots = git_tree_dirs("encore-active")

migration_debt_roots = {
    "eden-core", "eden-vulkan", "ns2-runtime",
    "ns2-astc", "amd-fsr31",
}

unknown_active_roots = active_roots - source_roots - migration_debt_roots
assert not unknown_active_roots, (
    "unknown encore-active dependency roots", sorted(unknown_active_roots)
)

for retired in (
    "eden-gpu", "eden-sync", "eden-threading", "eden-shader",
    "ns2-a64", "eden-build", "eden-backports",
):
    assert retired not in active_roots, ("retired active root reintroduced", retired)

active_files = git_tree_files("encore-active")
for retired_prefix in ("eden-core/src/", "eden-core/include/"):
    assert not any(path.startswith(retired_prefix) for path in active_files), (
        "retired active subtree reintroduced", retired_prefix
    )

print(
    "VENDOR_SOURCE_LAYOUT_PASS",
    "sources=frozen_dependency_namespace",
    "active=only_maintained_override_namespace",
    f"source_roots={len(source_roots)}",
    f"active_roots={len(active_roots)}",
    f"migration_debt_roots={len(active_roots & migration_debt_roots)}",
)
