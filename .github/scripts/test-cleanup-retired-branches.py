#!/usr/bin/env python3
"""Run the exact maintenance script against a fake API. No network or real refs."""
import ast
import contextlib
import io
import json
import os
from pathlib import Path
import textwrap
from unittest.mock import patch
from urllib.parse import parse_qs, unquote, urlsplit

root = Path(__file__).resolve().parents[2]
workflow = (root / ".github/workflows/cleanup-merged-pr-branches.yml").read_text()
marker = "          python3 - <<'PY'\n"
assert workflow.count(marker) == 1
program = textwrap.dedent(workflow.split(marker, 1)[1].split("\n          PY\n", 1)[0])
code = compile(program, "cleanup-merged-pr-branches.yml", "exec")
nodes = [n for n in ast.parse(program).body if isinstance(n, ast.Assign)
         and any(isinstance(t, ast.Name) and t.id == "approved_retired" for t in n.targets)]
assert len(nodes) == 1
approved = ast.literal_eval(nodes[0].value)
assert approved and all(len(sha) == 40 for sha in approved.values())
name, sha = next(iter(approved.items()))
repo = "fixture/repo"
main = "f" * 40

def exercise(mode):
    snapshot = [{"name": "main", "commit": {"sha": main}, "protected": True},
                {"name": name, "commit": {"sha": sha}, "protected": mode == "protected"},
                {"name": "fixture/unknown", "commit": {"sha": "a"*40}, "protected": False}]
    if mode == "stale":
        snapshot[1]["commit"]["sha"] = "b"*40
    pr = {"number": 123, "head": {"ref": name, "repo": {"full_name": repo},
                                  "sha": sha}, "merged_at": None}
    deleted, queries = [], []
    # A PR can become open after the initial listing.
    def fake_open(request, timeout):
        url = urlsplit(request.full_url)
        assert url.scheme == "https" and url.netloc == "api.github.com"
        prefix = "/repos/" + repo
        assert url.path.startswith(prefix + "/")
        path = unquote(url.path[len(prefix):])
        method = request.get_method()
        queries.append((method, path))
        if method == "DELETE":
            assert path == "/git/refs/heads/" + name
            deleted.append(name)
            return io.BytesIO(b"")
        if path == "/branches":
            value = snapshot
        elif path.startswith("/branches/"):
            target = path.removeprefix("/branches/")
            value = json.loads(json.dumps(next(x for x in snapshot if x["name"] == target)))
            if mode == "moved" and target == name:
                value["commit"]["sha"] = "c"*40
        elif path == "/pulls":
            state = parse_qs(url.query)["state"][0]
            count = sum(p == "/pulls" for _, p in queries)
            value = [pr] if state == "open" and (mode == "open" or (mode == "late_open" and count >= 3)) else []
        elif path.startswith("/compare/"):
            count = sum(p.startswith("/compare/") for _, p in queries)
            diverged = mode == "diverged" or (mode == "late_diverged" and count >= 2)
            value = {"status": "diverged" if diverged else "ahead",
                     "behind_by": 1 if diverged else 0}
        else:
            raise AssertionError((method, path))
        return io.BytesIO(json.dumps(value).encode())
    with patch.dict(os.environ, {"GH_TOKEN": "fake-token", "GH_REPO": repo}, clear=True), \
         patch("urllib.request.urlopen", fake_open), contextlib.redirect_stdout(io.StringIO()):
        exec(code, {})
    assert deleted == ([name] if mode == "eligible" else []), (mode, deleted)
    if mode == "eligible":
        assert sum(p.startswith("/compare/") for _, p in queries) == 2

for mode in ("eligible", "protected", "stale", "moved", "open", "late_open",
             "diverged", "late_diverged"):
    exercise(mode)
print("RETIRED_BRANCH_CLEANUP_HOST_PASS scenarios=8 no_network=1 exact_refs=1")
