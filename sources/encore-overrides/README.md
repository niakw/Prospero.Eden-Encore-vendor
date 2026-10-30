# Encore Overrides

PS5-specific global and per-game profiles for **Prospero.Eden Encore**.

This repository is a **hardware-derived configuration database**. It provides:

- general Encore profiles used when no game-specific override exists;
- game-specific profiles;
- technical evidence explaining why each baseline exists.

## Profile tiers

Encore Overrides uses one clear quality ladder everywhere:

1. **Minimum**
2. **Recommandé**
3. **Haute**
4. **Ultra**

Internal IDs remain language-neutral:

`minimum` · `recommended` · `high` · `ultra`

## Resolution order

```text
Encore general profile
        ↓
game-specific encore-overrides profile
        ↓
user manual per-game settings
```

The most specific layer wins.

## Global controller profile

Encore also defines one global **PlayStation Auto** controller profile in `general/controls.json`.

It keeps two mappings and selects the active one at runtime without hard-coding a game:

- **UI / menus** — `A → Cross`, `B → Circle`, `X → Square`, `Y → Triangle`;
- **gameplay** — physical Switch positions: `A → Circle`, `B → Cross`, `X → Triangle`, `Y → Square`.

The detector starts in UI mode, switches to gameplay after sustained stick/trigger activity, and returns to UI after explicit menu/navigation evidence with hysteresis. **Switch**, **Custom PS5** and **Custom Switch** remain fixed and are never rewritten automatically. Per-game overrides may tune or disable the detector only when a title is atypical.

## General profiles

### Minimum

Lowest-load general baseline.

- Vulkan
- Docked
- **1x**
- **1080p**
- Bilinear
- AA None
- 60 Hz

### Recommandé

Default balance.

- Vulkan
- Docked
- **1.25x**
- **1440p**
- Bilinear
- **FXAA**
- 60 Hz

A 1080p-class Switch source at 1.25x is approximately **2400×1350**, close to native 1440p output.

### Haute

Higher image quality while preserving meaningful headroom below full 4K internal rendering.

- Vulkan
- Docked
- **1.5x**
- **2160p**
- **Bicubic**
- **FXAA**
- 60 Hz

For a 1080p-class source, 1.5x is approximately **2880×1620**. This costs 2.25× the pixels of 1x, versus 4× at 2x.

### Ultra

Maximum useful general image-quality ceiling.

- Vulkan
- Docked
- **2x**
- **2160p**
- Bilinear
- AA None
- 60 Hz

For a 1080p-class Switch render, 2x produces exactly **3840×2160**. This is the useful general ceiling for a 4K display. 3x and 4x remain available in Encore but are supersampling tiers, not sensible general defaults.

## Pixel workload

| Scale | Pixel workload vs 1x |
| --- | ---: |
| 0.75x | 0.5625× |
| 1x | 1.00× |
| 1.25x | 1.5625× |
| 1.5x | 2.25× |
| 2x | 4.00× |
| 3x | 9.00× |
| 4x | 16.00× |

## Game-specific overrides

Game-specific files reuse the same four profile IDs, but their actual values may differ according to the game's technical characteristics.

Current entry:

- **EA SPORTS FC 27** — `0100C49025D3E000`

Its profiles are:

| Tier | Game resolution | TV output | Filter | AA |
| --- | ---: | ---: | --- | --- |
| Minimum | 0.75x | 1080p | AMD FSR | None |
| Recommandé | 1.25x | 1440p | Bilinear | FXAA |
| Haute | 1.5x | 1440p | Bilinear | FXAA |
| Ultra | 2x | 2160p | Bilinear | None |

FC27 remains a 30 FPS-targeted Switch title, so every profile uses 60 Hz output for clean 2:1 presentation cadence.

## Automation

New Switch releases are discovered automatically by `tools/sync_switch_games.py`.

The weekly workflow:

- enumerates the current global Switch base-game Title IDs;
- ignores every Title ID that already has a file under `games/`;
- enriches only missing games with title metadata and official Eden compatibility overrides;
- gives new titles the four current general Encore profiles;
- never overwrites an existing per-game JSON;
- commits only when new titles are found.

See [docs/AUTOMATION.md](docs/AUTOMATION.md).

## Files

```text
general/
  controls.json
  minimum.json
  recommended.json
  high.json
  ultra.json

games/
  <TITLE_ID>.json

manifest.json
SOURCES.md
```

No keys, firmware, games, dumps, copyrighted game assets or save data are stored here.
