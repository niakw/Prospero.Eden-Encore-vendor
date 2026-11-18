# FC27 NS1 — partial vertex-attribute pipeline key research — 2026-11-18

## Symptom and scope

FC27 NS1 can show severe first-visit hitches which reduce after the scene has been exercised. Encore's PS5 Vulkan path deliberately waits for an unbuilt graphics pipeline rather than skipping the draw, because skipped first-use draws can produce missing HUD/text. The R457-versus-current host replay proves that `RunBuild()` removes one worker-queue dependency, but it does **not** remove the driver pipeline compilation itself.

This lot now contains a **source candidate** for PS5 NS1 only. It still makes no PS5 performance claim, does not change NS2/Ampere handling, and does not migrate the disk cache.

## Upstream-first evidence

- Lichii PR #408, commit `e3d61078810c482fed7ecd1958487df63e0e26fe` (*key vertex attribute types by what the program reads*), reports that dynamic vertex-input keys included all 32 attribute register types even when the vertex shader did not read them. The same shaders could therefore fan out to as many as 20 pipeline keys. Its focused policy keeps only the two-bit types for attributes present in the vertex program's input map, and stays conservative when VertexA is active.
- EglantinePS5 PR #105, merged as `4957dcc6e2ae327e5a8d435bb457a961068ad241` and console-tested, independently shows the PS5 consequence of a closely related key/prebuild mismatch: 78–81 pipelines were rebuilt mid-game for about 2.2 s of GPU-thread stalls; accepting the correct cached attributeless key reduced those mid-game rebuilds to zero in that test.
- Lichii PR #2102 targets a different layer: a waiter can claim the driver-stage build after CPU translation rather than waiting behind the queue. Encore's existing `RunBuild()` replay covers that class; it does not address redundant distinct keys.
- Broad shader-cache v2 migration, eviction changes and warm-up systems from Lichii #408/#579 are deliberately **not** imported here.

## Encore selected-source match

The selected `encore-active/eden-vulkan/fixed_pipeline_state.cpp` currently computes `attribute_types` from every non-constant attribute register when dynamic vertex input is available. The selected `vk_pipeline_cache_cost.cpp` already contains `EdenRecordAttributeless`, which uses `Shader::Info::loads.Generic(index)`, but only optimizes the special case where **no** generic attribute is read.

The selected shader compiler already records the safe per-attribute information:
- direct `GetAttribute` / `GetAttributeU32` set `Info::loads`;
- indexed inputs are expanded from `ProgramHeader::vtg.InputGeneric(index)`.

Therefore the missing case is specifically a shader that reads **some but not all** generic attributes.

## Host negative control

`tests/fc27_vertex_attribute_key_mask_host.py` authenticates the selected source structure and models the key rule.

It proves:
- changing an unread attribute register currently changes the raw dynamic vertex-input key;
- applying the upstream two-bit read mask collapses those redundant keys;
- changing a **read** attribute type still changes the key;
- VertexA+VertexB remains conservative and unmasked;
- a fully-read shader keeps the old key exactly;
- the zero-read case becomes zero, matching Encore's existing attributeless concept.

This is a mechanism witness only. It does not establish that FC27's historical 858 >50 ms frames are caused by these keys.

## Implemented source candidate

The selected PS5 Vulkan owner now keeps a raw copy of dynamic vertex `attribute_types` per `PipelineCache`. Before a new `FixedPipelineState::Refresh`, it restores that raw value so a mask from one shader cannot leak into the next key. After Refresh it captures the raw value again.

A builder that has translated VertexB publishes a two-bit mask derived from the same `Shader::Info::loads.Generic(index)` predicate already used by the selected graphics-pipeline builder to decide which vertex attributes are actually emitted. The GPU-thread lookup uses a bounded thread-local memo backed by a synchronized map capped at 8192 shader identities; once full, unknown identities remain on the historical unmasked path.

Admission is deliberately narrow:
1. `PS5_NATIVE` only.
2. Authenticated NS2/Ampere (`native == true`) explicitly clears the raw-state validity and never consumes this mask.
3. VertexA+VertexB stays unmasked.
4. An unknown shader mask leaves the historical key unchanged.
5. If the same shader identity ever publishes conflicting metadata, the stored mask widens permanently to all 64 type bits, disabling this optimization for that identity.
6. The metadata map is capped at 8192 identities; overflow is fail-open to the historical unmasked key.
7. The existing zero-attribute/static-input optimization remains intact and separate.
8. No on-disk cache migration is performed. Existing unmasked records may miss once and remain valid; cache-format/version is unchanged.

The host witness keeps the historical negative control and now also verifies source ordering (raw restore → Refresh → raw capture → mask), NS2 exclusion, VertexA conservatism, used/unread counterexamples, and the collision fail-safe.

## Validation still required

Before any merge as a performance fix:
- run the focused vendor CI and normal source/security checks;
- pair the vendor candidate to an engine branch and run the full host preflight/native owner gates;
- inspect warnings and exact selected owner hashes;
- **do not** claim reduced FC27 stutter until PS5 A/B compares pipeline creations, held-draw time, frame-time distribution, Options latency and HUD freshness at equivalent cache state.
