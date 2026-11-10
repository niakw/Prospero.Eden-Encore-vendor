#!/usr/bin/env python3
"""Read-only inventory of open Code Scanning alerts; no false-positive dismissal."""
import collections
import json
import os
import sys
import urllib.error
import urllib.request

repo = os.environ.get("GITHUB_REPOSITORY", "niakw/Prospero.Eden-Encore-vendor")
token = os.environ.get("GITHUB_TOKEN", "")
summary = os.environ.get("GITHUB_STEP_SUMMARY")


def owner(path):
    if path.startswith(("encore-active/", ".github/", "tools/")):
        return "maintained_or_infrastructure"
    if path.startswith(("sources/", "objects/")):
        return "upstream_snapshot"
    return "other_or_unknown"


def main():
    if not token:
        raise RuntimeError("no GITHUB_TOKEN; zero findings cannot be inferred")
    alerts = []
    for page in range(1, 101):
        url = f"https://api.github.com/repos/{repo}/code-scanning/alerts?state=open&per_page=100&page={page}"
        req = urllib.request.Request(url, headers={
            "Authorization": "Bearer " + token,
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "vendor-code-scanning-read-only-inventory",
        })
        with urllib.request.urlopen(req, timeout=30) as response:
            batch = json.load(response)
        if not isinstance(batch, list):
            raise RuntimeError("unexpected GitHub Code Scanning API response")
        alerts.extend(batch)
        if len(batch) < 100:
            break
    else:
        raise RuntimeError("pagination limit exceeded, incomplete inventory")

    findings = []
    for alert in alerts:
        rule = alert.get("rule") or {}
        location = (alert.get("most_recent_instance") or {}).get("location") or {}
        path = location.get("path") or ""
        findings.append({
            "id": alert.get("number"),
            "url": alert.get("html_url"),
            "rule": rule.get("id") or "unknown",
            "severity": rule.get("security_severity_level") or rule.get("severity") or "unknown",
            "path": path,
            "line": location.get("start_line"),
            "location_group": owner(path),
            "runtime_reachable": "not_assessed",
        })
    groups = collections.Counter(f["location_group"] for f in findings)
    rules = collections.Counter((f["location_group"], f["rule"]) for f in findings)
    print("CODE_SCANNING_OPEN_COUNT", len(findings))
    for k, v in sorted(groups.items()):
        print("CODE_SCANNING_OWNER_GROUP", k, v)
    for row in findings:
        print("CODE_SCANNING_ALERT " + json.dumps(row, sort_keys=True))
    if summary:
        with open(summary, "a", encoding="utf-8") as out:
            out.write("## Open Code Scanning alerts (read-only)\n\n")
            out.write(f"**{len(findings)} open**, no dismissals or source changes; "
                      "path grouping does not prove runtime reachability.\n\n")
            out.write("| File owner group | Alerts |\n| --- | ---: |\n")
            for k, v in sorted(groups.items()):
                out.write(f"| {k} | {v} |\n")
            out.write("\n| Group | Rule | Count |\n| --- | --- | ---: |\n")
            for (group, rule), count in rules.most_common(30):
                out.write(f"| {group} | {rule} | {count} |\n")
            out.write("\n### Maintained code/infrastructure: prioritized alerts\n\n")
            out.write("| Alert | Rule | Path |\n| --- | --- | --- |\n")
            maintained = [f for f in findings if f["location_group"] == "maintained_or_infrastructure"]
            for f in maintained[:75]:
                out.write(f"| [#{f['id']}]({f['url']}) | {f['rule']} | {f['path']} |\n")
            out.write("\nFor all alert IDs, see machine-readable CODE_SCANNING_ALERT log lines.\n")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, urllib.error.URLError) as exc:
        print(f"CODE_SCANNING_INVENTORY_UNAVAILABLE: {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(2)
