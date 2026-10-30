# Eden Encore — complete vendored dependency sources

This is the **actual content of our pinned dependencies**, included directly in this Git repository to make auditing, reproducibility and disaster recovery possible even if any upstream repository disappears. All licences, notices and source files that the upstream archives or Git snapshots contain are preserved. See `tools/deps.json` for the exact URLs and pinned commits/checksums. `vendor/sources/` contains full readable upstream source trees at their locked revisions; `vendor/objects/` contains byte-for-byte original source archives, Git packs and binary SDK packages split into **40 MiB or smaller Git-trackable chunks**, with a SHA-256 for every chunk and a SHA-256 over the whole reconstructed object. No Git LFS subscription or separate upstream clone is necessary to possess the complete inputs.

## What is included

| Name in `tools/deps.json` | Here | Type |
| --- | --- | --- |
| eden | `sources/eden/`, `objects/eden/` | Pinned full emulator source plus original tarball |
| ffmpeg | `sources/ffmpeg/`, `objects/ffmpeg/` | Pinned full FFmpeg source plus original tarball |
| fmt | `sources/fmt/`, `objects/fmt/` | Full fmt source plus original tarball |
| zbic | `sources/zbic/`, `objects/zbic/` | Exact Git worktree and source-pack preserving the upstream SHA |
| compiler-rt | `sources/compiler-rt/` | Eight individually SHA-256-pinned LLVM emutls/runtime source files |
| pacbrew | `objects/pacbrew/` | Entire third-party binary PS5 toolchain archive, split into small chunks |
| opengl-source | `sources/opengl-source/`, `objects/opengl-source/` | Exact Git snapshot and source-pack |
| harfbuzz | `sources/harfbuzz/`, `objects/harfbuzz/` | Full HarfBuzz source and tarball |
| boilerplate | `sources/boilerplate/`, `objects/boilerplate/` | Exact native app boilerplate source and Git snapshot |
| ps5-vulkan-tools | `sources/ps5-vulkan-tools/`, `objects/ps5-vulkan-tools/` | Complete Mihawk PS5_Vulkan repository at the build's pin |
| ps5-mesa | `sources/ps5-mesa/`, `objects/ps5-mesa/` | Complete Mihawk Mesa/RADV source at the build's pin |
| ps5-payload-sdk-fork | `sources/ps5-payload-sdk-fork/`, `objects/ps5-payload-sdk-fork/` | Complete Mihawk SDK fork source at the build's pin |
| encore-overrides | `sources/encore-overrides/`, `objects/encore-overrides/` | Full authored catalogue of 8,993 Switch titles, four quality tiers, DualSense auto-controller policy, glyph schema, weekly sync source and exact upstream SHA |
| boilerplate setup inputs | `sources/boilerplate-setup-0/`, `objects/boilerplate-setup-{0,1}/` | zlib 1.3.2 full source and upstream tarball; complete binary PS5 payload SDK v0.42 ZIP |

The `pacbrew` and PS5 SDK ZIPs are *prebuilt external binary toolchains*, not buildable original source by virtue of having a ZIP. Their contents are nevertheless preserved completely. Other library/toolchain upstreams are not silently assumed: if a future bootstrap needs another external artifact, it must be added here and hash-locked.

## Use and verification

```bash
python3 tools/check-vendored-deps.py
PROSPEROEDEN_DEPS_OFFLINE=1 python3 tools/deps.py fetch
PROSPEROEDEN_DEPS_OFFLINE=1 python3 tools/deps.py verify
```

The dependency loader uses checked-in readable source trees for the archive-based inputs, presenting them to existing CMake consumers through isolated mutable `.deps` working copies. It restores the Git dependencies' **original upstream commit identity** from content-hashed chunks, using their shallow Git object pack and exact-SHA shallow boundary, into disposable working checkouts. This is **not** a Git submodule requiring any external service. Packages such as pacbrew and the v0.42 SDK reconstruct from their chunks before extraction. Set `PROSPEROEDEN_DEPS_CACHE` to an empty directory to check that no global cache is needed. The `PROSPEROEDEN_DEPS_OFFLINE=1` flag makes any missing source a hard error, never a network fallback.

The build-time adapters already in `tools/prepare-vulkan-port.py` and `tools/patch-radv-wsi.py` are **still applied to the disposable compiled copies**. They are kept during this first vendor-only migration to avoid destabilizing FC27; they can now be moved in reviewable commits into the readable vendored trees. Merely bundling sources does not prove these rendering fixes work on PS5.

`python3 tools/vendor_all_sources.py capture` refreshes vendor snapshots **only after intentional dependency version updates**; it must not run for routine builds. It never silently alters pinned SHAs. `python3 tools/vendor_all_sources.py verify` and the CI gate detect missing/corrupted source archives. Every upstream copyright notice and license remains governed by its original terms; downstream modifications should retain license headers and attribution.

## Active patched Vulkan fork (R342)

`vendor/encore-active/eden-vulkan/` contains **the 39 complete, human-readable generated Vulkan C++ and header files used by our PS5 renderer**, including R334–R337. CMake now copies these tracked files directly into its mutable build directory; it does not execute `prepare-vulkan-port.py` during normal configuration. Original pinned Eden source remains unchanged at `vendor/sources/eden/` and in the exact original archive. `tools/prepare-vulkan-port.py` remains solely as a compatibility/equivalence oracle while we move the remaining GPU thread/CMake-derived conversions into versioned source files. Validate equivalence using `python3 tools/check-vendored-vulkan-fork.py`.

## Active patched Mesa RADV/VideoOut fork (R343)

`vendor/encore-active/ps5-mesa/` contains the **five full exact C files used for the native PS5 RADV driver**, not tiny strings spliced into third-party source at build time. `tools/build-radv-dependencies.sh` stages Mihawk Mesa normally and then calls `tools/install-vendored-radv.py` to replace the staged compiler translation units with our complete tracked versions before the incremental RADV build. This also tracks VideoOut's lifecycle, refresh fallback, host memory protections and startup diagnostics. The old `tools/patch-radv-wsi.py` is retained as a **test-only source-equivalence oracle**; it no longer patches anything as part of a production PS5 build. `python3 tools/check-vendored-radv-fork.py` confirms all five files are byte-for-byte equal to the old effective build and exercises a clean installer, repeated installer and refusal to clobber an unexpected Mesa source revision.

Keep `vendor/sources/ps5-mesa/` untouched as the exact original SHA checkout for history/comparison. The same applies to original Eden code; only `vendor/encore-active/*` is our maintained PS5-specific fork. Other CMake-derived core/HLE/GPU transformations still need the same migration, and should not be silently declared complete.

## Active native PS5 GPU worker fork (R345)

`vendor/encore-active/eden-gpu/` holds the exact GPU worker sources, a shared GPU thread header and the stop-aware bounded SPSC queue header. CMake selects `gpu_thread_quiet.cpp` for normal PS5 builds or `gpu_thread_dev.cpp` for detailed diagnostics, and copies all three compiler inputs directly into its temporary build directory. The source and tests preserve the 32-command queue, worker shutdown/fences and dev-only clock probes without applying source-text replacements during native configuration. `tools/check-vendored-gpu-thread.py` reconstructs the legacy derived PS5 sources for both profiles and executes the actual new PS5 CMake branch; all copies must match byte-for-byte. Its normalization removes only the previous build-machine-specific absolute include prefixes, replacing them with normal headers available through the existing `headless` compiler include path.

Other Core/HLE/CPU/JIT and selected OpenGL CMake source rewrites still exist, are version-controlled, and are scheduled for the same conversion; the native Vulkan, Mesa and GPU worker portions are now checked-in complete source translations.

## Complete game override database (R346)

Our previous embedded profile header referenced `niakw/encore-overrides@0afb14f3`, while that repository's verified latest source was `4fdb329e`. `vendor/sources/encore-overrides/` now includes **all 8,993 complete per-title JSONs**, `manifest.json`, `general/` settings, `glyphs/`, scripts, provenance and the upstream GPL license. The Git object pack in `vendor/objects/encore-overrides/` restores its original commit offline. `headless/encore_overrides_generated.h` is regenerated from this committed tree, using `VENDOR-ORIGIN.json` as authoritative pin rather than incorrectly reading the parent Eden repository's Git HEAD. The 8,993 titles mostly use one of the shared tiers; exactly **one** has distinct tier parameters (EA SPORTS FC27), so the C++ header intentionally has one special profile. `tools/check-vendored-overrides.py` verifies every game ID, manifest link and tier, the generated binary header, DualSense controls and glyph snapshot. It also ensures the nonexistent `runtime-manifest.json` is not fetched automatically during launcher boot. Manual per-game settings still override authored tiers.

## Complete default Eden backports (R347)

`vendor/encore-active/eden-backports/` contains **all 83 complete files changed or added by 43 shipping backport patches** (firmware services, guest RAM/GPU mapping, JIT/texture caches, network policy, controller/HID, Vulkan buffer synchronization). The ordered original `headless/backports/` patches and `tools/apply-eden-backports.sh` are retained as an independent oracle, not production string rewriting. `tools/prepare-build.sh` installs the exact tracked files via `tools/install-vendored-eden-backports.py`; CI replays the 43 patches on a fresh pinned Eden tree and compares all 83 full files and receipt checksums. Three experimental patches remain deliberately disabled (`DUMMY_THREAD_WAITS`, `ICACHE_COHERENCE`, `SM_HOST_WAIT`) pending game/hardware proof and are applied only on explicit test opt-in. The Vulkan fork is also derived **after** these 43 patches; this catches two previously unsynchronized files, including the `KnownGpuTick` compiler failure.

## Full PS5 OpenGL compiler and graphics dependency closure (R348)

**Do not confuse the Mesa implementations:** our native Vulkan renderer uses Mihawk's full PS5 RADV source (`sources/ps5-mesa/`) while the optional OpenGL renderer uses a separate exact Mesa **26.2.0** release (`sources/opengl-mesa-26-2/`). The OpenGL source's own `dependencies.json` pins seven additional original Git repositories, plus its *own* `ps5-native-app-boilerplate` commit. All mandatory revisions are now explicit in `tools/deps.json` and included here as fully readable trees with complete original commit Git packs in `objects/`. The additional inputs are:

- `opengl-opengnm-psbc` — the PSBC compiler backend and source patch;
- `opengl-opengnm` — GNM graphics runtime;
- `opengl-spirv-headers` and `opengl-vulkan-headers` — shader/API headers;
- `opengl-imgui`, `opengl-nanovg`, `opengl-sokol` — native UI/drawing components;
- `opengl-native-boilerplate` — its independently pinned PS5 toolchain wrapper;
- `opengl-mesa-26-2` — complete 12,812-file Mesa 26.2 source plus exact SHA-verified original release archive.

`tools/stage-vendored-opengl-deps.py` now stages all of these into a **disposable** checkout of PS5 OpenGL, reconstructing only original SHA-pinned Git trees and Mesa archive. It also restores the already preserved SDK v0.42 ZIP and zlib 1.3.2 tarball, pre-extracting the SDK to prevent a hidden bootstrap download. Our `tools/build-opengl-sdk.sh` invokes staging before the upstream OpenGL `fetch-sources.py`. The upstream fetcher independently verifies the patched PSBC Git tree and all declared immutable revisions. An isolated test staged the entire closure with `PROSPEROEDEN_DEPS_OFFLINE=1`, then ran the **unmodified upstream fetcher** with HTTP/HTTPS proxies set to an unreachable loopback and `GIT_ALLOW_PROTOCOL=file`. All required repos and Mesa were verified without external internet access.

`tools/check-vendored-opengl-deps.py` enforces the upstream dependency closure and validates every relevant commit and archive pin from our checked-in manifest. Optional sample-and-validation repositories `sokol-samples` and `VK-GL-CTS` are **not used for production builds** and not silently represented as included. Transitive Eden CPM packages (Boost, Xbyak, httplib, Vulkan headers, etc.), Meson `subprojects/` wraps and platform toolchain downloads require a distinct inventory; full end-to-end airgap compliance must be established beyond this OpenGL staging test.

## Native Eden CPM package-cache closure (R349)

`vendor/sources/eden-cpm/` contains **all 22 package/version source directories actually populated by our pinned PS5 CMake build**, a complete ~1 GiB, 77,011 directly inspectable files. The `VENDOR-CPM-LOCK.json` maps each cached source package to the exact `vendor/sources/eden/cpmfile.json` repo, release tag and SHA-512 pin, plus a content-addressed SHA-256 fingerprint of every file and symlink in the cached source tree. A root `.gitattributes -text` protects normal source paths, while 69 files affected by nested upstream text rules are preserved byte-for-byte in `vendor/objects/eden-cpm-eol-originals.tar.gz` and restored by the source stager. The full ~1 GiB duplication is intentional for independently auditable first-clone offline builds.

CMake's `cpmfile.json` has **42 possible packages** across OSes, tests and binary CI, but only **22 cached source packages** are confirmed from our PS5 build, including full Boost 1.90, FFmpeg, SDL3, Vulkan headers, Sirit, Xbyak, httplib, zstd and all other populated package trees. R349 does not fabricate a source mirror for the remaining 20 unused or unobserved pins. Each entry stays registered in `excluded_unfetched_cpm_pins`, making the residual scope explicit.

`tools/vendor-eden-cpm.py --check` validates the complete source-lock and all package tree SHA-256 checksums against the pinned Eden cpmfile. `tools/vendor-eden-cpm.py --stage /path/to/disposable/eden` copies only missing package-version source directories to the normal `.cache/cpm`, preserving all files and symlinks without accessing remote repos. It runs in `tools/prepare-build.sh` **after** the optional historical CPM cache seed and **before** FFmpeg/CMake configure, so incremental caches are not wiped and every future clean checkout has the original PS5 populated packages available. The `--capture PATH` mode is for intentional source version changes only and rejects any source/tree mismatch.

Original CPM downloaded archive byte streams are generally **not present in the old CPM cache** and therefore not falsely claimed to be included; all original *unpacked source contents* and pinned upstream URLs/hashes are. Some downstream `Meson` wrap contents and tools unrelated to this actual PS5 source snapshot still require a separate complete empty-cache network-denial build qualification.
