# Automatic game discovery

`tools/sync_switch_games.py` builds baseline Encore profiles for Nintendo Switch base games that do not already exist in this repository.

## Core rule

**Existing `games/<TITLE_ID>.json` files are never modified.**

This means a manually tuned game such as FC27 remains authoritative forever unless a maintainer edits it explicitly.

## Sources

The generator currently combines:

- `ch0c01dxyz/nsw-titledb/versions.json` — canonical discovery set of Title IDs;
- US / FR / JP regional title metadata — name, publisher, release-date/NSU metadata when available;
- `eden-emulator/eden-overrides/overrides.ini` — official Eden compatibility overrides;
- local Encore `general/*.json` files — Minimum / Recommandé / Haute / Ultra settings;
- fixed Nintendo Switch 1 and standard PS5 hardware facts used by the project.

The Title DB is metadata-only and MIT-licensed. Eden's override list is used only as compatibility evidence; its settings are not presented as recommended settings.

## Missing game-specific technical data

The generator does **not** invent a game's native resolution or FPS target.

For a newly discovered title without trusted technical measurements:

```json
"source_game": {
  "docked_resolution": null,
  "handheld_resolution": null,
  "target_fps": null,
  "confidence": "unknown"
}
```

The title receives the four general Encore profiles until a game-specific profile is authored.

## Manual use

Preview:

```bash
python3 tools/sync_switch_games.py
```

Create every currently missing base-game entry:

```bash
python3 tools/sync_switch_games.py --write
```

Development smoke test:

```bash
python3 tools/sync_switch_games.py --write --limit 5
```

## Weekly automation

`.github/workflows/weekly-switch-game-sync.yml` runs every Monday.

It:

1. downloads the current Switch Title-ID catalog;
2. removes every Title ID already present under `games/`;
3. enriches only the remaining/new IDs;
4. creates their JSON files;
5. updates `manifest.json`;
6. validates every profile;
7. commits only when new games were actually found.

The workflow can also be started manually with an optional limit.
