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

## Pinned graphical source dependencies — exact reviewed source trees (P5)

The **current PS5 build input** is the engine's immutable `vendor` Gitlink, not whichever vendor `main` commit happens to be newest. The first P5 build [#42846319389](https://github.com/niakw/ProsperoEden-Encore/actions/runs/42846319389) was explicitly launched on engine `6fcfe5454a20f600f092e51bdda331a0796b8220`, which points to vendor **`0b0265079139622b759167c9d10a9a31aba1ea88`**. Vendor documentation/security commits newer than that pin are **not silently pulled into this build**.

These three **original upstream** projects are ordinary tracked source trees in `sources/`, not extra Git submodules. They remain pinned exactly as verified; the untested Dependabot upgrade of the MihawkGen trainer was **not** applied.

| Build source (in vendor) | Upstream author/project and revision | Reviewed, immutable Git tree | Role / attribution |
| --- | --- | --- | --- |
| [`sources/ps5-mihawkgen/`](sources/ps5-mihawkgen/) | [Mihawk's PS5_MihawkGen](https://github.com/mihawk-99/PS5_MihawkGen/tree/c57232a9f16bcb6df5fe07c9bb631518f2b1cc0e) · `c57232a9f16bcb6df5fe07c9bb631518f2b1cc0e` | `e3b42f5b89775469bec04795b93f048b74aff207` | Native frame-generation source; original model `framegen/models/network.fgw`, SHA-256 `31ecc7d69af1e9defd62b9abad71bc3430cd8a0a4d2f4eafcabaafa48c34bf6b` |
| [`sources/ps5-fsr3-optical-flow/`](sources/ps5-fsr3-optical-flow/) | [Swordpdf/ps5-framegen](https://github.com/Swordpdf/ps5-framegen/tree/64362101757cdd6f7df9eeb984cb53f03f1fbfca) · `64362101757cdd6f7df9eeb984cb53f03f1fbfca` | `85e34d638cd9e8b211e08fb19d1a57a588bb8d76` | FSR3 optical flow; AMD FidelityFX shader notices and upstream `COPYING` preserved |
| [`sources/libwebp/`](sources/libwebp/) | [webmproject/libwebp](https://github.com/webmproject/libwebp/tree/4fa21912338357f89e4fd51cf2368325b59e9bd9) · **v1.6.0**, `4fa21912338357f89e4fd51cf2368325b59e9bd9` | `3a7830b9713d9187cf1211862d4571ff58b0d507` | Image decoder/encoder source; `COPYING` and `PATENTS` retained; v1.5.0 → v1.6.0 still awaits PS5-device confirmation |

**Critical distinction:** exact Git-tree values in this table describe **these three imported source trees only**. Other snapshots (Eden, Boost CPM, FFmpeg, Mesa, HarfBuzz) have different pins/manifest rules; `encore-active/` contains only project-maintained **overrides**, never copies of unmodified upstream sources. The tree and model SHA checks run through `tools/check-source-tree-layout.py`; the engine additionally checks the single Gitlink and internalized paths. **Do not change these hashes to make a dependency PR pass.**

**Credits and licence details live in the parent engine:** [full contributor/upstream credits](https://github.com/niakw/ProsperoEden-Encore#credits) · [third-party attribution and licences](https://github.com/niakw/ProsperoEden-Encore/blob/main/THIRD_PARTY_NOTICES.md). The sources here preserve the original authors' notices; this README is not a substitute for either legal attribution.

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

## Security and dependency follow-up — 10 November 2026

The engine's **17/17 P1/P2 host contracts** and six mandatory CI checks passed on engine `7154be1e727437e264609b805d80a2d1a17d82e2` with pinned vendor `0b0265079139622b759167c9d10a9a31aba1ea88`. This does **not** demonstrate native PS5 execution. New vendor-only security documentation or inventory runs are **not** automatically adopted into that validated engine SHA.

[Dependabot PR #26](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/26) was **closed without merge** after a failed source-layout CI and unqualified Python dependency pairings. [Issue #22](https://github.com/niakw/Prospero.Eden-Encore-vendor/issues/22) is closed as a superseded **tracking issue only**; the unresolved dependency risks remain explicitly documented in [the manifest backlog](docs/VENDOR_DEPENDABOT_MANIFEST_INVENTORY_2026-11-10.md) and GitHub security alerts. Never bypass immutable upstream source-tree integrity just to merge an automated dependency batch.

The [read-only Code Scanning inventory](.github/workflows/vendor-code-scanning-inventory.yml) lists current finding IDs, paths and rule groups in its run logs and summary, with read-only `security-events` permission. Its source-path classification is **not** proof of release reachability, a CodeQL fix or an alert dismissal. See [security triage](docs/VENDOR_SECURITY_ALERT_TRIAGE_2026-11-09.md) and [dependency triage](docs/DEPENDABOT_TRIAGE_2026-11-10.md).

**Qualified Code Scanning findings:** [28 exact open alerts, owner and rule inventory (10 November)](docs/VENDOR_CODE_SCANNING_FINDINGS_2026-11-10.md); this is separate from the 201 Security and Quality UI figure.

**Dependabot permissions:** the follow-up [inventory run #42844544589](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42844544589) confirmed 28 Code Scanning alerts again, but GitHub returned `HTTP 403` for the distinct Dependabot alerts API. The screenshot provides **173 manifest-labelled Dependabot alerts**; combined with **28 open Code Scanning alerts**, this arithmetically matches the **201 reported**. Counts are not proof of remediation and the Dependabot API still returns HTTP 403. Details and an authenticated audit command are in the [security triage](docs/VENDOR_SECURITY_ALERT_TRIAGE_2026-11-09.md).
