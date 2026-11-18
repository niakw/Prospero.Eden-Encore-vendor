# FC27 NS1 — partial vertex-attribute pipeline key research — 2026-11-18

## Symptom and scope

FC27 NS1 can show severe first-visit hitches which reduce after the scene has been exercised. Encore's PS5 Vulkan path deliberately waits for an unbuilt graphics pipeline rather than skipping the draw, because skipped first-use draws can produce missing HUD/text. The R457-versus-current host replay proves that `RunBuild()` removes one worker-queue dependency, but it does **not** remove the driver pipeline compilation itself.

This research lot changes no runtime behavior and makes no PS5 performance claim.

## Upstream-first evidence

- Lichii PR #408, *Ocarina of Time: fix stutter from pipeline rebuilds and slow compute builds*, reports that dynamic vertex-input keys included all 32 attribute register types even when the vertex shader did not read them. The same shaders could therefore fan out to as many as 20 pipeline keys. Its focused policy keeps only the two-bit types for attributes present in the vertex program's input map, and stays conservative when VertexA is active.
- EglantinePS5 PR #105, merged and console-tested, independently shows the PS5 consequence of a closely related key/prebuild mismatch: 78–81 pipelines were rebuilt mid-game for about 2.2 s of GPU-thread stalls; accepting the correct cached attributeless key reduced those mid-game rebuilds to zero in that test.
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

## Candidate boundary

If the negative control is green, the next runtime candidate should be separate and narrow:
1. NS1/Maxwell graphics only initially; do not alter authenticated NS2 Ampere keys.
2. Publish a two-bit read mask per `{VertexA, VertexB}` identity from the existing shader-builder result.
3. Apply it to a fresh copy of `attribute_types` before the graphics-cache lookup; never carry a masked value into the next shader key.
4. Preserve VertexA conservative behavior.
5. Do not migrate the on-disk cache in the first experiment; allow old records to miss and be rebuilt once, then evaluate a safe migration separately.
6. Add exact negative controls for used/unread types, alternating HUD/world identities, collisions, cache hits, and title teardown.
7. Only claim an FC27 improvement after a PS5 A/B capture compares pipeline creations, held-draw time, frame-time distribution, Options latency and HUD freshness at equivalent cache state.
