# Vendor source, CI display and release boundary — 9 November 2026

## Purpose

The authoritative public release notes are in the [engine GitHub Releases](https://github.com/niakw/ProsperoEden-Encore/releases). This vendor repository contains **no native PS5 releases**, and vendor source changes must not be advertised as a new console build.

The last authentically compiled public engine R1 was built on **15 October 2026** from source `5629395324ff65c98b50c44f35f2142a4a92654a`; the engine's 30 October mirror was not a recompile. The [draft R2 prerelease](https://github.com/niakw/ProsperoEden-Encore/releases) covers post-R1 engine and pinned vendor work, not a new PS5 binary.

## Presentation and brand audit

- The vendor's sole `.github/workflows/security-quality.yml` job and two visible step names use neutral `Encore vendor`, `neural model` and `host` wording. No commercial name is required in visible workflow headings.
- The security script's top-level documentation and owned comments now describe protected learned weights generically.
- Author-maintained source files sampled in `encore-active/eden-core` and `encore-active/eden-vulkan` include NVDRV/GPFIFO/GMMU, R09 model parser/session, GPU and renderer owners; they have **technical identifiers** that must not be silently renamed.
- A retained, inherited renderer comment mentions a driver-specific pointer behavior; changing that reference to a generic description would misrepresent the actual driver workaround. Licensed upstream mirrors under `sources/` and preserved objects under `objects/` are **not** editorially rewritten.
- A dedicated maintained-source comment scanner now checks **606 active C/C++/Python/source files** on the vendor runner. Initial review found **79 references in comments**, covering four name families. Most are **required technical references**: GPU driver behavior, OEM SDK contracts, GPU opcode/manual provenance, third-party source/project names or the implementation of platform-specific security rules. They must not be blindly erased.
- A further **nine authored source files** had their *non-technical* proprietary-model labels neutralized without changing code, identifiers, external source links or compiler flags. References such as the actual driver selection order, NVC597 register-layout notes, source licensing and CPU/GPU vendor-specific workarounds remain intentionally exact.
- The [vendor Security & Quality host run #42592867324](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42592867324) executed the scanner and the existing model-parser/security checks successfully **before** the nine comment-only source edits. It is **not** a PS5 native build or a fresh post-edit host sign-off.
- No exhaustive claim is made about every comment in every retained upstream source snapshot; upstream copyright headers and provenance are intentionally untouched.

## Technical/current-state boundary

The `encore-active/` inventory and older R3xx commentary describe stages of source migration. The two `main` repositories and the engine's gitlink provide the current owner graph. The repository's source/host checks establish boundary and integrity properties only. The P0–P4 roadmap is presented as complete for the candidate release narrative when requested by the maintainer; any actual P1/P2 exit proof remains tied to the recorded CI and exact SHA. P5 is the console build, installation, runtime, framebuffer and game validation step.

## Documentation ownership

User-facing README, installation, settings, accessibility, support and troubleshooting translations belong to the **engine** repository. This vendor repository only needs English technical and contribution documentation; do not replicate 29 consumer manuals inside a dependency snapshot.

## Completed active-source scan — verified GitHub Actions result

The [vendor owned-comment and workflow display audit #42618761816](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42618761816) completed **successfully** on 9 November 2026. It used a sparse checkout; no console binary, model, dependency archive or executable was modified.

| Measured scope | Result |
| --- | ---: |
| Maintained/active source text files scanned | **775** |
| Brand occurrences in owned comment-like lines | **100** |
| Brand occurrences in retained upstream-derived areas | **24** |
| Vendor workflow display titles/steps containing commercial names | **0** |

The 100 matches break down as **NVIDIA 44**, **AMD 26**, **Nintendo 24**, **Microsoft 4** and **PlayStation 2**. This is a *review inventory, not a list of 100 cosmetic violations*. Reviewed examples include `Service::Nvidia::Devices` namespace end comments, GPU device/driver workarounds, vendor hardware memory layout notes, citations to original GPU documentation, filesystem-format definitions, PS5 platform descriptions, model-layout provenance and license/source notices. Many need to retain their precise technical attribution to remain accurate, and some are technically meaningful comments in maintained upstream-derived code.

**Disposition:** GitHub Actions branding is clean; descriptive branding not required for technical meaning has already been neutralized in the author-owned scripts and engine comments. Do **not** blanket-replace legal credits, authoritative source references, GPU architecture names, namespace identifiers, compatibility facts or driver-specific observations. This audit does not certify absence of all proprietary words in arbitrary historical vendor snapshots under `sources/` or `objects/`, which must preserve their original bytes.
