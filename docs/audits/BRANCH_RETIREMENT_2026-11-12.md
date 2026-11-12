# Encore vendor branch retirement — 12 November 2026

This ledger is about **branch references**, not certification that all historic research features are active. Original commits remain addressable by immutable archive tags under `archive/2026-11-12/`, with each `/` in the branch name represented as `__`.

The selected production `main` is unchanged by branch retirement; obsolete changes are **not** blindly merged. Code marked `archived-unmerged` remains available for future targeted work and is **not** claimed to compile, boot, or run on PS5.

| Original branch | Frozen Git SHA | Classification | Verification / disposition |
|---|---|---|---|
| `fix/r23-consolidated-backports-manifest-20261112` | `ade23efe2cc034783bf30db4163307805a4f73d8` | **identical** | manifest.json matches vendor/main git blob; no source delta |
| `integration/c00-r457-unified-audio-20261112` | `3afdfd6c2d7aee6a544109d2942cbf0457be523f` | **merged** | vendor #86 merged C00+R457 audio implementation |
| `integration/ns1-r23-e1-20261112` | `b7f8973dff254fcc031793187aafcbbdd15adf9f` | **archived-unmerged** | old 8-commit E1 integration graph superseded by consolidated production vendor; historical workflow/fixture edits retained via tag, not cherry-picked |
| `integration/r457-audio-readiness-real-owner-20261112` | `7bfe0f40362a9929d7e113c51d7a2bf2c331cf81` | **merged** | vendor #85 merged native audio readiness; later #86 unified AudioIn/Out |
| `integration/residual-am-phi-tests-20261112` | `19a27d3f73ea490b4457be4e91b5493f1394b516` | **merged** | vendor #88 merged current source-pinned AM overlay and conditional Phi tests |
| `integration/residual-hid-fw23-tests-20261112` | `bebc822efaf4fe0a387f897894cccb8b795c0795` | **merged** | vendor #87 merged exact FW23 and HID source-contract tests |
| `research/ns1-r23-applet-overlay-zorder-20261111` | `e2b30d2a9169300f481d1d15ba33b02852fc733c` | **superseded** | current window_system.cpp contains overlay-open guard and newer changes; adapted host test merged #88 |
| `research/ns1-r23-attribute-handle-sprites-20261112` | `f84e020ac8b3bbe3e3165573760165863072d452` | **archived-unmerged** | old sprite CBUF/descriptor changes touch shared shader and Vulkan ABI; current selected graph differs; not safe to cherry-pick; source preserved by immutable tag |
| `research/ns1-r23-bindless-phi-20261111` | `21b69b3aaa2f5a1d9b25c2df75e44e6729547841` | **superseded** | main retained newer conditional Phi implementation and same BFS header; re-pinned source-extracted test merged #88 |
| `research/ns1-r23-fw235-ns-commands-20261111` | `307e3f5dbb9c6850005a82598d42bc411b357287` | **identical** | application_manager_interface.cpp/.h git blobs exact match main, FW23 test merged #87 |
| `research/ns1-r23-hid-controller-reconnect-20261111` | `1c02e14088899323daf13a6c1c67d67796eb20bf` | **identical** | emulated_controller.cpp/.h exact match main, HID ASan/UBSan host test merged #87 |
| `research/ns2-r4-safe-volume-slice-20261110` | `d5b31bece4cb637fa3e14a70ac53fe2b423004ac` | **archived-unmerged** | older texture_cache variant is not selected: PS5 R4 volume handling is in selected ns2-astc; do not replace actual owner |
| `research/r457-ns1-audio-unready-memory-20261111` | `0498ddc379154bb05651d8ada87dfe1cbf433b91` | **superseded** | old research changed pinned Eden originals; current vendor #85/#86 moved safeguards to PS5 native source owner without modifying pinned source |

## Safety criteria

The one-shot deletion workflow freezes and checks the branch SHA, verifies that no pull request is open, writes and re-reads each immutable archival tag before deleting the corresponding branch reference. A moved branch aborts rather than losing new work. The build graph and upstream `sources/eden` tree are not rewritten by this administrative change.

**Not a console qualification:** no PS5 NS1/NS2 boot proof is implied. The archived sprite-handle shader research and the older R4 cache variant must not be enabled without a fresh selected-owner and memory/GPU lifetime audit.
