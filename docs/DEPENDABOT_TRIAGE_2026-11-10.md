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
