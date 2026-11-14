#!/usr/bin/env python3
"""Offline PS5 per-title Vulkan shader-cache diagnostic summaries.

Does not read or upload logs unless invoked with an explicit local file.
This tool diagnoses only the cache admission telemetry of the matching build,
not GPU stutter causality or game render correctness.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

KV = re.compile(r"\b([a-z_]+)=([A-Za-z0-9_.:-]+)")
DISK = "EDEN_PS5_SHADER_CACHE_DISK "
ADMISSION = "EDEN_PS5_SHADER_CACHE_ADMISSION "
PREWARM = "EDEN_PS5_SHADER_PREWARM "


def parse_fields(line: str) -> dict[str, str]:
    return dict(KV.findall(line))


def as_int(fields: dict[str, str], name: str, default: int = 0) -> int:
    try:
        return int(fields.get(name, default))
    except ValueError:
        return default


def classify(disk: dict[str, str], admission: dict[str, str],
             prewarm: dict[str, str]) -> str:
    if not disk:
        return "no_diagnostic_markers_or_old_build"
    if as_int(disk, "env_present") == 0:
        return "environment_cache_missing_at_start"
    if as_int(disk, "env_bytes") <= 12:
        return "environment_cache_header_only_or_empty"
    if not admission:
        return "environment_cache_exists_but_admission_unreported"
    if as_int(admission, "env_after_present") == 0:
        return "environment_cache_removed_during_load"
    if as_int(admission, "env_after_bytes") < as_int(disk, "env_bytes"):
        return "environment_cache_truncated_during_load"
    graphics = as_int(admission, "graphics_records")
    filtered = as_int(admission, "graphics_filtered")
    compute = as_int(admission, "compute_records")
    queued = as_int(admission, "queued")
    if graphics > 0 and graphics == filtered and compute == 0:
        return "all_graphics_records_filtered_before_queue"
    if graphics == 0 and compute == 0:
        return "populated_file_yielded_no_records_check_version_or_damage"
    if queued == 0:
        return "cache_records_seen_but_no_pipelines_admitted"
    if prewarm and as_int(prewarm, "valid") == 0:
        return "pipelines_queued_but_none_executable"
    return "cache_admitted_verify_reuse_and_frame_latency"


def summarize(lines: list[str]) -> list[dict]:
    sessions: list[dict] = []
    current: dict | None = None
    for line in lines:
        if DISK in line:
            fields = parse_fields(line.split(DISK, 1)[1])
            current = {"title": fields.get("title", "unknown"),
                       "disk": fields, "admission": {}, "prewarm": {}}
            sessions.append(current)
        elif current is not None and ADMISSION in line:
            current["admission"] = parse_fields(line.split(ADMISSION, 1)[1])
        elif current is not None and PREWARM in line:
            current["prewarm"] = parse_fields(line.split(PREWARM, 1)[1])
    for session in sessions:
        session["assessment"] = classify(
            session["disk"], session["admission"], session["prewarm"])
    return sessions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="PS5 stdout/stderr saved locally")
    parser.add_argument("--json", action="store_true",
                        help="emit structured sessions instead of compact text")
    options = parser.parse_args()
    sessions = summarize(options.log.read_text(
        encoding="utf-8", errors="replace").splitlines())
    if options.json:
        print(json.dumps(sessions, indent=2, ensure_ascii=False))
    elif not sessions:
        print("No EDEN_PS5_SHADER_CACHE_DISK markers; "
              "installed build may predate diagnostics.")
    else:
        for session in sessions:
            disk = session["disk"]
            admitted = session["admission"]
            print(f"title={session['title']} "
                  f"env_bytes={disk.get('env_bytes', '?')} "
                  f"driver_bytes={disk.get('driver_bytes', '?')} "
                  f"records_gfx={admitted.get('graphics_records', '?')} "
                  f"gfx_filtered={admitted.get('graphics_filtered', '?')} "
                  f"queued={admitted.get('queued', '?')} "
                  f"status={session['assessment']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
