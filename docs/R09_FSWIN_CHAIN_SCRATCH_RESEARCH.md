# R09 DLSS-NR Min — isolated native FSwin scratch-pool experiment

**DRAFT RESEARCH ONLY. DISABLED by default. Do not merge to vendor `main` or change Encore's vendor pin.**

Related work: [engine R09 Min PR #43](https://github.com/niakw/ProsperoEden-Encore/pull/43) and independent [ViT pool vendor PR #34](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/34).

## Why this experiment exists

The pinned native R09 `NrRdna2FSwinChain::Initialize` creates every `NrRdna2FSwinLayerRuntime` and every `NrRdna2FSwinGpuTensors` allocation upfront. At 1280×720 structural geometry, the real FSwin owner requests **1,097,072,640 bytes (1,046.25 MiB)** for the 44-layer Original path and **477,757,440 bytes (455.625 MiB)** for the 16-layer Compact path, before weights and other layers.

The research-only policy retains each individual layer's **Upload input** and **DeviceLocal output** and permits sharing only four internal roles **within one chain**: expanded, middle (absent for C32), contracted and attention context. Q/K/V remain distinct non-owning views into expanded, as in the maintained native source. There are eight independent chains per frame session; no scratch sharing across chains, frame slots, weights or game processes.

## Exact source-derived candidate allocation reduction

| Request | Original, 44 FSwin layers | Compact, 16 FSwin layers |
| --- | ---: | ---: |
| Default full layer-owned FSwin tensors | 256 buffers / 1,046.25 MiB | 92 buffers / 455.625 MiB |
| Experimental chain-private scratch with per-layer I/O retained | **118 buffers / 419.0625 MiB** | **62 buffers / 281.5625 MiB** |
| Theoretical reduction | **138 buffers / 627.1875 MiB** | **30 buffers / 174.0625 MiB** |

These are **logical Vulkan buffer requests only**. They are neither actual PS5 allocations nor a VRAM measurement. They exclude learned weights, descriptor pools, external FrameSession buffers, model upload, down/up adapters, game and swapchain images, memory padding and fragmentation. No source is deleted or removed from the learned graph.

The native default allocator remains intact behind `#else`. The pooled path requires a **deliberate** research-only compilation with `EDEN_R09_MIN_FSWIN_SCRATCH_POOL_EXPERIMENTAL=1`. This branch does **not** set that flag in any normal CMake/build configuration, and no PS5 build is triggered.

## Native integration locations

- `include/rdna2_nr_fswin_scratch_pool_policy.h`: strict 32/64/128/256-channel and per-buffer/per-layer bound checks for a chain's four shared scratch roles. All 4×4, Q/K/V and stage ABI sizes must match the currently pinned graph.
- `nr_rdna2_fswin_chain.cpp/.h`: one scratch Vulkan owner **per chain**, allocated before creating the borrowing layer runtimes, with distinct source/destination VkBuffers preserved for every layer. Scratch owner is declared before `layers`, so it is destroyed **after** the borrower runtimes.
- `nr_rdna2_fswin_layer_runtime.cpp/.h`: opt-in layered initialization still imports the actual model weights and builds all per-layer graph descriptors; it passes borrowed scratch descriptors to the tensor layer.
- `nr_rdna2_fswin_gpu_tensors.cpp/.h`: opt-in `InitializeWithScratch` checks distinct scratch handles, sizes, alignment and storage-range limits; creates only per-layer Upload input and DeviceLocal output; `Info` presents the shared scratch for the proper roles and Q/K/V offsets. Default `Initialize` stays untouched.

The existing `LinkInputFrom` still performs an explicit `vkCmdCopyBuffer` from the previous **private** output to the next **private** input, with compute-to-transfer and transfer-to-compute barriers. Shifted windows still execute the maintained GPU repack, and every FSwin dense/QKV/attention pass records its existing compute visibility barriers. A source-linked guard refuses 8 deliberate dangerous mutations in those prerequisites. These statements describe source-code intent; **real cross-layer aliasing has not yet been independently checked with Vulkan synchronization validation**.

## Test ladder and nonclaims

1. Two native-independent GCC/Clang UBSan tests of the strict capacity policy at Original and Compact 720p shapes; a negative suite rejects unsupported shapes, capacities and misuse.
2. A Python extractor compiles the **real** native `InitializeWithScratch` and `Info` methods verbatim with fake VkBuffer/VMA classes; the positive/negative test checks private-vs-shared handle identities, Q/K/V descriptor offsets, invalid source views, missing and aliased handles, and failures at each private allocation.
3. A source guard checks macro isolation, the native chain's reference/borrower destruction order, retained legacy path, scheduler copy and barriers. **These do not compile the complete vendor translation units**.
4. **Still required** before a real opt-in can even be considered safe: compile ALL affected authentic vendor C++ units together against exactly pinned Encore headers in an isolated host build; execute actual two-layer scratch reuse on Vulkan 1.1 with positive and deliberately missing-barrier negative synchronization validation and numerical readback; verify rollback/descriptors/fences on an authentic model, prove game-frame lifetime and memory with PS5, then assess quality and frametime.

## Integration rules

Do not merge this research PR with vendor `main`, do not enable its macro in main, do not point Encore's vendor submodule to it, and do not combine it with the ViT experimental branch until BOTH native paths are separately qualified. The complete 152-operation 720p Original inference and post-neural FSR 4K upscaler remain outside this PR and are **not complete**.
