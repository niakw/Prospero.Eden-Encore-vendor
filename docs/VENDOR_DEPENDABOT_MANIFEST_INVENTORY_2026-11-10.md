# Manifest-level Dependabot inventory — 10 November 2026

This inventory was transcribed from the **user-provided GitHub Security & Quality manifest-filter screenshot**. It is an auditable scope list, **not** a successful API enumeration of individual Dependabot alert IDs or a count of fixed vulnerabilities.

| Upstream manifest in `sources/` | Screenshot alerts | Component / review group |
| --- | ---: | --- |
| `sources/eden-cpm/boost/boost-1.90.0/libs/msm/doc/package-lock.json` | 54 | Boost.MSM documentation npm lock, upstream snapshot |
| `sources/eden-cpm/opus/a3f0ec02b3/dnn/torch/osce/requirements.txt` | 50 | Opus/OSCE Python ML tooling, torch/torchaudio compatibility |
| `sources/ps5-mihawkgen/train/requirements.txt` | 41 | MihawkGen model trainer, torch/Pillow; exact pinned upstream source tree |
| `sources/eden-cpm/boost/boost-1.90.0/libs/openmethod/doc/package-lock.json` | 8 | Boost.OpenMethod documentation npm lock |
| `sources/eden-cpm/boost/boost-1.90.0/libs/url/doc/package-lock.json` | 8 | Boost.URL documentation npm lock |
| `sources/opengl-mesa-26-2/bin/ci/test/requirements.txt` | 5 | Mesa upstream developer CI test tools |
| `sources/ps5-mesa/bin/ci/test/requirements.txt` | 5 | Mesa PS5 upstream developer CI test tools |
| `sources/opengl-mesa-26-2/bin/pick/requirements.txt` | 1 | Mesa interactive pick utility |
| `sources/ps5-mesa/bin/pick/requirements.txt` | 1 | PS5 Mesa interactive pick utility |
| **Total in screenshot** | **173** | **Nine source manifests** |

[Read-only Code Scanning run #42844544589](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42844544589) independently verified **28 open Code Scanning alerts**, including **1 critical, 12 high and 15 medium**, all in upstream snapshots. **173 + 28 = 201**, consistent with the reported combined GitHub UI figure, though the UI might represent distinct categories/timeframes and individual Dependabot alert IDs remain unavailable through the connected API (`HTTP 403`). Avoid double-counting across future changes or claiming confirmed GitHub individual-alert closures.

## Review / repair strategy

1. **Do not remove Dependabot warnings by hiding source snapshots or changing their manifest hashes.** Original third-party source trees stay under `sources/`; maintained porting changes stay under `encore-active/`. Preserve licences/model bytes.
2. **Doc-only npm locks (70 alerts):** Boost.MSM 54, OpenMethod 8, URL 8. Rebuild each lock with its upstream docs toolchain and clean `npm ci`, check Antora docs generation and lock integrity, then approve an explicit source-overlay/provenance update instead of hand-patching `package-lock.json` in place. Note earlier Boost doc security overlays have already been assessed in [dependabot triage](DEPENDABOT_TRIAGE_2026-11-10.md), so do not overwrite those digests without recomputation and validation.
3. **ML tooling (91 alerts):** Opus/OSCE 50 and MihawkGen trainer 41. Retained `torchaudio==2.0.2` cannot be paired blindly with a proposed `torch==2.15.0`; MihawkGen Torch/Pillow upgrade must be trainer-tested on Python 3.12 with actual model import and retain the original pinned network SHA-256. Neither a GHSA match nor a green host test proves the trainer is shipped in PS5 runtime.
4. **Mesa test/pick tooling (12 alerts):** two CI manifests at 5 each and two pick manifests at 1 each. Test matching `pytest`/`pytest-asyncio`, filelock 4.x and urwid 4.x in isolated environments, `python -m pip check`, real test-runner and interactive pick flows. Keep PS5 Mesa's upstream tree provenance distinct from maintained Vulkan overrides.
5. **Per-alert closure:** use an account/token with **Dependabot alerts: read** to retrieve real alert IDs, severities and fixed-version floors. Test each compatible pinned upgrade, capture source tree digest and run relevant host CI, then verify changes in GitHub before treating an alert as resolved. No bulk dismissal or silent graph suppression.

## Issue-tracker cleanup

[Vendor issue #22](https://github.com/niakw/Prospero.Eden-Encore-vendor/issues/22) served as initial triage for grouped PRs. It is closed as **not planned as a separate issue**, because this versioned inventory and GitHub Security alerts are now the tracking source; **the underlying outstanding alerts and version-compatibility risks are not fixed or dismissed**. [PR #26](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/26) was closed unmerged after failing the pinned source-tree validation.

## Build isolation

The first [P5 native build #42846319389](https://github.com/niakw/ProsperoEden-Encore/actions/runs/42846319389) uses engine `6fcfe545` and its historically green vendor pin `0b026507`. The vendor's new security documentation is **not** included in that engine checkout unless a future, separately validated gitlink update explicitly adopts it.
