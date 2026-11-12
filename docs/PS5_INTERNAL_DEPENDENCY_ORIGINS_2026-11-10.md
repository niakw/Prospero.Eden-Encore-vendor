# Internal PS5 dependency source provenance and directory policy

This vendor repository uses **two deliberately different, non-interchangeable
layers**: `sources/` is for upstream source trees pinned to exact revisions,
while `encore-active/` is **only** for our maintained, adapted or overridden
runtime files. Do not import unchanged dependencies under `encore-active/`.
If a third-party component requires a port or patch, maintain the changed files
as a distinct Encore-owned override and leave its upstream source snapshot
untouched under `sources/`.

These three imported projects are ordinary tracked Git files under `sources/`,
not additional Git submodules. There is only the parent `vendor` Gitlink.
This migration does not establish native PS5 device compatibility.

| Source path inside vendor | Original upstream repository | Exact upstream commit | Exact vendor Git tree |
| --- | --- | --- | --- |
| `sources/ps5-mihawkgen/` | https://github.com/mihawk-99/PS5_MihawkGen | `c57232a9f16bcb6df5fe07c9bb631518f2b1cc0e` | `aad67f4bd28c040db7aa0d78cfd3d0de85cfba7a` (training-only Pillow security delta; original `e3b42f5b89775469bec04795b93f048b74aff207`) |
| `sources/ps5-fsr3-optical-flow/` | https://github.com/Swordpdf/ps5-framegen | `64362101757cdd6f7df9eeb984cb53f03f1fbfca` | `85e34d638cd9e8b211e08fb19d1a57a588bb8d76` |
| `sources/libwebp/` | https://github.com/webmproject/libwebp | `4fa21912338357f89e4fd51cf2368325b59e9bd9` (v1.6.0) | `3a7830b9713d9187cf1211862d4571ff58b0d507` |

A targeted vendor security change updates only `sources/ps5-mihawkgen/train/requirements.txt` from `pillow==12.3.0` to `pillow==13.0.0`. The original upstream Git tree and upstream commit remain recorded above. The new vendor tree SHA is checked along with an exact one-file comparison against the untouched upstream baseline; no compiled renderer code, licensed assets, model weights, or GPU kernels are rewritten.\n\nMihawkGen retains its GPL/MIT, distillation and other upstream notices,
including `sources/ps5-mihawkgen/THIRD_PARTY_NOTICES.txt` and its
`framegen/models/network.fgw` with SHA-256
`31ecc7d69af1e9defd62b9abad71bc3430cd8a0a4d2f4eafcabaafa48c34bf6b`.
FSR3 Optical Flow retains `sources/ps5-fsr3-optical-flow/COPYING` and AMD
shader notices. libwebp retains `sources/libwebp/COPYING` and
`sources/libwebp/PATENTS`. libwebp's previous external v1.5.0 Gitlink
was `a4d7a715337ded4451fec90ff8ce79728e04126c`; updating to 1.6.0
is a separate source upgrade that requires independent device validation.

A build or test referring to the *old* `encore-active/PS5_MihawkGen`,
`encore-active/PS5_FSR3OpticalFlow`, or `encore-active/libwebp` paths is
**wrong**. The single authoritative location is the table above. All source
consumers (CMake, packaging, model SHA checks, GLSL/SPIR-V tests, license
installation and documentation) must use the `sources/` paths. Validation:
`python3 -B tools/check-source-tree-layout.py` and parent host qualification.

A parent engine release or full PS5 build must never be launched merely because
this repository's host-only source-integrity checks are green.

## Attribution and current build provenance

[Official Encore full upstream credits](https://github.com/niakw/ProsperoEden-Encore#credits) and [THIRD_PARTY_NOTICES](https://github.com/niakw/ProsperoEden-Encore/blob/main/THIRD_PARTY_NOTICES.md) are authoritative for author/maintainer acknowledgement and licences. The [first P5 build](https://github.com/niakw/ProsperoEden-Encore/actions/runs/42846319389) is based on the previously qualified vendor pin `0b0265079139622b759167c9d10a9a31aba1ea88`, **not** the latest documentation-only vendor main. Do not change the three source tree pins or their model hash without an independently reviewed migration and requalification.
