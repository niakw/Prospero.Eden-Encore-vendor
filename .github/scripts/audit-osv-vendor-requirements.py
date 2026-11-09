#!/usr/bin/env python3
"""Read-only OSV inventory of selected vendored Python requirements.

Version matches do not establish PS5 runtime reachability or close GitHub alerts.
"""
from collections import defaultdict
from pathlib import Path
import json
import os
import re
import sys
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[2]
ENDPOINT = "https://api.osv.dev/v1/querybatch"
MANIFESTS = (
    "sources/eden-cpm/nlohmann_json/v3.12.0/docs/mkdocs/requirements.txt",
    "sources/eden-cpm/opus/a3f0ec02b3/dnn/torch/osce/requirements.txt",
    "sources/harfbuzz/.ci/requirements.txt",
    "sources/opengl-mesa-26-2/bin/ci/test/requirements.txt",
    "sources/opengl-opengnm/docs/requirements.txt",
    "sources/ps5-mesa/bin/ci/test/requirements.txt",
)
PIN = re.compile(r"^\s*([a-zA-Z0-9][\w.-]*)\s*==\s*([^\s;\\#]+)")


def package_pins():
    pins = defaultdict(list)
    nonexact = []
    for filename in MANIFESTS:
        path = ROOT / filename
        if not path.is_file():
            raise RuntimeError(f"Selected source manifest missing: {filename}")
        for line in path.read_text(encoding="utf-8").splitlines():
            match = PIN.match(line)
            if match:
                name = match.group(1).lower().replace("_", "-").replace(".", "-")
                version = match.group(2)
                if "*" in version or not re.fullmatch(r"[a-zA-Z0-9][\w.+!-]*", version):
                    nonexact.append((name, version, filename))
                else:
                    pins[(name, version)].append(filename)
    if not pins:
        raise RuntimeError("No pinned Python packages found")
    return pins, nonexact


def query_osv(pins):
    keys = sorted(pins)
    queries = [
        {"package": {"ecosystem": "PyPI", "name": name}, "version": version}
        for name, version in keys
    ]
    collected = {key: set() for key in keys}
    pending = [(i, query) for i, query in enumerate(queries)]
    for _ in range(10):
        if not pending:
            break
        next_pending = []
        for start in range(0, len(pending), 100):
            batch = pending[start:start + 100]
            payload = json.dumps({"queries": [query for _, query in batch]}).encode("utf-8")
            request = Request(
                ENDPOINT, data=payload, method="POST",
                headers={"Content-Type": "application/json", "User-Agent": "eden-vendor-audit/1"},
            )
            with urlopen(request, timeout=40) as response:
                if response.status != 200:
                    raise RuntimeError(f"OSV returned HTTP {response.status}")
                result = json.load(response)
            rows = result.get("results")
            if not isinstance(rows, list) or len(rows) != len(batch):
                raise RuntimeError("OSV returned incomplete or mismatched query results")
            for (index, _), row in zip(batch, rows):
                for vuln in row.get("vulns", []):
                    if vuln.get("id"):
                        collected[keys[index]].add(vuln["id"])
                if row.get("next_page_token"):
                    next_pending.append((
                        index, {**queries[index], "page_token": row["next_page_token"]}
                    ))
        pending = next_pending
    if pending:
        raise RuntimeError(f"OSV pagination unfinished for {len(pending)} packages")
    return collected


def main():
    pins, nonexact = package_pins()
    vulnerabilities = query_osv(pins)
    affected = [(name, version, sorted(ids), sorted(pins[(name, version)]))
                for (name, version), ids in vulnerabilities.items() if ids]
    affected.sort(key=lambda row: (-len(row[2]), row[0], row[1]))
    total_ids = len({advisory for _, _, ids, _ in affected for advisory in ids})
    print(
        f"VENDOR_OSV_INVENTORY_COMPLETE manifests={len(MANIFESTS)}"
        f" pinned_packages={len(pins)} affected_versions={len(affected)}"
        f" advisory_ids={total_ids} nonexact_constraints={len(nonexact)}"
        " runtime_reachability=unverified"
    )
    for name, version, filename in nonexact:
        print(f"VENDOR_OSV_NONEXACT {name}=={version} manifest={filename}")
    for name, version, ids, paths in affected:
        print(f"VENDOR_OSV_MATCH {name}=={version} advisories={','.join(ids)}")
        for path in paths:
            print(f"  manifest={path}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as output:
            output.write(
                "## Vendored Python dependency inventory (informational only)\n\n"
                f"Scanned {len(MANIFESTS)} manifests and {len(pins)} distinct pins; "
                f"{len(affected)} affected package versions, {total_ids} advisory IDs.\n\n"
                "These are **version matches**, not demonstrated PS5 runtime impact "
                "or GitHub Dependabot alert closures. No dependencies were installed.\n\n"
                "| Pinned package | Advisory IDs | Source manifest(s) |\n"
                "| --- | --- | --- |\n"
            )
            for name, version, ids, paths in affected:
                output.write(
                    f"| `{name}=={version}` | {', '.join(ids)} "
                    f"| {', '.join('`' + p + '`' for p in paths)} |\n"
                )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"VENDOR_OSV_INVENTORY_ERROR {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(2)
