# Security policy — Eden Encore vendor

## Maintained code

The `main` branch contains the vendor snapshot used by Eden Encore. Prior
historical branches and old snapshots are **not** separately supported. No
"5.1.x" or "4.0.x" support promise applies to this repository.

## Reporting a vulnerability

Please use the repository's GitHub **Security** area and private vulnerability
reporting when available. If that option is unavailable, contact the repository
maintainer privately before publishing proof-of-concept code, credentials or
exploit details in a public issue.

A useful report identifies the affected source path, exact vendor commit,
reachability in the PS5 build, reproduction steps, expected versus observed
behavior and any publicly known CVE or GHSA.

## Scope and remediation

Third-party source under `sources/` is vendored and may have its own upstream
security advisories; its presence alone does not demonstrate reachability in
the actual PS5 artifact. We investigate both dependency versions and whether
the maintained `encore-active/` / PS5 runtime actually compiles or calls the
affected code.

No historical import workflow may overwrite `main`. Security fixes must be
reviewed in `main`, with the engine's `vendor` gitlink updated to the exact
new SHA and qualified by relevant host checks. Secret leakage requires
credential revocation/rotation, not merely deleting the exposed file.
