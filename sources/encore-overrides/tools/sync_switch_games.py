#!/usr/bin/env python3
"""Discover new Nintendo Switch base-game Title IDs and create Encore baseline JSON files.

Important behavior:
- Existing games/<TITLE_ID>.json files are NEVER modified.
- New games inherit the current general Minimum/Recommandé/Haute/Ultra settings.
- Game-specific FPS/resolution data is left unknown unless a trusted source supplies it.
- Eden's official compatibility overrides are attached as compatibility metadata, not guessed.
- manifest.json is updated only when new game files are created.

Standard-library only so the weekly GitHub Actions job needs no pip dependencies.
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import sys
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
GAMES_DIR = ROOT / "games"
GENERAL_DIR = ROOT / "general"
MANIFEST = ROOT / "manifest.json"

TITLE_RE = re.compile(r"^01[0-9A-F]{14}$")
SECTION_RE = re.compile(r"^\[\s*([0-9A-Fa-f]{16})(?:\s*\|\s*(.*?))?\s*\]$")

VERSIONS_URL = "https://raw.githubusercontent.com/ch0c01dxyz/nsw-titledb/master/versions.json"
REGION_SOURCES = [
    ("US", "en", "https://raw.githubusercontent.com/ch0c01dxyz/nsw-titledb/master/US.en.json"),
    ("FR", "fr", "https://raw.githubusercontent.com/ch0c01dxyz/nsw-titledb/master/FR.fr.json"),
    ("JP", "ja", "https://raw.githubusercontent.com/ch0c01dxyz/nsw-titledb/master/JP.ja.json"),
]
EDEN_OVERRIDES_URL = "https://raw.githubusercontent.com/eden-emulator/eden-overrides/master/overrides.ini"

SWITCH_HARDWARE = {
    "platform": "Nintendo Switch 1",
    "soc": "NVIDIA custom Tegra",
    "memory": "4 GB LPDDR4-class",
    "built_in_display": "1280x720",
    "tv_output_limit": "1920x1080@60",
}

PS5_HARDWARE = {
    "platform": "PS5 standard / original model",
    "cpu": "AMD Zen 2, 8 cores / 16 threads, up to 3.5 GHz",
    "gpu": "AMD RDNA 2-class, approximately 10.3 TFLOPS",
    "memory": "16 GB GDDR6",
    "memory_bandwidth": "448 GB/s",
}

NAME_KEYS = (
    "name",
    "title",
    "formalName",
    "formal_name",
    "productName",
    "product_name",
    "displayName",
)
PUBLISHER_KEYS = (
    "publisher",
    "publisherName",
    "publisher_name",
    "developer",
)
DATE_KEYS = (
    "releaseDate",
    "release_date",
    "releaseDateDisplay",
    "release_date_display",
    "releaseDateString",
)
NSU_KEYS = ("nsuId", "nsuid", "nsu_id")
TITLE_KEYS = ("titleId", "titleID", "title_id", "applicationId", "application_id")


def fetch_bytes(url: str, *, timeout: int = 90) -> bytes:
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "encore-overrides-sync/1.0 (+https://github.com/niakw/encore-overrides)",
            "Accept": "application/json,text/plain,*/*",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return response.read()


def fetch_json(url: str) -> Any:
    return json.loads(fetch_bytes(url).decode("utf-8"))


def normalize_title_id(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    value = value.strip().upper()
    return value if TITLE_RE.fullmatch(value) else None


def is_base_game(title_id: str) -> bool:
    # Switch applications use the base program Title ID ending in 000.
    # Updates conventionally end in 800 and add-on content uses other low IDs.
    return title_id.endswith("000")


def iter_objects(value: Any):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from iter_objects(child)
    elif isinstance(value, list):
        for child in value:
            yield from iter_objects(child)


def find_title_id(obj: dict[str, Any]) -> str | None:
    for key in TITLE_KEYS:
        title_id = normalize_title_id(obj.get(key))
        if title_id:
            return title_id

    # Some title databases use a generic "id" field for Title ID. Only accept
    # it when it actually looks like a Switch Title ID.
    return normalize_title_id(obj.get("id"))


def first_text(obj: dict[str, Any], keys: tuple[str, ...]) -> str | None:
    for key in keys:
        value = obj.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip()
    return None


def scalar_values(obj: dict[str, Any], keys: tuple[str, ...]) -> list[str]:
    out: list[str] = []
    for key in keys:
        value = obj.get(key)
        if isinstance(value, (str, int)):
            text = str(value).strip()
            if text and text not in out:
                out.append(text)
        elif isinstance(value, list):
            for item in value:
                if isinstance(item, (str, int)):
                    text = str(item).strip()
                    if text and text not in out:
                        out.append(text)
    return out


def collect_title_ids() -> set[str]:
    data = fetch_json(VERSIONS_URL)
    found: set[str] = set()

    if isinstance(data, dict):
        for key in data:
            title_id = normalize_title_id(key)
            if title_id and is_base_game(title_id):
                found.add(title_id)

    # Future-proof if upstream ever changes from titleId-keyed objects.
    if not found:
        for obj in iter_objects(data):
            title_id = find_title_id(obj)
            if title_id and is_base_game(title_id):
                found.add(title_id)

    if not found:
        raise RuntimeError("Title DB returned no base-game Title IDs")
    return found


def collect_metadata(wanted: set[str]) -> dict[str, dict[str, Any]]:
    metadata: dict[str, dict[str, Any]] = {}
    successful_sources = 0

    for region, language, url in REGION_SOURCES:
        try:
            data = fetch_json(url)
        except Exception as exc:  # noqa: BLE001 - one regional mirror may fail transiently
            print(f"warning: metadata source {region}.{language} failed: {exc}", file=sys.stderr)
            continue

        successful_sources += 1
        for obj in iter_objects(data):
            title_id = find_title_id(obj)
            if not title_id or title_id not in wanted:
                continue

            entry = metadata.setdefault(
                title_id,
                {
                    "regions": [],
                    "languages": [],
                    "release_dates": [],
                    "nsu_ids": [],
                },
            )
            if region not in entry["regions"]:
                entry["regions"].append(region)
            if language not in entry["languages"]:
                entry["languages"].append(language)

            if not entry.get("name"):
                name = first_text(obj, NAME_KEYS)
                if name:
                    entry["name"] = name

            if not entry.get("publisher"):
                publisher = first_text(obj, PUBLISHER_KEYS)
                if publisher:
                    entry["publisher"] = publisher

            for release_date in scalar_values(obj, DATE_KEYS):
                if release_date not in entry["release_dates"]:
                    entry["release_dates"].append(release_date)

            for nsu_id in scalar_values(obj, NSU_KEYS):
                if nsu_id not in entry["nsu_ids"]:
                    entry["nsu_ids"].append(nsu_id)

    if successful_sources == 0:
        raise RuntimeError("All regional metadata sources failed")
    return metadata


def parse_eden_overrides(text: str) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = {}
    current: dict[str, Any] | None = None
    current_id: str | None = None

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith((";", "#")):
            continue

        match = SECTION_RE.match(line)
        if match:
            current_id = match.group(1).upper()
            current = {
                "conditions": match.group(2).strip() if match.group(2) else None,
                "settings": {},
            }
            result.setdefault(current_id, []).append(current)
            continue

        if current is not None and current_id is not None and "=" in line:
            key, value = line.split("=", 1)
            current["settings"][key.strip()] = value.strip()

    return result


def load_general_profiles() -> dict[str, dict[str, Any]]:
    profiles: dict[str, dict[str, Any]] = {}
    for profile_id in ("minimum", "recommended", "high", "ultra"):
        path = GENERAL_DIR / f"{profile_id}.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        profiles[profile_id] = {
            "name": data["name"],
            "profile_source": f"general/{profile_id}.json",
            "settings": copy.deepcopy(data["settings"]),
        }
    return profiles


def build_game(
    title_id: str,
    metadata: dict[str, Any],
    eden: list[dict[str, Any]],
    profiles: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    name = metadata.get("name") or f"Nintendo Switch title {title_id}"

    return {
        "schema_version": 1,
        "title_id": title_id,
        "name": name,
        "source_console": "Nintendo Switch 1",
        "target_hardware": "PS5 standard",
        "status": "auto-baseline",
        "auto_apply": True,
        "default_profile": "recommended",
        "profiles": copy.deepcopy(profiles),
        "technical_data": {
            "catalog": {
                "source": "ch0c01dxyz/nsw-titledb",
                "regions": metadata.get("regions", []),
                "languages": metadata.get("languages", []),
                "publisher": metadata.get("publisher"),
                "release_dates": metadata.get("release_dates", []),
                "nsu_ids": metadata.get("nsu_ids", []),
            },
            "source_game": {
                "docked_resolution": None,
                "handheld_resolution": None,
                "target_fps": None,
                "confidence": "unknown",
                "rule": "Do not infer game-specific resolution or FPS without a trusted source.",
            },
            "switch_1": copy.deepcopy(SWITCH_HARDWARE),
            "ps5_standard": copy.deepcopy(PS5_HARDWARE),
            "eden": {
                "official_overrides_source": "eden-emulator/eden-overrides",
                "compatibility_overrides": eden,
            },
            "encore": {
                "profile_model": ["minimum", "recommended", "high", "ultra"],
                "profile_origin": "general baseline",
                "manual_user_override_wins": True,
            },
        },
        "generated": {
            "generator": "tools/sync_switch_games.py",
            "policy": "new-title-only; existing repository entries are never rewritten",
        },
    }


def load_manifest() -> dict[str, Any]:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def existing_title_ids() -> set[str]:
    return {
        path.stem.upper()
        for path in GAMES_DIR.glob("*.json")
        if normalize_title_id(path.stem)
    }


def sync(*, write: bool, limit: int | None) -> int:
    GAMES_DIR.mkdir(parents=True, exist_ok=True)

    existing = existing_title_ids()
    discovered = collect_title_ids()
    new_ids = sorted(discovered - existing)
    if limit is not None:
        new_ids = new_ids[:limit]

    print(
        f"discovered={len(discovered)} existing={len(existing)} "
        f"new={len(new_ids)}"
    )

    if not new_ids:
        return 0

    metadata = collect_metadata(set(new_ids))
    eden_text = fetch_bytes(EDEN_OVERRIDES_URL).decode("utf-8", errors="replace")
    eden_overrides = parse_eden_overrides(eden_text)
    profiles = load_general_profiles()

    manifest = load_manifest()
    entries = manifest.setdefault("entries", [])
    manifest_ids = {
        str(entry.get("title_id", "")).upper()
        for entry in entries
        if isinstance(entry, dict)
    }

    created: list[str] = []
    for title_id in new_ids:
        game = build_game(
            title_id,
            metadata.get(title_id, {}),
            eden_overrides.get(title_id, []),
            profiles,
        )
        target = GAMES_DIR / f"{title_id}.json"

        # Double fail-safe: never overwrite a game profile even if the directory
        # changed between discovery and write.
        if target.exists():
            print(f"skip-existing {title_id}")
            continue

        if write:
            target.write_text(
                json.dumps(game, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )

        if title_id not in manifest_ids:
            entries.append(
                {
                    "title_id": title_id,
                    "name": game["name"],
                    "path": f"games/{title_id}.json",
                    "status": "auto-baseline",
                    "auto_apply": True,
                    "default_profile": "recommended",
                    "profiles": ["minimum", "recommended", "high", "ultra"],
                }
            )
            manifest_ids.add(title_id)

        created.append(title_id)

    if write and created:
        entries.sort(key=lambda item: str(item.get("title_id", "")))
        manifest["database_revision"] = int(manifest.get("database_revision", 0)) + 1
        MANIFEST.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    print(f"{'created' if write else 'would-create'}={len(created)}")
    for title_id in created[:25]:
        print(f"  {title_id} {metadata.get(title_id, {}).get('name', '')}")
    if len(created) > 25:
        print(f"  ... and {len(created) - 25} more")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--write",
        action="store_true",
        help="Create new game JSON files and update manifest.json.",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Optional maximum number of new titles (useful for development).",
    )
    args = parser.parse_args()

    if args.limit is not None and args.limit < 1:
        parser.error("--limit must be positive")

    return sync(write=args.write, limit=args.limit)


if __name__ == "__main__":
    raise SystemExit(main())
