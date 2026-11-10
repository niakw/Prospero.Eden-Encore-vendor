# Dependabot documentation-toolchain triage — 10 November 2026

**Scope:** `niakw/Prospero.Eden-Encore-vendor` PRs #12–#21. This audit addresses vendored third-party documentation/CI requirements; **not** an endorsement of changing the P1/P2 CPU, GPU, compiler, native PS5 runtime or guest networking.

## Accepted

- **#15 (merged, squash `bc2e100f`)** consolidates Antora and npm lockfile updates across Boost `dynamic_bitset`, `openmethod`, `redis`, `unordered` and `url` documentation. The three direct dependency vulnerabilities covered by the individual minimatch, brace-expansion and fast-copy PRs are either updated (3.1.5, 1.1.21, 3.1.0) or have no remaining lockfile package entry in the consolidated tree.
- **#12 (superseded):** #15 carries its group-wide updates. The one higher `es-object-atoms` package from #12, **1.1.2 versus 1.1.1**, was incorporated separately in the Redis docs lockfile without replacing the merged tree.
- **#13/#14/#16/#17/#18/#19/#20 (superseded):** individual npm package locks are covered by #15; do not apply old patch heads on top of a newer resolved lockfile.
- **#21 (partially incorporated):** doc-only `wheel 0.46.2`, `mkdocs-material 9.7.7` in both relevant reference sites, and the existing HarfBuzz CI hashed `setuptools 84.1.0` pin/hashes have been copied exactly from the Dependabot proposal.

## Rejected or deferred, with explicit compatibility risk

**Do not merge #21 in full.** Its bundled versions may not be mutually compatible and cannot be safely qualified as an isolated dependency refresh:

- Opus `torch 2.13.0` would coexist with `torchaudio 2.0.2`; the model-toolchain pair must be updated and tested together.
- Mesa `pytest 9.0.3` would coexist with `pytest-asyncio 0.21.0`; require an environment-matrix/test compatibility check before changing these test tools. Major `filelock 4.2.1` must likewise be qualified in that environment.
- `urwid 4.0.7` (Mesa interactive pick tool) is a separate major-version change and requires its tool-path test.
- Opus `gitpython` and `tqdm` dependency changes are not performance or security proof; defer with the Opus model-toolchain environment.

**Remaining risks:** This review neither establishes absence of all Dependabot/security alerts nor proves that the vendored tool snapshots match latest upstream releases. Keep a separate explicit test or future dependency update for deferred paths rather than forcing incompatible pins. No PS5 build, console runtime or P1/P2 exit proof was performed.

## Submodule-boundary integration

The separate `maintenance/internalize-ps5-deps-20261110` branch is responsible for replacing external MihawkGen/FSR3/libwebp Gitlinks with internally pinned, attributed sources. This documentation-dependency triage does not merge or rewrite that source migration, and does not move the main repository's vendor gitlink.

## Fully pinned CPM snapshots after the documentation-only security fixes

The original Eden CPM package pins and upstream archive SHA-512 values are
**unchanged**. The two checked-in **post-security-overlay** source snapshots
have modified documentation dependency locks only, so their whole-tree
content hashes must be updated to reflect the exact files Git now ships:

| Package | Original source snapshot SHA-256 | Reviewed post-overlay SHA-256 | Files | Reviewed size |
| --- | --- | --- | ---: | ---: |
| Boost 1.90.0 | `20519b3bdcaaa64ca0eb3d252b9018af513343ffc994431a0128cae2c1c0a9e8` | `02027393412e03a79a37eb7620fdb382f7e8374dddafa78606eed8e80534fd83` | 59798 | 550701340 |
| nlohmann_json v3.12.0 | `e827f18c1d00b705d7231aafd64d8ccd1bc7f7a4e2dd0cf174e19c0d053d6268` | `46ec167af23320dcdf32da1a9df0f3f4e5ca6cdbeee5901e6e5898257f6c6a82` | 1164 | 15958749 |

**Provenance:** the 9 Boost documentation `package.json`/`package-lock.json`
files from Dependabot #15, plus the reviewed Redis lockfile
`es-object-atoms 1.1.2` correction, and nlohmann_json
`docs/mkdocs/requirements.txt` from #21. Original upstream source pin
remains recorded in `VENDOR-CPM-LOCK.json`; this does *not* assert the
post-overlay tree equals the unmodified upstream archive. Any later change to
the full source snapshot still fails `tools/vendor-eden-cpm.py --check`.

The byte counts and hashes above are taken from the real PS5-source
checkout's complete deterministic CPM tree digest (GitHub Actions
`validate` job on Encore migration commit `acecd3c`), **not guessed**.
No C++ emulator owner, compiler, shader, model or runtime behaviour was
modified by these documentation/CI lockfile updates.

## Grouped PR #26: rejected (10 November 2026)

**Disposition:** [PR #26](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/26) was closed without merge, with a detailed CI explanation. Its [Security & Quality run #42735747880](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42735747880) failed `Verify strict upstream sources / Encore override boundary`: checked-in `sources/ps5-mihawkgen` changes pinned original Git tree `e3b42f5b89775469bec04795b93f048b74aff207` to unapproved `7a88e18be693d90077ac678182acc8758d924b13`.

The six changed upstream requirements files bundle distinct unqualified upgrades:

- Opus OSCE: `torch 2.0.1 → 2.15.0` with retained `torchaudio 2.0.2`, plus `gitpython 3.1.41 → 3.2.1`, `tqdm 4.66.1 → 4.70.1`.
- Two Mesa test stacks: `pytest 7.4.2 → 9.2.0` with retained `pytest-asyncio 0.21.0`, and `filelock 3.12.4 → 4.2.1`.
- Two Mesa interactive tools: `urwid 3.0.3 → 4.0.7`.
- MihawkGen Python 3.12 trainer: `torch 2.14.1 → 2.15.0` and `pillow 12.3.0 → 13.0.0`.

Keep [issue #22](https://github.com/niakw/Prospero.Eden-Encore-vendor/issues/22) **open**. Qualify compatible version pairs in isolated environments with `python -m pip check` and meaningful trainer/Mesa/Opus smoke tests. Approval additionally requires explicit source-pin/provenance migration, original model digest retention and relevant host CI. Closing PR #26 does not fix GitHub Dependabot advisories or PS5 runtime issues.

**Alert API access check:** [read-only run #42844544589](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42844544589) returned `HTTP 403` from the GitHub Dependabot alerts endpoint. The exact open Dependabot alert IDs, package severities and total count require a token with the separate **Dependabot alerts: read** permission. This does not invalidate the independently verified 28 Code Scanning alerts, and **no Dependabot alerts have been reported closed** by this audit.

## Screenshot inventory reconciliation (10 November)

The nine listed upstream manifests account for **173 reported Dependabot alerts** (54+50+41+8+8+5+5+1+1). Together with the separately API-confirmed **28 Code Scanning alerts**, they match the reported Security & Quality aggregate **201**. **No individual Dependabot alert state was readable**, because GitHub returns HTTP 403 to the workflow's token. The issue tracker is not a substitute for exact alerts. See [manifest-level backlog](VENDOR_DEPENDABOT_MANIFEST_INVENTORY_2026-11-10.md) for full paths and compatible-upgrade acceptance tests. The initial issue #22 can be closed administratively without asserting the vulnerabilities have been fixed.
