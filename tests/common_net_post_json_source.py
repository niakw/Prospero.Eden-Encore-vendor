#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
upstream_header = root / "sources/eden/src/common/net/net.h"
upstream_source = root / "sources/eden/src/common/net/net.cpp"
active = root / "encore-active/eden/src/common/net"
header = (active / "net.h").read_text()
source = (active / "net.cpp").read_text()

# sources/ is the imported dependency; the maintained transport belongs only
# to the canonical Encore override at the same Eden-relative path.
assert "PostJson(" not in upstream_source.read_text()
assert "HttpResponse" not in upstream_header.read_text()

assert "struct HttpResponse" in header
assert "std::optional<HttpResponse> PostJson(" in header
owner = source.split("std::optional<HttpResponse> PostJson(", 1)[1].split(
    "\nstd::vector<Release> GetReleases()", 1
)[0]
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

print(
    "COMMON_NET_HTTPS_POST_JSON_SOURCE_PASS "
    "canonical_override=1 request_cap=64KiB response_cap=64KiB "
    "redirect=off timeout_max=15s"
)
