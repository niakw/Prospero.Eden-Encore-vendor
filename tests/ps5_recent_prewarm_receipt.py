#!/usr/bin/env python3
"""Exact, source-only reversal for the bounded FC27 cached-pipeline admission change."""
import hashlib

RECENT_128_BLOB = "79660c5cc4ff046f24e7ffa50ab066a86857698c"
RECENT_32_BLOB = "a0e2a45b9b0f080b739d97abb92e9485a0063f73"
RECENT_BUDGET_REVERSES = [
    ("    // FC27: the old fixed 32-entry PREFIX admitted 18 compute and only\n    // 14 graphics shaders from a 28.7 MiB cache. Thousands of newer records\n    // were ignored and the GPU owner still translated 303 new graphics keys.\n    // A recorded FC27 warm cache contains 710 valid pipeline records, yet the\n    // 32-entry prewarm left 361 guest shaders to translate during the match.\n    // For a modest, already populated cache, admit the newest 128 builders\n    // before gameplay; preserve the 32-entry prefix for large/absent caches.\n    // This shifts some first-use work to loading without skipping any draw.\n    constexpr size_t kStartupPrewarmPipelines = 32;\n    constexpr size_t kRecentPrewarmPipelines = 128;\n    constexpr std::uintmax_t kRecentShaderCacheScanLimit = 64ULL << 20;\n    const bool prefer_recent_cache =\n        env_before.present && env_before.bytes > 12 &&\n        env_before.bytes <= kRecentShaderCacheScanLimit;\n    const size_t prewarm_pipeline_limit =\n        prefer_recent_cache ? kRecentPrewarmPipelines : kStartupPrewarmPipelines;\n", "    // FC27: the old fixed 32-entry PREFIX admitted 18 compute and only\n    // 14 graphics shaders from a 28.7 MiB cache. Thousands of newer records\n    // were ignored and the GPU owner still translated 303 new graphics keys.\n    // Parse modest caches once and retain only the newest 32 valid builders;\n    // a large cache keeps the existing fast prefix cap to avoid startup I/O.\n    constexpr size_t kStartupPrewarmPipelines = 32;\n    constexpr std::uintmax_t kRecentShaderCacheScanLimit = 64ULL << 20;\n    const bool prefer_recent_cache =\n        env_before.present && env_before.bytes > 12 &&\n        env_before.bytes <= kRecentShaderCacheScanLimit;\n"),
    ("if (recent_cache_builds.size() == prewarm_pipeline_limit)", "if (recent_cache_builds.size() == kStartupPrewarmPipelines)"),
    ("if (state.total == prewarm_pipeline_limit)", "if (state.total == kStartupPrewarmPipelines)"),
    ("static_cast<long long>(startup_prewarm_ms), prewarm_pipeline_limit);", "static_cast<long long>(startup_prewarm_ms), kStartupPrewarmPipelines);"),
]

def git_blob(text: str) -> str:
    raw = text.encode("utf-8")
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()

def reverse_adaptive_prewarm(text: str) -> str:
    assert git_blob(text) == RECENT_128_BLOB, "Unreviewed cached-pipeline source"
    for current, prior in RECENT_BUDGET_REVERSES:
        assert text.count(current) == 1, ("Changed prewarm owner", current[:90])
        text = text.replace(current, prior, 1)
    assert git_blob(text) == RECENT_32_BLOB, "Recent prewarm predecessor drift"
    return text
