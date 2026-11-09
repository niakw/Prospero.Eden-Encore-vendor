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
