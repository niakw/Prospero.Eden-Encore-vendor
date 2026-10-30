# Eden Encore — in-game PlayStation button graphics

This is the **visual asset** compatibility catalogue, beside
`encore-overrides` performance profiles. It never changes DualSense
input mappings.

Source of truth: [`glyphs/manifest.json`](manifest.json), schema **2**.

A future **verified** rule:

```json
{
  "title_id": "0100123456789000",
  "update_version": "v1.2.0"
}
```

*Illustrative data only.* There are currently **no qualified titles**.
Do not add real rules until a compatible, legally redistributable
PlayStation replacement for game artwork has been tested.

The Title ID identifies a game. The update/display version identifies
the resource release used by that game. **No Build ID is required**:
the NSO executable Build ID can change with updates and is relevant to
executable-dependent cheats/patches, whereas these overrides replace
specific RomFS **graphic files**.

A proposed pack includes per-file **original RomFS SHA-256** and
**replacement SHA-256**, independently checked when the pack is staged.
Eden compares the installed pack's title and update version with the
running game's known update version, then uses its existing RomFS
LayeredFS mod loader to substitute the verified artwork. If information
is missing or a version is incompatible, it retains the Nintendo art.

The existing one-command `tools/sync-encore-overrides.py` pipeline in
Prospero.Eden-Encore also exports these rules into the embedded C++
snapshot. A newer runtime JSON can be used without recompiling.

**Current limitations:** No real in-game PlayStation atlas is distributed;
the source verifier checks a legally extracted original atlas during
staging, *not* the active game's unpatched resource bytes on PS5.
Eden must further validate base-game versions, update discovery,
safe automatic distribution, actual hardware rendering and UI opt-out
before claiming that any supported game automatically changes prompts.
See [Issue #7](https://github.com/niakw/Prospero.Eden-Encore/issues/7).
