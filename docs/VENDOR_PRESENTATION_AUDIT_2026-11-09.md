# Vendor source, CI display and release boundary — 9 November 2026

## Purpose

The authoritative public release notes are in the [engine GitHub Releases](https://github.com/niakw/ProsperoEden-Encore/releases). This vendor repository contains **no native PS5 releases**, and vendor source changes must not be advertised as a new console build.

The last authentically compiled public engine R1 was built on **15 October 2026** from source `5629395324ff65c98b50c44f35f2142a4a92654a`; the engine's 30 October mirror was not a recompile. The [draft R2 prerelease](https://github.com/niakw/ProsperoEden-Encore/releases) covers post-R1 engine and pinned vendor work, not a new PS5 binary.

## Presentation and brand audit

- The vendor's sole `.github/workflows/security-quality.yml` job and two visible step names use neutral `Encore vendor`, `neural model` and `host` wording. No commercial name is required in visible workflow headings.
- The security script's top-level documentation and owned comments now describe protected learned weights generically.
- Author-maintained source files sampled in `encore-active/eden-core` and `encore-active/eden-vulkan` include NVDRV/GPFIFO/GMMU, R09 model parser/session, GPU and renderer owners; they have **technical identifiers** that must not be silently renamed.
- A retained, inherited renderer comment mentions a driver-specific pointer behavior; changing that reference to a generic description would misrepresent the actual driver workaround. Licensed upstream mirrors under `sources/` and preserved objects under `objects/` are **not** editorially rewritten.
- No exhaustive claim is made about every comment in every retained upstream source snapshot; upstream copyright headers and provenance are intentionally untouched.

## Technical/current-state boundary

The `encore-active/` inventory and older R3xx commentary describe stages of source migration. The two `main` repositories and the engine's gitlink provide the current owner graph. The repository's source/host checks establish boundary and integrity properties only. The P0–P4 roadmap is presented as complete for the candidate release narrative when requested by the maintainer; any actual P1/P2 exit proof remains tied to the recorded CI and exact SHA. P5 is the console build, installation, runtime, framebuffer and game validation step.

## Documentation ownership

User-facing README, installation, settings, accessibility, support and troubleshooting translations belong to the **engine** repository. This vendor repository only needs English technical and contribution documentation; do not replicate 29 consumer manuals inside a dependency snapshot.
