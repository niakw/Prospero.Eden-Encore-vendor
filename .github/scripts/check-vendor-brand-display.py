#!/usr/bin/env python3
"""Read-only display/comment inventory of active authored vendor sources.

Do not modify upstream snapshots, attribution, third-party license headers,
SDK identifiers, ABI names or technical facts merely to remove a trademark.
This scan is intentionally separate from the native PS5 build and P1/P2 gates.
"""
from __future__ import annotations

from collections import Counter
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
ACTIVE = ROOT / "encore-active"
BRAND = re.compile(r"\b(NVIDIA|Sony|Nintendo|PlayStation|Microsoft|AMD)\b",
                   re.IGNORECASE)
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".c", ".inc", ".cmake",
                   ".py", ".sh", ".glsl", ".md", ".txt", ".yml", ".yaml"}
THIRD_PARTY_DIRS = {"amd-fsr31", "ps5-mesa", "compiler-rt", "eden-backports"}
counts: Counter[str] = Counter()
third_party: Counter[str] = Counter()
display: list[tuple[str, int, str]] = []
lines: list[tuple[str, int, str]] = []
scanned = 0

for path in sorted(ACTIVE.rglob("*")):
    if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
        continue
    rel = path.relative_to(ACTIVE)
    retained = rel.parts[0] in THIRD_PARTY_DIRS
    scanned += 1
    try:
        content = path.read_text(encoding="utf-8")
    except (UnicodeError, OSError):
        continue
    for number, line in enumerate(content.splitlines(), 1):
        match = BRAND.search(line)
        if match is None:
            continue
        # Skip ordinary ABI usages (e.g., namespace Service::Nvidia), and
        # only inspect human-authored prose/comment segments.
        stripped = line.lstrip()
        comment = (stripped.startswith(("//", "/*", "* ", "# ", "<!--"))
                   or ("//" in line and BRAND.search(line.split("//", 1)[1]) is not None))
        if not comment:
            continue
        (third_party if retained else counts)[match.group(1).lower()] += 1
        if not retained:
            lines.append((rel.as_posix(), number, line.strip()[:160]))

workflow = ROOT / ".github/workflows/security-quality.yml"
for number, line in enumerate(workflow.read_text(encoding="utf-8").splitlines(), 1):
    if re.match(r"\s*(name|description):", line):
        match = BRAND.search(line)
        if match:
            display.append((str(workflow.relative_to(ROOT)), number, line.strip()))

print("VENDOR_COMMENT_DISPLAY_AUDIT",
      f"source_files_scanned={scanned}",
      f"project_owned_comment_mentions={sum(counts.values())}",
      f"upstream_retained_comment_mentions={sum(third_party.values())}",
      f"workflow_display_mentions={len(display)}")
print("VENDOR_OWNED_BRAND_COUNTS", dict(sorted(counts.items())))
print("VENDOR_UPSTREAM_BRAND_COUNTS", dict(sorted(third_party.items())))
for item in lines[:120]:
    print("VENDOR_OWNED_REVIEW", f"{item[0]}:{item[1]}", item[2])
for item in display:
    print("VENDOR_WORKFLOW_DISPLAY_REVIEW", f"{item[0]}:{item[1]}", item[2])
if len(lines) > 120:
    print("VENDOR_OWNED_TRUNCATED_REVIEW", len(lines) - 120)
assert scanned > 500, "vendor active checkout incomplete"
print("VENDOR_BRAND_AUDIT_SCOPE_PASS no_native_build=1 no_source_changes=1")
