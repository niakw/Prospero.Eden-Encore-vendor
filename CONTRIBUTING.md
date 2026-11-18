# Contributing to Eden Encore vendor sources

This is the pinned source and dependency repository for [Eden Encore](https://github.com/niakw/ProsperoEden-Encore), not a standalone emulator release. Normal development, bug reports and roadmap tracking belong in the engine repository.

## Language of project records

**English (en-US) is the default language for all new and materially revised technical documentation and persistent project records**, including project memory and handoff notes, audits, build/CI reports, issues and pull requests (titles, descriptions, reviews and comments), commit messages, changelogs and release notes.

- Preserve historical source, third-party attribution, licenses, file paths, API identifiers, log excerpts and original quoted material. Do not rewrite existing history solely for translation.
- Continue supporting all launcher/UI localization languages; translating technical records does **not** change the game's or console's locale.
- Discussions with a contributor may use their preferred language (for example French), while the corresponding recorded GitHub issue or PR remains English by default.
- Keep diagnostics and validation evidence tied to exact engine/vendor SHAs; an English write-up is not a substitute for host/native/PS5 proof.

See [AGENTS.md](AGENTS.md) for the default conventions followed by automated coding assistants and contributors.

## Change boundaries

- Preserve upstream snapshots under `sources/` and immutable provenance objects under `objects/`. Do not rewrite licenses, copyright headers, file names, API identifiers or historical upstream attribution to remove product names.
- Make authored corrections in `encore-active/` only when the corresponding build owner and source parity are known. Do not modify source simply to hide a required GPU or SDK term.
- Keep source and host validation separate from real PS5 qualification; do not claim an active override has been console-tested just because it compiles.
- Update the engine's `vendor` gitlink to the exact reviewed vendor SHA when a vendor change is adopted.
- No proprietary model binaries, console keys, firmware, private saves or credentials may be added.

Please submit a focused report to the [engine issue tracker](https://github.com/niakw/ProsperoEden-Encore/issues) before a wide dependency-version or source-architecture change.

## Security dependency updates

A grouped Dependabot PR that alters a pinned upstream `sources/` tree and fails `tools/check-source-tree-layout.py` must not be merged by weakening its hash checks. Isolate Python dependency pair upgrades (Torch/Torchaudio, pytest/pytest-asyncio), reproduce `python -m pip check` and tool-path tests in clean host-only environments, and revise exact provenance only for a deliberately approved upstream source migration. A closed PR is not equivalent to an addressed vulnerability.
