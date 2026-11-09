#!/usr/bin/env python3
"""Inventory vendor-name references in maintained source comments only.

This is a read-only audit. Exact API names, driver-specific workarounds,
upstream attribution, copyright and SDK identifiers are not generic marketing
labels and cannot be automatically removed.
"""
from pathlib import Path
from collections import Counter
import re

root = Path(__file__).resolve().parents[2]
vendor_names = re.compile(r"\b(?:Nvidia|Sony|Nintendo|PlayStation|Microsoft|AMD)\b", re.I)
exts = {".cpp",".cc",".c",".h",".hpp",".py",".cmake",".sh"}
roots = ["eden-core","eden-vulkan","ns2-runtime","ns2-helpers","ns2-a64","ns2-astc","eden-gpu","eden-shader","eden-threading","eden-sync"]
counts = Counter()
matches = []
scanned = 0
for directory in roots:
    base = root / "encore-active" / directory
    if not base.is_dir(): continue
    for path in sorted(base.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in exts: continue
        scanned += 1
        for i,line in enumerate(path.read_text(encoding="utf-8",errors="replace").splitlines(),1):
            stripped=line.lstrip()
            if not (stripped.startswith(("//","# ","/*","* ","-- "))): continue
            terms = vendor_names.findall(line)
            if not terms: continue
            counts.update(x.casefold() for x in terms)
            matches.append((path.relative_to(root).as_posix(),i,stripped[:170]))
# The paths are intentionally source/owner evidence, not a blocker that could
# be bypassed by renaming technically required API symbols.
print("ENCORE_VENDOR_MAINTAINED_COMMENT_AUDIT",
      f"scanned_files={scanned} comment_name_references={len(matches)}",
      f"families={len(counts)}")
for path,i,line in matches[:80]:
    print(f"REVIEW {path}:{i}: {line}")
if len(matches) > 80:
    print(f"REVIEW_TRUNCATED additional={len(matches)-80}")
