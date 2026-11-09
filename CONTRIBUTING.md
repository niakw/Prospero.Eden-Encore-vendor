# Contributing to Eden Encore vendor sources

This is the pinned source and dependency repository for [Eden Encore](https://github.com/niakw/ProsperoEden-Encore), not a standalone emulator release. Normal development, bug reports and roadmap tracking belong in the engine repository.

## Change boundaries

- Preserve upstream snapshots under `sources/` and immutable provenance objects under `objects/`. Do not rewrite licenses, copyright headers, file names, API identifiers or historical upstream attribution to remove product names.
- Make authored corrections in `encore-active/` only when the corresponding build owner and source parity are known. Do not modify source simply to hide a required GPU or SDK term.
- Keep source and host validation separate from real PS5 qualification; do not claim an active override has been console-tested just because it compiles.
- Update the engine's `vendor` gitlink to the exact reviewed vendor SHA when a vendor change is adopted.
- No proprietary model binaries, console keys, firmware, private saves or credentials may be added.

Please submit a focused report to the [engine issue tracker](https://github.com/niakw/ProsperoEden-Encore/issues) before a wide dependency-version or source-architecture change.
