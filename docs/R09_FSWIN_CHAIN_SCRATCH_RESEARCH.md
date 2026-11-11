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

The existing `LinkInputFrom` still performs an explicit `vkCmdCopyBuffer` from the previous **private** output to the next **private** input, with compute-to-transfer and transfer-to-compute barriers. Shifted windows still execute the maintained GPU repack, and every FSwin dense/QKV/attention pass records its existing compute visibility barriers. A source-linked guard refuses 8 deliberate dangerous mutations in those prerequisites. These source guarantees are now supplemented by a **two-layer real Vulkan 1.1 synthetic readback witness** with one shared scratch VkBuffer and distinct per-layer input/output VkBuffers. Khronos synchronization validation reports **zero positive-path errors**. Its independent deliberately unsynchronized compute-read → transfer-write scratch overwrite is detected as **SYNC-HAZARD-WRITE-AFTER-READ** and **SYNC-HAZARD-READ-AFTER-WRITE**. This is **not** authentic FSwin learned shader inference, native 44-layer output, or PS5 proof.

## Test ladder and nonclaims

1. Two native-independent GCC/Clang UBSan tests of the strict capacity policy at Original and Compact 720p shapes; a negative suite rejects unsupported shapes, capacities and misuse.
2. A Python extractor compiles the **real** native `InitializeWithScratch` and `Info` methods verbatim with fake VkBuffer/VMA classes; the positive/negative test checks private-vs-shared handle identities, Q/K/V descriptor offsets, invalid source views, missing and aliased handles, and failures at each private allocation.
3. A source guard checks macro isolation, the native chain's reference/borrower destruction order, retained legacy path, scheduler copy and barriers. **All three authentic native FSwin translation units now pass Clang C++20 syntax with both the macro OFF and ON** against pinned Encore dependencies, without advancing its `vendor` gitlink. This is syntax-only, not a complete linked or executed emulator.
4. A real host Vulkan 1.1 synthetic test performs **four compute dispatches, one GPU-to-GPU transfer and exact readback** across two sequential layers which bind the **same VkBuffer** as scratch. It passes without validation; passes with Khronos sync validation and **zero errors**; and rejects an intentionally unsafe scratch TRANSFER write while an earlier compute read is pending (both `WRITE_AFTER_READ` and `READ_AFTER_WRITE` detected). The first negative attempt merely omitted an in-layer compute memory barrier and Khronos did not report that hazard; it was replaced by the stronger, explicitly overlapping transfer fixture. The success is not evidence about native learned FSwin shader output or the full model.
5. **Still required** before real use: execute the authentic FSwin layer shader with exact shared descriptors against user-authorized model data; verify chain adapters, rollback/descriptors/fences under allocation failures; instrument 44-layer buffer residency and complete 152-op Original neural output; test the GPU barriers and trained-model output on physical PS5. Post-neural FSR and 4K quality/performance remain outside this PR.

## Integration rules

Do not merge this research PR with vendor `main`, do not enable its macro in main, do not point Encore's vendor submodule to it, and do not combine it with the ViT experimental branch until BOTH native paths are separately qualified. The complete 152-operation 720p Original inference and post-neural FSR 4K upscaler remain outside this PR and are **not complete**.

### Verified host CI evidence

- [Vendor FSwin research run #42938153680](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42938153680) — **GREEN** at `bb3dbba4b9d272deb7ef11d5da25814bcc8ef0e0`: policy GCC/Clang under both defines; exact extracted `InitializeWithScratch` and `Info` with fake Vulkan; **six real native C++ syntax checks (three TUs, macro 0/1)**; real two-layer synthetic Vulkan compute/readback; zero Khronos validation errors with correct barriers; deliberately injected scratch transfer hazard detected twice.
- No PS5 binary, linked full native graph, proprietary weights, frame-time measurement or device VRAM observation was produced by this host run.


### Eight-segment Vulkan stress (synthetic)

An extended [CI #42939379907](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42939379907) now compiles and executes `tools/r09_fswin_chain_pool_vulkan_graph_smoke.cpp` with the public **synthetic** two-pass shader and **eight physically independent scratch VkBuffers**, one per segment. **Original-shaped** chain lengths `4,4,6,8,4,4,6,8` create **44 private Input VkBuffers, 44 private Output VkBuffers, 8 shared segment scratch buffers**, and record **88 real compute dispatches** plus 43 real `vkCmdCopyBuffer` handoffs; all **44 output buffers** match CPU arithmetic readback. **Compact-shaped** chains use `2×8=16` layers, 32 private input/output buffers, 8 segment scratch buffers, 32 dispatches, 15 GPU copies and all **16 outputs** verified. Both pass Khronos synchronization validation with **zero errors**. An intentionally unsynchronized scratch `vkCmdFillBuffer` at layer 4 in Original raises `SYNC-HAZARD-WRITE-AFTER-READ` as required. The distinct scratch buffers are independently allocated, not merely different views of the same handle.

**Important limitation:** this stress harness keeps the same synthetic buffer shape across segment boundaries and copies outputs directly across boundaries. The actual learned FSwin graph inserts downsample/upsample, skip, shifted-window and learned attention adapters, which are **not executed** by this synthetic stress harness. Its 44/16 count is analogous to the real FSwin layer counts, **not a GPU proof of the 152/76 learned model rows**. This also does not establish peak physical PS5 VRAM usage or actual RDNA2 performance.


### Real RDNA2 FSwin Dense shader GPU A/B

The next rung is no longer an abstract arithmetic shader: [isolated vendor host CI #42940756753](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/42940756753) compiles the **maintained, unmodified** `encore-active/eden-vulkan/r09_rdna2_nr/fswin_dense_rdna2.comp` with `glslangValidator` targeting Vulkan 1.1 and checks `spirv-val`. The test `tools/r09_fswin_native_dense_pool_compare.cpp` executes **four real C32 FSwin Dense expansion (stage 0) and contraction (stage 2) shader passes** with legal, deterministic, publicly generated E4M3FN **identity-test weights** and nontrivial FP8 input. It compares the GPU readback for four independent expanded scratch buffers against a single chain-owned expanded scratch buffer reused after each layer, while retaining independent per-layer inputs and outputs, real compute barriers and copy handoffs. **All 8,192 output bytes agree bit for bit; all 8,192 are nonzero, 7,680 differ from the first byte**. The same test is rerun with **Khronos synchronization validation enabled for BOTH variants** and reports **zero validation errors**.

This is a genuine *native FSwin shader* GPU correctness witness for Dense stages 0 and 2, not a fake shader. Still **not covered by this A/B**: C64/128/256 Dense middle, QKV projection, 64-token window attention, post-attention Dense stage 3, authentic model weights, eight full FSwin learned chains with real raster adapters, or the 152-operation Original R09 neural frame and 4K FSR handoff. In particular the 44/16 stress test uses a separate synthetic shader and must not be described as 44/16 learned GPU layers.

All experimental code remains disabled for normal builds. The engine submodule pin is unchanged; no PS5 binary was created.
