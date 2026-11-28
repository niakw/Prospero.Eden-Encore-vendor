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

unknown_active_roots = active_roots - source_roots
assert not unknown_active_roots, (
    "unknown encore-active dependency roots", sorted(unknown_active_roots)
)

retired_roots = (
    "eden-gpu", "eden-sync", "eden-threading", "eden-shader", "eden-core",
    "eden-vulkan", "ns2-runtime", "amd-fsr31", "eden-header-hygiene",
    "eden-surface-lifecycle", "ns2-helpers",
    "ns2-a64", "ns2-astc", "eden-build", "eden-backports",
)
for retired in retired_roots:
    assert retired not in active_roots, ("retired active root reintroduced", retired)

active_files = git_tree_files("encore-active")
for retired_prefix in ("eden-core/src/", "eden-core/include/"):
    assert not any(path.startswith(retired_prefix) for path in active_files), (
        "retired active subtree reintroduced", retired_prefix
    )

# Workflows, host tests and maintenance tools are source consumers too. Scan
# them as a single set so stale paths cannot survive merely because the old
# directory itself has already been deleted.
consumer_paths = {
    *(ROOT/".github/workflows").glob("*.yml"),
    *(ROOT/".github/workflows").glob("*.yaml"),
    *(ROOT/".github/scripts").rglob("*.py"),
    *(ROOT/"tests").rglob("*.py"),
    *(ROOT/"tools").rglob("*.py"),
    *(ROOT/"tools").rglob("*.sh"),
}
consumer_paths.discard(Path(__file__).resolve())
legacy_consumer_refs = []

def negative_witness(line: str) -> bool:
    stripped = line.strip()
    return (
        " not in " in line
        or stripped.startswith("assert not ")
        or ("retired" in stripped and "assert" in stripped)
        or "forbidden" in stripped
    )

for path in sorted(consumer_paths):
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    for retired in retired_roots:
        for forbidden in (f"encore-active/{retired}/",):
            if forbidden in text:
                legacy_consumer_refs.append((str(path.relative_to(ROOT)), forbidden))
        for line_no, line in enumerate(lines, 1):
            if negative_witness(line):
                continue
            for forbidden in (f'"{retired}/', f"'{retired}/"):
                if forbidden in line:
                    legacy_consumer_refs.append(
                        (f"{path.relative_to(ROOT)}:{line_no}", forbidden)
                    )
assert not legacy_consumer_refs, (
    "active vendor consumer references retired Encore root",
    legacy_consumer_refs,
)

print(
    "VENDOR_SOURCE_LAYOUT_PASS",
    "sources=frozen_dependency_namespace",
    "active=only_maintained_override_namespace",
    f"source_roots={len(source_roots)}",
    f"active_roots={len(active_roots)}",
    "migration_debt_roots=0",
    f"active_consumers={len(consumer_paths)}",
)
