#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parents[1]
upstream_header=root/"sources/eden/src/common/net/net.h"
upstream_source=root/"sources/eden/src/common/net/net.cpp"
active=root/"encore-active/eden/src/common/net"
header=(active/"net.h").read_text()
source=(active/"net.cpp").read_text()
manifest=json.loads((root/"encore-active/eden/backports/manifest.json").read_text())

# The pinned Eden source remains immutable. Encore-specific transport belongs
# to the shipping backport that install-vendored-eden-backports.py selects.
assert hashlib.sha256(upstream_source.read_bytes()).hexdigest()=="6c5b0dbf4cd7f0ea1cdeb292611a1abbae30f60a83e6a076bbb7780de523f8ad"
assert hashlib.sha256(upstream_header.read_bytes()).hexdigest()=="7905b53d92150c14d194bb762ad292e50f2febbbda3ea1fbe50df1202cfe0295"
assert "PostJson(" not in upstream_source.read_text()
assert "HttpResponse" not in upstream_header.read_text()

assert "struct HttpResponse" in header
assert "std::optional<HttpResponse> PostJson(" in header
owner=source.split("std::optional<HttpResponse> PostJson(",1)[1].split("\nstd::vector<Release> GetReleases()",1)[0]
for token in (
    'url.starts_with("https://")',
    "path.front() != '/'",
    'path.find_first_of("\\r\\n")',
    "body.size() > max_request_bytes",
    "timeout_seconds == 0",
    "timeout_seconds > max_timeout_seconds",
    "client->set_connection_timeout(timeout_seconds)",
    "client->set_read_timeout(timeout_seconds)",
    "client->set_write_timeout(timeout_seconds)",
    "client->set_follow_location(false)",
    'client->Post(path, body, "application/json")',
    "response.body.size() > max_response_bytes",
    "HttpResponse{.status = response.status, .body = response.body}",
):
    assert token in owner, token
assert "Authorization" not in owner
assert "set_follow_location(true)" not in owner
assert "http://" not in owner

for rel in ("src/common/net/net.cpp","src/common/net/net.h"):
    item=manifest["entries"][rel]
    data=(root/"encore-active/eden"/rel).read_bytes()
    upstream=(root/"sources/eden"/rel).read_bytes()
    assert hashlib.sha256(data).hexdigest()==item["active"], rel
    assert hashlib.sha256(upstream).hexdigest()==item["upstream"], rel
    assert len(data)==item["bytes"], rel

print("COMMON_NET_HTTPS_POST_JSON_SOURCE_PASS active_backport=1 pinned_upstream_immutable=1 request_cap=64KiB response_cap=64KiB redirect=off timeout_max=15s")
