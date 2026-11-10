# Code Scanning inventory — 10 November 2026

**Verified:** 28 open Code Scanning alerts, all in immutable third-party `sources/` snapshots, at [read-only API inventory #42843595702](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42843595702) on vendor commit `c51f12d1953097e8fe8d8e44430eae16fc5e1587`. **Severity:** 1 critical, 12 high, 15 medium. Maintained `encore-active/` and project infrastructure paths: **0 in this snapshot**. These are Code Scanning findings, not a reconciliation of the separate 201-item Security and Quality UI count (which may include Dependabot alerts).

The classification below is a **file path** classification, not proof that a vulnerable script can or cannot execute in an actual PS5 release. No findings were dismissed and no `sources/` files were patched. Confirm actual call/package reachability and an upstream fix before any dismissal or source migration. Keep the engine vendor gitlink from the completed P1/P2 host exit unchanged until separately tested.

## Priority triage

- **Critical [#14](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/14)**: command-line injection alert in Boost URL documentation's `cpp-reference.js`. Determine whether this documentation generator runs in CI/packaging and whether command input is trusted; do not blindly claim false positive.
- **High #17–#23**: seven regex complexity alerts in Boost MPL's `preprocessed/pp.py`. Inspect invocation and inputs when generating/refreshing sources.
- **High #15–#16**: Boost Litre temporary-file handling; verify script usage and unsafe symlink/external path exposure.
- **High #1–#3**: DOM XSS warnings in Boost and VulkanMemoryAllocator documentation JavaScript; verify whether generated documentation is served to users or included in distributables.
- **Medium remaining**: developer-only test Python scripts, documentation JS and external-script loading; verify usage and trust boundary separately.

## All open alerts — exact GitHub IDs

| ID | Severity | CodeQL rule | Exact upstream path |
| ---: | --- | --- | --- |
| [#1](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/1) | high | `js/xss-through-dom` | `sources/eden-cpm/boost/boost-1.90.0/libs/numeric/ublas/doc/js/jquery.toc-gw.js` |
| [#2](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/2) | high | `js/xss-through-dom` | `sources/eden-cpm/boost/boost-1.90.0/libs/numeric/ublas/doc/js/jquery.toc-gw.js` |
| [#3](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/3) | high | `js/xss-through-dom` | `sources/eden-cpm/vulkanmemoryallocator/v3.3.0/docs/html/dynsections.js` |
| [#4](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/4) | medium | `js/functionality-from-untrusted-source` | `sources/eden-cpm/boost/boost-1.90.0/libs/hana/benchmark/chart.html` |
| [#5](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/5) | medium | `js/functionality-from-untrusted-source` | `sources/eden-cpm/boost/boost-1.90.0/libs/numeric/ublas/doc/container_concept.html` |
| [#6](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/6) | medium | `js/functionality-from-untrusted-source` | `sources/eden-cpm/boost/boost-1.90.0/libs/numeric/ublas/doc/index.html` |
| [#7](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/7) | medium | `js/functionality-from-untrusted-source` | `sources/eden-cpm/boost/boost-1.90.0/libs/numeric/ublas/doc/tensor/extents.html` |
| [#8](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/8) | medium | `js/functionality-from-untrusted-source` | `sources/eden-cpm/boost/boost-1.90.0/libs/numeric/ublas/doc/tensor/strides.html` |
| [#9](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/9) | medium | `js/functionality-from-untrusted-source` | `sources/eden-cpm/boost/boost-1.90.0/libs/numeric/ublas/doc/tensor/tensor_index.html` |
| [#10](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/10) | medium | `js/functionality-from-untrusted-source` | `sources/eden-cpm/boost/boost-1.90.0/libs/numeric/ublas/doc/tensor.html` |
| [#11](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/11) | medium | `js/shell-command-injection-from-environment` | `sources/eden-cpm/boost/boost-1.90.0/libs/url/doc/generate-files.mjs` |
| [#12](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/12) | medium | `js/shell-command-injection-from-environment` | `sources/eden-cpm/boost/boost-1.90.0/libs/url/doc/generate-files.mjs` |
| [#13](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/13) | medium | `js/shell-command-injection-from-environment` | `sources/eden-cpm/boost/boost-1.90.0/libs/url/doc/generate-files.mjs` |
| [#14](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/14) | critical | `js/command-line-injection` | `sources/eden-cpm/boost/boost-1.90.0/libs/url/doc/lib/cpp-reference.js` |
| [#15](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/15) | high | `py/insecure-temporary-file` | `sources/eden-cpm/boost/boost-1.90.0/tools/litre/cplusplus.py` |
| [#16](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/16) | high | `py/insecure-temporary-file` | `sources/eden-cpm/boost/boost-1.90.0/tools/litre/cplusplus.py` |
| [#17](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/17) | high | `py/redos` | `sources/eden-cpm/boost/boost-1.90.0/libs/mpl/preprocessed/pp.py` |
| [#18](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/18) | high | `py/redos` | `sources/eden-cpm/boost/boost-1.90.0/libs/mpl/preprocessed/pp.py` |
| [#19](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/19) | high | `py/redos` | `sources/eden-cpm/boost/boost-1.90.0/libs/mpl/preprocessed/pp.py` |
| [#20](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/20) | high | `py/redos` | `sources/eden-cpm/boost/boost-1.90.0/libs/mpl/preprocessed/pp.py` |
| [#21](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/21) | high | `py/redos` | `sources/eden-cpm/boost/boost-1.90.0/libs/mpl/preprocessed/pp.py` |
| [#22](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/22) | high | `py/redos` | `sources/eden-cpm/boost/boost-1.90.0/libs/mpl/preprocessed/pp.py` |
| [#23](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/23) | high | `py/redos` | `sources/eden-cpm/boost/boost-1.90.0/libs/mpl/preprocessed/pp.py` |
| [#24](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/24) | medium | `py/overly-large-range` | `sources/harfbuzz/test/shape/hb_test_tools.py` |
| [#25](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/25) | medium | `py/overly-large-range` | `sources/harfbuzz/test/shape/hb_test_tools.py` |
| [#26](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/26) | medium | `py/overly-large-range` | `sources/opengl-mesa-26-2/bin/nir-test-runner.py` |
| [#27](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/27) | medium | `py/overly-large-range` | `sources/ps5-mesa/bin/nir-test-runner.py` |
| [#28](https://github.com/niakw/Prospero.Eden-Encore-vendor/security/code-scanning/28) | medium | `py/overly-large-range` | `sources/eden-cpm/boost/boost-1.90.0/tools/quickbook/test/python/run_tests.py` |

## Follow-through to actually resolve alerts

1. For each ID, identify the real executable/tool and the engine/vendor CI or PS5 packaging entry point. Preserve a negative reachability proof if it is not used.
2. For reachable code, source-fix in the upstream project or an explicitly approved maintained override; add a reproduction/host test and recheck preserved source pins and licences.
3. For unreachable third-party files, collect objective build/package exclusion evidence **before** considering a narrowly justified GitHub dismissal; do not bulk-dismiss on path prefix alone.
4. Re-run the [Code Scanning inventory](../.github/workflows/vendor-code-scanning-inventory.yml) after default CodeQL completes. Reconcile the same ID, latest state and SHA. **Verified closed findings during this audit: zero.**

The separate [Dependency issue #22](https://github.com/niakw/Prospero.Eden-Encore-vendor/issues/22) tracks incompatibilities and Dependabot upgrades and must not be treated as resolved by closing grouped [PR #26](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/26).
