# PS5 fragment-shuffle fallback — 2026-11-17

## Symptom and regression boundary

FC27 NS1 (Title ID `0100C49025D3E000`) reaches gameplay on the Nov-17 R457 candidate, but guest text remains incomplete: menu text can be absent, HUD glyphs arrive late, refreshed clock digits disappear, pause-menu letters are missing, player-shirt names are absent, and a stadium-seat texture can render green. The older R1 source `5629395324ff65c98b50c44f35f2142a4a92654a`, reported by hardware testing to render FC27 text, uses the same authored FC27 video/performance profile as current Encore and predates the selected Eden #4503 fragment-shader drop.

This candidate is a rendering/shader hypothesis. It is not evidence for the separate black intro-video issue, and it does not claim that every reported texture defect has one cause.

## Upstream-first comparison

- Eden mirror `67bada77f8a43a90da2e94e89b8e7da73c256989` / reviewed change #4503 detects fragment shuffles sourced through `BitCastU32F32(GetAttribute(PositionX/Y))` and returns no Vulkan pipeline on non-NVIDIA drivers.
- Current Encore selected Vulkan owner carried that behavior: an exact `HasBrokenPattern()` hit logs a warning then returns `nullptr`, dropping the entire guest draw.
- Lichii `68caa6c75ea6c4db7be2a5988cdbcacf5418d3ed` and Eglantine `18106bdf4920341071ca916d582406da714cd712` add `has_broken_spirv_fragment_shuffle` and lower affected fragment shuffles through subgroup broadcasts/selects instead of using the native broken shuffle instruction.
- Those newer upstreams still preserve the generic Eden #4503 skip in their pipeline cache. Encore therefore adapts the fallback only for the PS5-native path and only after the exact existing `HasBrokenPattern()` predicate matches.

## Candidate

Branch `p1/ps5-fragment-shuffle-fallback-20261117`.

- Add the newer upstream fragment-shuffle fallback profile bit.
- Use broadcast/select lowering for absolute and relative fragment shuffles.
- Extend the same fallback to `ShuffleButterfly`, because the existing Eden predicate classifies butterfly as broken too; non-fallback paths remain unchanged.
- On `PS5_NATIVE`, mark only the matching guest program for fallback and continue pipeline creation. On all other builds, preserve the existing Eden #4503 `return nullptr`.
- Do not globally force slow fallback for every PS5 fragment shader.
- No shader-cache-version bump is required for this specific transition: the affected shaders were previously returned before SPIR-V emission/pipeline serialization, so there is no valid old cached variant to invalidate for those exact dropped programs.

## Host acceptance

`tests/ps5_fragment_shuffle_fallback_host.py` verifies the exact selected source structure, preservation of the non-PS5 drop, per-program rather than global fallback, and 32/64-lane guest-warp partition semantics for index/up/down/butterfly. It does not prove PS5 GPU output.

## Required PS5 acceptance

Use the same FC27 save/scene and compare against the Nov-17 baseline:

1. main menu text completeness;
2. pause-menu letters;
3. HUD labels at match start;
4. refreshed match-clock digits over time;
5. player shirt names;
6. stadium-seat green texture defect;
7. first-match versus second-match frame pacing.

If text remains unchanged, keep texture/invalidation and descriptor freshness as separate candidates. If the PS5 driver fails or artifacts worsen, revert this candidate and retain Eden #4503 until a more specific driver workaround is demonstrated.
