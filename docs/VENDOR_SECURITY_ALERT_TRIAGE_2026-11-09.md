# Vendor security alert triage — 2026-11-09

**Status: open; not a claim that repository security alerts have been resolved.**
This is an evidence-based queue, not a substitute for GitHub's actual Dependabot
alert IDs, Code Scanning findings or proof of runtime reachability.

## Boundaries

- Vendor `main` only. Do not modify the engine gitlink, PS5 packaging, P1/P2
  shader, IPC, GPU, or memory owners while the parallel roadmap work is active.
- `sources/` contains immutable upstream snapshots. Do not perform automated
  bulk upgrades inside those trees; preserve source pins and compatibility.
- `encore-active/` contains maintained overrides. A file in an upstream snapshot
  is not by itself proof that it executes in the PS5 artifact.
- Do **not** dismiss a GitHub security alert merely because its path is vendored.
  Affected components must first be traced through the actual build/runtime graph.

## Upstream advisories confirmed against pinned *manifest versions*

These are confirmed **version matches**, not verified GitHub alert numbers and
not proof that the affected Python tools are installed in any release.

| Manifest path / pinned package | Confirmed upstream issue | Fixed-version floor | Build/reachability |
| --- | --- | --- | --- |
| `sources/eden-cpm/opus/a3f0ec02b3/dnn/torch/osce/requirements.txt` — `torch==2.0.1` | CVE-2025-32434, GHSA-53q9-r3pm-6pq6 (unsafe `torch.load`) and CVE-2026-24747, GHSA-63cw-57p8-fm3p | 2.10.0 to address both named advisories | **Unknown**, OSCE Python tooling; verify invocation before changing the upstream snapshot |
| same file — `gitpython==3.1.41` | CVE-2026-114145, GHSA-83vg-56qc-22m7 (unsafe submodule path handling) | 3.2.1 | **Unknown**, verify use on untrusted Git repositories |
| `sources/eden-cpm/nlohmann_json/v3.12.0/docs/mkdocs/requirements.txt` — `wheel==0.45.1` | CVE-2026-24049, GHSA-8rrh-rw8j-w5fx (`wheel unpack` path traversal affecting file permissions) | 0.46.2 | **Unknown**, documentation tool; verify whether `wheel unpack` is called |

References (upstream):
- https://github.com/advisories/GHSA-53q9-r3pm-6pq6
- https://github.com/pytorch/pytorch/security/advisories/GHSA-63cw-57p8-fm3p
- https://github.com/advisories/GHSA-83vg-56qc-22m7
- https://github.com/advisories/GHSA-8rrh-rw8j-w5fx

The archived `torchaudio==2.0.2` accompanies `torch==2.0.1`. A security update
of PyTorch cannot be merged in isolation without resolving and validating the
paired audio stack and Python compatibility.

## Dependabot

- PR #11 mixed upgrades of **six** third-party source-snapshot manifests,
  including a PyTorch/Torchaudio version mismatch and a pytest/pytest-asyncio
  compatibility risk. It was closed without merging.
- `.github/dependabot.yml` manages only maintained GitHub Actions updates.
  This does **not** suppress Dependabot vulnerability alerts for old manifests.
- Inventory each actual alert by GHSA/CVE, manifest path, fixed version, affected
  execution path and test evidence. Do not mark as fixed or dismiss without it.

## Code Scanning

- GitHub's Default Setup was running for several languages, but the checks
  inspected on `5e76febbd3ab1a4f8f5e4256508d73fbceea8963` did not list
  a C/C++ analysis job. **C/C++ coverage has not been demonstrated.**
- `.github/codeql/active-scope.yml` exists, but merely committing this file
  does **not** prove the configuration is applied to Default Setup.
  Confirm the effective configuration in GitHub's code-scanning tool-status page.
- Do not add a competing advanced CodeQL SARIF upload while Default Setup is
  enabled. If C/C++ is not selected, configure it in Default Setup and verify
  the resulting analysis job before claiming coverage.
- Existing Code Scanning alerts must be handled per finding, preserving
  the genuine owner and checking exploitability rather than bulk dismissal.

## Preventive host CI

The maintained security workflow now has read-only token permissions, SHA-pinned
external actions, checkout without persisted credentials, sparse inputs, and a
separate workflow-policy audit. Its successful run is a **preventive host check**,
not a security alert resolution.

## Completion criteria

1. Obtain the complete current GitHub Dependabot and Code Scanning alert inventory
   (IDs, severities, rules, paths, dismissals and timestamps).
2. Separate active execution dependencies from documentation/training tools and
   immutable upstream snapshots using engine build-selection evidence.
3. Apply compatible fixes only to maintained paths or approved source-pin updates,
   with focused host validation and no loss of NS1 behavior.
4. Verify CodeQL C/C++ coverage and re-scan at a single exact vendor SHA.
5. Reconcile every finding with a verified fixed/dismissed state and explicit
   rationale. **Until then, alert count closed: not verified.**


## Live OSV inventory (qualified host result, 9 November 2026)

Read-only scan on vendor commit `8402e3bdd1d541986c3ebf04e7012b12fd9fe3cc`:
https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42630123903

- Six selected upstream Python requirements manifests were inspected.
- **31 exact package/version pins** were queried against the OSV API.
- **Nine affected package/version combinations** returned one or more advisories.
- **112 distinct OSV advisory identifiers** were returned; this is **not** a
  count of unique vulnerabilities or GitHub Dependabot alerts, because OSV may
  expose multiple identifiers/aliases for the same issue.
- Two `aiohttp==3.13.*` constraints were explicitly **excluded from exact-version
  lookup** and reported as nonexact, not falsely declared safe.
- No pinned manifest or runtime source was changed. The scan alone resolves
  **zero verified GitHub alerts** and cannot establish reachability in PS5.

Affected exact package/version pairs reported by OSV: `gitpython==3.1.41`,
`torch==2.0.1`, `filelock==3.12.4`, `setuptools==80.9.0`,
`mkdocs-material==9.5.49`, `mkdocs-material==9.6.11`, `pytest==7.4.2`,
`tqdm==4.66.1`, `wheel==0.45.1`.

The audit runs independently of the P1/P2 host gates and does not install,
update or execute these upstream Python dependencies.

## Follow-up: 10 November 2026 — 201 security/quality findings reported

The user reported **201** findings in the vendor Security and Quality UI. Their exact GitHub alert IDs, state, severity and classification have **not yet been independently reconciled**; 201 is not a verified number of exploitable PS5 bugs or of distinct CVEs. Direct repository-file access alone cannot read the authenticated Code Scanning alerts collection. Do not claim any of these alerts fixed or dismissed without an actual rescan and alert ID.

A separate [read-only Code Scanning inventory workflow](../.github/workflows/vendor-code-scanning-inventory.yml) now requests `security-events: read` and enumerates paginated open alerts. It emits exact IDs, rules, severities, locations and URLs to CI logs and groups rules in the job summary. This **does not** add a competing CodeQL/SARIF upload, change sources, or dismiss alerts; failure to access the API is an explicit failure, not a fictitious zero.

**Triage:** prioritize maintained files in `encore-active/`, then `.github/`/`tools/` infrastructure. Investigate third-party `sources/` and `objects/` findings separately against the actual engine build selection and reachability. Ownership paths alone do not justify dismissal; an upstream security issue needs a reviewed source upgrade plus its tree/provenance changes and regression tests. Preserve NS1 and the green P1/P2 host exit while doing this.

The grouped [Dependabot PR #26](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/26) was rejected without merge after [source-layout CI failure](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42735747880). Its incompatible/unqualified changes remain under [issue #22](https://github.com/niakw/Prospero.Eden-Encore-vendor/issues/22), not a resolved security alert.

## Reconciled Code Scanning subset (inventory #42843595702)

The GitHub API inventory returned **28 open Code Scanning findings**: **1 critical, 12 high, 15 medium**, all in `sources/` and **0 in `encore-active/`** for the scanned snapshot. 23 were in the Boost source snapshot, 2 HarfBuzz test scripts, 2 Mesa NIR test runners, and 1 VulkanMemoryAllocator documentation script. Full IDs, severities, affected paths and next steps: [qualified Code Scanning inventory](VENDOR_CODE_SCANNING_FINDINGS_2026-11-10.md). **No findings have been proved fixed/closed.** These 28 are only the verified Code Scanning subset, not the full 201 Security and Quality UI figure.

## Authenticated Dependabot inventory access — confirmed blocker

[Inventory run #42844544589](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42844544589) reconfirmed 28 Code Scanning findings; the API request for `/repos/niakw/Prospero.Eden-Encore-vendor/dependabot/alerts` returned **HTTP 403** with the workflow's available token. GitHub Code Scanning's `security-events: read` does **not** supply this separate Dependabot-alerts access. **There is no verified Dependabot alert count** from this audit, so do not subtract 28 from the user-reported 201 or claim remaining items resolved.

An authorized maintainer with a separately configured GitHub token granted **Dependabot alerts: read** can independently retrieve the actual open list with:

```bash
gh api --paginate 'repos/niakw/Prospero.Eden-Encore-vendor/dependabot/alerts?state=open&per_page=100' \
  --jq '.[] | {number, state, html_url, manifest_path, package: .dependency.package.name, severity: .security_advisory.severity, ghsa: .security_advisory.ghsa_id}'
```

Do not paste tokens into tickets, logs or documentation. Once verified alert IDs exist, classify them by exact manifest/version and PS5 build reachability, qualify compatible dependency upgrades in host isolation, then confirm GitHub's changed alert states. **At present no Dependabot alerts are claimed fixed.**
