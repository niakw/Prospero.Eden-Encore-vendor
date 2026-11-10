# Eden Encore — pinned vendor sources and maintained overrides

This is the **source/dependency repository** used by
[ProsperoEden Encore](https://github.com/niakw/ProsperoEden-Encore)
as its **single `vendor` Git submodule**. It is not a PS5 executable,
release repository, or standalone application. No host-only check asserts
PS5-device compatibility.

## Directory contract — mandatory

| Directory | Purpose | Modification policy |
| --- | --- | --- |
| `sources/` | Original third-party upstream source snapshots, pinned by commit/tree or original manifest | Do **not** patch or reclassify these as Encore-owned files |
| `encore-active/` | Our maintained emulator overrides, graphics ports, fixes and backports | Review and qualify changes as Encore-owned code |
| `objects/` | Stored upstream objects, source archives or chunked SDK inputs | Preserve original bytes, integrity and attribution |
| `docs/` | Upstream origin, dependency security triage and technical audits | Keep links and exact paths synchronized |

**The rule is structural:** a dependency consumed in a PS5 build does not
become an override merely because CMake compiles it. Only the project-owned
changed files are `encore-active/` owners. A source's original version and
a maintained patch must be kept in separate directories.

## Pinned graphical source dependencies

These are **ordinary Git-tracked source trees**, not external/nested
submodules and not files in `encore-active/`.

| Original source path | Repository and version | Exact Git tree |
| --- | --- | --- |
| `sources/ps5-mihawkgen/` | [mihawk-99/PS5_MihawkGen](https://github.com/mihawk-99/PS5_MihawkGen) @ `c57232a9f16bcb6df5fe07c9bb631518f2b1cc0e` | `e3b42f5b89775469bec04795b93f048b74aff207` |
| `sources/ps5-fsr3-optical-flow/` | [Swordpdf/ps5-framegen](https://github.com/Swordpdf/ps5-framegen) @ `64362101757cdd6f7df9eeb984cb53f03f1fbfca` | `85e34d638cd9e8b211e08fb19d1a57a588bb8d76` |
| `sources/libwebp/` | [webmproject/libwebp](https://github.com/webmproject/libwebp) @ `4fa21912338357f89e4fd51cf2368325b59e9bd9`, release **1.6.0** | `3a7830b9713d9187cf1211862d4571ff58b0d507` |

MihawkGen retains its original model `framegen/models/network.fgw`,
with SHA-256 `31ecc7d69af1e9defd62b9abad71bc3430cd8a0a4d2f4eafcabaafa48c34bf6b`.
All original code licences, AMD shader notices, model-distillation
credits, libwebp `COPYING` and `PATENTS` remain within their upstream trees.
The **libwebp 1.5.0 → 1.6.0 upgrade** still requires PS5-device validation.

## Other source inputs

The vendor also stores original upstream snapshots under
`sources/eden/`, `sources/eden-cpm/`, `sources/ffmpeg/`,
`sources/ps5-mesa/`, `sources/opengl-mesa-26-2/`,
`sources/harfbuzz/`, `sources/encore-overrides/` and other source roots.
Maintained PS5/Eden patches are recorded separately under
`encore-active/eden-core/`, `encore-active/eden-vulkan/`,
`encore-active/eden-backports/` and the other project-owned
active directories.

**These source trees are not all regulated by the three graphical Git-tree
pins above.** Each existing dependency family retains its own pinned
provenance or lockfile. Don't infer a native-PS5-test result from the
existence of a checked-in source snapshot.

## Checkout and host-only verification

From the **parent engine** checkout:

```bash
git submodule sync --recursive
git submodule update --init vendor
python3 -B vendor/tools/check-source-tree-layout.py
python3 -B tools/graphics/check-internalized-ps5-deps.py
```

The first Python check verifies the vendor's three exact source tree IDs,
model bytes and original licences, and rejects any re-import under
`encore-active/`. The second additionally verifies the parent has **only**
the vendor Gitlink. The parent owns CMake, packaging and the host shader
and codec regression workflows; those are not located in this repository.

**Security/provenance documentation:**
[PS5 upstream origins](docs/PS5_INTERNAL_DEPENDENCY_ORIGINS_2026-11-10.md),
[dependency triage](docs/DEPENDABOT_TRIAGE_2026-11-10.md),
[vendor security triage](docs/VENDOR_SECURITY_ALERT_TRIAGE_2026-11-09.md),
and [vendor presentation audit](docs/VENDOR_PRESENTATION_AUDIT_2026-11-09.md).
The maintained-source security workflow is
[`security-quality.yml`](.github/workflows/security-quality.yml);
GitHub's default CodeQL setup runs independently.

## Historical documentation

[Archived vendor README](docs/ARCHIVE_VENDOR_README_PRE_LAYOUT_20261110.md)
records the R342–R349 decisions and older source-capture workflows.
It is **not** an authoritative command or directory reference.
Current dependency paths and override classifications are those above.
