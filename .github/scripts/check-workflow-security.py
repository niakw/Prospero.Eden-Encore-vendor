#!/usr/bin/env python3
"""Audit maintained GitHub Actions workflows without executing their contents.

This is a preventive host guard, not a substitute for CodeQL/Dependabot alerts.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
USES = re.compile(r"(?m)^[ \t]*(?:-[ \t]+)?uses:[ \t]*([^\s#]+)")
PIN = re.compile(r"^[^\s@]+@[a-f0-9]{40}$")
READ_ONLY = re.compile(r"(?m)^[ \t]+[a-z][a-z-]*:[ \t]*write(?:[ \t]*#.*)?$")
DANGEROUS_EVENT = re.compile(r"(?m)^[ \t]*(?:pull_request_target|workflow_run):[ \t]*(?:#.*)?$")
PERSIST_FALSE = re.compile(r"(?m)^[ \t]+persist-credentials:[ \t]*false(?:[ \t]*#.*)?$")


def check_workflow(path: Path) -> int:
    content = path.read_text(encoding="utf-8")
    assert re.search(r"(?m)^permissions:[ \t]*(?:#.*)?$", content), (
        f"{path}: missing explicit top-level GITHUB_TOKEN permissions"
    )
    assert not READ_ONLY.search(content), f"{path}: write-capable token scope requires separate review"
    assert not DANGEROUS_EVENT.search(content), f"{path}: privileged event requires separate review"
    refs = USES.findall(content)
    checkouts = 0
    for ref in refs:
        if ref.startswith("./"):
            continue
        assert PIN.fullmatch(ref), f"{path}: unpinned or dynamic action: {ref}"
        if ref.startswith("actions/checkout@"):
            checkouts += 1
    if checkouts:
        assert len(PERSIST_FALSE.findall(content)) >= checkouts, (
            f"{path}: every checkout must disable persisted GitHub credentials"
        )
    return len(refs)


def main() -> None:
    paths = sorted((*WORKFLOWS.glob("*.yml"), *WORKFLOWS.glob("*.yaml")))
    assert paths, "No first-party workflows to audit"
    uses = sum(check_workflow(p) for p in paths)
    print(
        "ENCORE_VENDOR_WORKFLOW_SECURITY_PASS"
        f" workflows={len(paths)} uses={uses} permissions=read_only"
        " external_actions=sha_pinned checkout_credentials=disabled"
    )


if __name__ == "__main__":
    main()
