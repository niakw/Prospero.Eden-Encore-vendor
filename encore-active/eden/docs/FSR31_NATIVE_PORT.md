# Encore native FSR 3.1.4 — Vulkan GPU backend (R07)

**Code imported and maintained on a feature branch; no PS5 validation yet.**

Source provenance:
- AMD MIT subset `encore-active/amd-fsr31/`: upstream
  GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, tag v1.1.4,
  original commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`.
  Includes `LICENSE.txt` and unmodified AMD GPU source/pass shaders.
- Ported host wrapper `fsr31_temporal_upscaler.cpp/.h`: Lichii
  `zortos293/Lichii` branch nx2, commit
  `27226d2568c99c70cf78aa37adaf98b60457b2e5`,
  GPL-3.0-or-later. Port changes are owned in Encore vendor.

What is implemented on the feature branch:

1. Eleven FSR3.1.4 compute variants from AMD's official GPU source:
   PrepareInputs, SPD LumaPyramid (wave/no-wave),
   ShadingChangePyramid (wave/no-wave), ShadingChange,
   PrepareReactivity, LumaInstability, Accumulate,
   Accumulate+Sharpen and RCAS.
2. C++ host `Vulkan::Fsr3Upscaler`: resources/ping-pong histories,
   compute passes, dispatch, output image/view and barriers,
   adapted from Lichii to the pinned Eden descriptor API.
3. Descriptor pool extension reserves **actual**
   `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` and
   `VK_DESCRIPTOR_TYPE_SAMPLER` for the separate shader bindings,
   without repurposing legacy combined-sampler allocations.
4. FSR3 depth projection is supplied by a **verified guest producer**.
   The imported default 60-degree FOV/0.1 m camera assumption was
   removed. The generic bridge requires color, depth, motion,
   conventions, exact frame identities, jitter, verified camera
   parameters, GPU completion and separated HUD.
5. The bridge resets history on gaps or scene changes and provides
   a borrow-only output view to the guest compositing path.
   The owner finishes GPU work before destroying its history.
6. The main repository's PS5 `headless/vulkan.cmake` compiles these
   exact sources + all shader variants as native video_core units;
   no release/dev/P5 build conditions.

**Critical integration gap:** the pinned Encore guest GPU backend
does NOT yet expose a general producer for color + depth + motion
+ projection + jitter with history/separate guest HUD. The output
is therefore **not connected to the normal real-game presentation
pipeline** and is **not selectable as a working FSR3.1 temporal mode**.
The game's DLSS hooks in Lichii use title-specific passes/CBUF offsets
and must NOT be copied as an all-games solution.

All eleven SPIR-V shaders validated on Mac host with spirv-val,
host C++ syntax checked against current Eden headers with Clang.
No PS5 SDK/full build or real console images were used.

This is not the R08 optical-flow frame-generation implementation,
nor the spatial FSR1 EASU/RCAS filter.
