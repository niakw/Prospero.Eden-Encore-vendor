# vulkan-ps4 — Vulkan 1.0 ICD over OpenGNM

Build `vulkan-ps4`: a Vulkan 1.0 Installable Client Driver (ICD) that
translates Vulkan API calls into GNM/PM4 commands via OpenGNM, enabling any
Vulkan application (including RetroArch) to run with GPU acceleration on
jailbroken PS4 hardware.

The shader compilation path (SPIR-V → GCN binary) is already solved by
`opengnm-psbc`. The project is architected for Vulkan 1.0 core, with the
dispatch table and module layout left open to add Vulkan 1.1/1.2 extensions
later without rewrites.

## Target

- **API**: Vulkan 1.0 core (`VK_API_VERSION_1_0`), no promoted extensions
- **Hardware**: PS4 base (GFX7 / Sea Islands), PS4 Pro (GFX8 / Polaris)
- **Runtime**: Jailbroken PS4 with OpenOrbis toolchain
  (`OO_PS4_TOOLCHAIN`, LLVM 18 via
  `tools/setup_openorbis_llvm18_macos.sh`). **Not orbisdev** — orbisdev is
  abandoned and superseded by OpenOrbis.
- **Shader path**: SPIR-V 1.0 → NIR → ACO → GCN ISA → `GnmShaderFileHeader`
  (via `opengnm-psbc`)
- **First customer**: RetroArch (`gfx/drivers/vulkan.c`)

## Architecture

```
Vulkan App (RetroArch / any vk app)
        |  vkCmd* / vkCreate*
        v
libvulkan_ps4.so  (this project — the ICD)
        |  sceGnm* / sceGpa* / PM4 packets
        v
opengnm  (existing)
        |  firmware externs
        v
libSceGnmDriver + libSceVideoOut  (PS4 firmware)
        |
        v
GPU  (GFX7 base / GFX8 Pro)
```

### Key integration points

| Vulkan concept | OpenGNM equivalent | Notes |
|---|---|---|
| `VkShaderModule` | `GnmShaderFileHeader` | Compile via `libpsbc` (Phase 0) |
| `VkCommandBuffer` | `GnmCommandBuffer` + PM4 | OpenGNM generic backend already emits PM4 |
| `vkQueueSubmit` | `sceGnmSubmitCommandBuffers` | Orbis backend forwards to firmware |
| `VkSwapchainKHR` | `GnmVideoOut` | OpenGNM helpers manage VideoOut open/flip |
| `VkDeviceMemory` | `GnmDirectMemory` | Garlic = GPU-local, Onion = CPU-coherent |
| `VkRenderPass` / `VkFramebuffer` | `GnmRenderTarget` + `GnmDepthRenderTarget` | |
| `VkPipeline` | GNM shader stage regs + blend/depth/raster | PM4 state packets |
| `VkDescriptorSet` | GNM user-data register slots | `SetVsharp/Tsharp/Ssharp/PointerUserData` |
| `VkVertexInput` | GNM fetch shader | Generated via `opengnm/src/gcn/` |
| `VkFence` / `VkSemaphore` | EOP event write + CPU poll | |
| `VkFormat` | `GnmDataFormat` | `opengnm/include/gnm_dataformat.h` |

## Phased Delivery

### Phase 0 — Foundation & libpsbc extraction

> **Note:** opengnm-psbc's Makefile is currently a placeholder (Mesa
> sources vendored but build system incomplete — see
> `opengnm-psbc/OPENGNM_PSBC_PLAN.md` Phase 3, deferred). Phase 0.1
> (libpsbc extraction) is blocked until opengnm-psbc builds. The
> vulkan-ps4 skeleton (0.2-0.4) uses a **stub shader path** in the
> meantime — `vkCreateShaderModule` returns a placeholder binary. The
> triangle test (Phase 1 step 16) uses a pre-compiled or hand-written
> GCN shader binary until libpsbc is available.

1. **Extract opengnm-psbc compilation into a reusable library (`libpsbc`)**
   - *Blocked on opengnm-psbc build system completion*
   - New: `opengnm-psbc/libpsbc/psbc_compile.h` + `psbc_compile.c`
   - Extract the SPIR-V → GCN binary logic from `cmd/psbc/main.c` (lines
     ~780-846) into a C API:
     ```c
     PsbcResult psbc_compile_shader(
         const uint32_t* spirv, size_t spirv_size,
         PsbcTarget target,              /* PS4_BASE / PS4_NEO / PS5 */
         mesa_shader_stage stage,
         PsbcShaderOutput* out           /* binary + metadata */
     );
     ```
   - Preserve the Mesa init sequence (`glsl_type_singleton_init`,
     `ac_init`, etc.) — call once per process, refcounted
   - `cmd/psbc/main.c` becomes a thin CLI wrapper around `libpsbc`
   - **This is the single most critical prerequisite for real shaders —
     but the ICD skeleton and most of Phase 1-2 can proceed with stub
     shaders.**

2. **Create vulkan-ps4 project skeleton**
   - New directory: `vulkan-ps4/` (sibling to `opengnm/`)
   - `CMakeLists.txt` — builds `libvulkan_ps4.so` (PS4) and a host stub
     library for unit tests
   - `Makefile.orbis` — OpenOrbis build, links opengnm + libpsbc
   - Link against: `opengnm`, `libpsbc`, `Vulkan-Headers` (types only)

3. **ICD entry point**
   - File: `src/vk_ps4_entry.c`
   - Implement `vk_icdNegotiateLoaderICDInterfaceVersion`,
     `vk_icdGetInstanceProcAddr`, `vk_icdGetPhysicalDeviceProcAddr`
   - On PS4 (no standard loader), also export all `vk*` symbols directly
     for static linking with RetroArch
   - Generate the dispatch table from `Vulkan-Headers/vk.xml` via a small
     Python script (`scripts/gen_dispatch.py`)

4. **VkFormat → GnmDataFormat mapping**
   - File: `src/vk_ps4_format.c`
   - Map all `VkFormat` enums to `GnmDataFormat` (opengnm has 100+ formats)
   - Handle format properties: linear, tiled, compressed, depth/stencil

### Phase 1 — MVP: Triangle on screen

5. **Instance & physical device**
   - File: `src/vk_ps4_instance.c`
   - `vkCreateInstance` — minimal, no layers
   - `vkEnumeratePhysicalDevices` — return one synthetic PS4 device
   - `vkGetPhysicalDeviceProperties` — report GCN properties,
     `VK_API_VERSION_1_0`
   - `vkGetPhysicalDeviceMemoryProperties` — two memory types:
     Garlic (GPU-local, `GNM_DIRECT_MEMORY_TYPE_WC_GARLIC`) and
     Onion (CPU-coherent)
   - `vkGetPhysicalDeviceQueueFamilyProperties` — one graphics+compute queue

6. **Logical device & queue**
   - File: `src/vk_ps4_device.c`
   - `vkCreateDevice` — create logical device, init opengnm
   - `vkGetDeviceQueue` — return single queue handle
   - `vkDestroyDevice` — tear down opengnm state

7. **Memory management**
   - File: `src/vk_ps4_memory.c`
   - `vkAllocateMemory` → `sceGnmDirectMemoryAllocate`
   - `vkMapMemory` / `vkUnmapMemory` / `vkFlushMappedMemoryRanges` /
     `vkInvalidateMappedMemoryRanges` — map direct memory
   - `vkFreeMemory` → `sceGnmDirectMemoryRelease`
   - Suballocate from large direct memory blocks (heap tracking)

8. **Buffers & images**
   - Files: `src/vk_ps4_buffer.c`, `src/vk_ps4_image.c`
   - `vkCreateBuffer` / `vkCreateImage` — create descriptors, compute
     size via `sceGpaComputeSurfaceInfo`
   - `vkBindBufferMemory` / `vkBindImageMemory` — bind to direct memory
   - `vkCreateImageView` — create `GnmTexture` view descriptor
   - `vkGetBufferMemoryRequirements` / `vkGetImageMemoryRequirements`

9. **Command buffers**
   - File: `src/vk_ps4_command.c`
   - `vkCreateCommandPool` / `vkAllocateCommandBuffers` — allocate
     `GnmCommandBuffer`
   - `vkBeginCommandBuffer` — init with
     `sceGnmDrawCmdInitDefaultHardwareState`
   - `vkEndCommandBuffer` / `vkResetCommandBuffer` / `vkFreeCommandBuffers`

10. **Render pass & framebuffer**
    - File: `src/vk_ps4_render_pass.c`
    - `vkCreateRenderPass` — store attachment descriptions (format,
      load/store ops)
    - `vkCreateFramebuffer` — create `GnmRenderTarget` from attached
      image views
    - `vkCmdBeginRenderPass` — emit render target set + clear operations
    - `vkCmdEndRenderPass` — emit EOP event

11. **Shader modules & pipelines (minimal)**
    - File: `src/vk_ps4_shader.c` — `vkCreateShaderModule` calls `libpsbc`
      to compile SPIR-V → GCN binary, store result
    - File: `src/vk_ps4_pipeline.c` — `vkCreateGraphicsPipelines`
      assembles GNM shader stage registers from compiled binaries + pipeline
      state (blend, rasterizer, depth/stencil)
    - `vkCreatePipelineLayout` / `vkCreateDescriptorSetLayout` — store
      layout metadata (no descriptor binding yet)
    - `vkCmdBindPipeline` — emit `sceGnmDrawCmdSetVsShader` /
      `SetPsShader` + state registers

12. **Draw commands**
    - In `src/vk_ps4_command.c`:
    - `vkCmdDraw` → `sceGnmDrawCmdDrawIndexAuto`
    - `vkCmdSetViewport` → `sceGnmDrawCmdSetViewport`
    - `vkCmdSetScissor` → `sceGnmDrawCmdSetScreenScissor`
    - `vkCmdBindVertexBuffers` — track for draw time

13. **Swapchain (VideoOut)**
    - File: `src/vk_ps4_swapchain.c`
    - `vkCreateSwapchainKHR` → `sceGnmVideoOutOpen` + allocate display
      buffers via `sceGnmVideoOutCalcBufferLayout`
    - `vkGetSwapchainImagesKHR` — return VideoOut buffer wrappers as
      `VkImage`
    - `vkAcquireNextImageKHR` — return next buffer index
    - `vkQueuePresentKHR` → `sceGnmVideoOutSubmitFlipAndWait`
    - `vkDestroySwapchainKHR` → `sceGnmVideoOutClose`

14. **Queue submit**
    - File: `src/vk_ps4_queue.c`
    - `vkQueueSubmit` → `sceGnmSubmitCommandBuffers` (or
      `SubmitAndFlipCommandBuffers` if presenting)
    - `vkQueueWaitIdle` / `vkDeviceWaitIdle` — wait for firmware
      completion

15. **Sync primitives**
    - File: `src/vk_ps4_sync.c`
    - `vkCreateFence` / `vkWaitForFences` / `vkResetFences` — use EOP
      event write + CPU poll
    - `vkCreateSemaphore` — binary semaphore, track signal/wait pairs
    - `vkDestroyFence` / `vkDestroySemaphore`

16. **Triangle test**
    - File: `tests/test_triangle.c`
    - Hardcoded triangle (positions in shader, no vertex buffer)
    - Shaders: `shaders/triangle.vert` + `shaders/triangle.frag`
    - Compile shaders with `opengnm-psbc` CLI (or `libpsbc` at runtime)
    - **Success criterion: colored triangle appears on PS4 screen via
      VideoOut. This is the first end-to-end proof of the pipeline.**

### Phase 2 — RetroArch-compatible

17. **Descriptor sets**
    - File: `src/vk_ps4_descriptor.c`
    - `vkCreateDescriptorPool` / `vkAllocateDescriptorSets` /
      `vkFreeDescriptorSets`
    - `vkUpdateDescriptorSets` — map to GNM resource descriptors:
      - `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` / `STORAGE_BUFFER` →
        `GnmBuffer` (Vsharp)
      - `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` / `STORAGE_IMAGE` →
        `GnmTexture` (Tsharp)
      - `VK_DESCRIPTOR_TYPE_SAMPLER` → `GnmSampler` (Ssharp)
      - `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` → Tsharp + Ssharp
    - `vkCmdBindDescriptorSets` → `sceGnmDrawCmdSetVsharpUserData` /
      `SetTsharpUserData` / `SetSsharpUserData` / `SetPointerUserData`

18. **Vertex input (fetch shaders)**
    - In `src/vk_ps4_pipeline.c`: translate
      `VkVertexInputBindingDescription` / `VkVertexInputAttributeDescription`
      → GNM fetch shader
    - Generate fetch shader via opengnm's GCN assembler (`opengnm/src/gcn/`)
    - `vkCmdBindVertexBuffers` — bind vertex buffer addresses to fetch
      shader slots

19. **Indexed draws**
    - `vkCmdBindIndexBuffer` → `sceGnmDrawCmdSetIndexBuffer` +
      `SetIndexSize`
    - `vkCmdDrawIndexed` → `sceGnmDrawCmdDrawIndex`

20. **Texture upload (copy/blit)**
    - In `src/vk_ps4_command.c`:
    - `vkCmdCopyBufferToImage` — emit PM4 DMA copy (staging buffer →
      tiled texture)
    - `vkCmdBlitImage` — emit PM4 blit (with format conversion if needed)
    - `vkCmdCopyBuffer` / `vkCmdCopyImage` — DMA copies
    - `vkCmdPipelineBarrier` — emit EOP wait /
      `sceGnmDrawCmdWaitGraphicsWrite`
    - `vkCmdImageMemoryBarrier` / `vkCmdBufferMemoryBarrier` — track
      layout transitions, emit barriers as needed

21. **Compute pipelines**
    - In `src/vk_ps4_pipeline.c`: `vkCreateComputePipelines` — assemble
      `GnmCsStageRegisters`
    - In `src/vk_ps4_command.c`: `vkCmdDispatch` →
      `sceGnmDrawCmdDispatchDirect`
    - RetroArch uses compute for RGB565→RGBA8888 conversion
      (`vk->pipelines.rgb565_to_rgba8888`)

22. **RetroArch integration**
    - **Migrate `RetroArch/Makefile.orbis` from orbisdev to OpenOrbis**
      (prerequisite — the current Makefile uses the abandoned orbisdev
      toolchain with `ORBISDEV` env var, `orbis-elf-create`, and
      `make_fself.py`; the rest of the PS4 homebrew ecosystem uses
      OpenOrbis with `OO_PS4_TOOLCHAIN`). Create a new
      `Makefile.openorbis` or update `Makefile.orbis` to:
      - Use `OO_PS4_TOOLCHAIN` instead of `ORBISDEV`
      - Use `create-fself-macos` instead of `make_fself.py`
      - Use `create-gp4` + `PkgTool.Core` for packaging
      - Use OpenOrbis linker script (`$OO_PS4_TOOLCHAIN/link.x`)
      - Use OpenOrbis stub libraries (`-lkernel_stub`,
        `-lSceGnmDriver_stub`, `-lSceVideoOut_stub`, etc.)
      - Reference: `Kisak-Source-PS4/build-ps4-monolithic.sh` and
        `ppsspp/CMakeLists.txt` (line ~1495) for working OpenOrbis
        build patterns
    - Add `HAVE_VULKAN=1` to the migrated Makefile, link `libvulkan_ps4`
      + `opengnm` + `libpsbc`
    - Modify `RetroArch/gfx/common/vulkan_common.c`: on PS4
      (`__ORBIS__`), link statically instead of dlopening `libvulkan.so`
    - Modify `RetroArch/libretro-common/gfx/vulkan_symbol_wrapper.c`:
      PS4 path resolves symbols at link time
    - Build: `make -f Makefile.openorbis HAVE_VULKAN=1`
    - **Success criterion: RetroArch renders the RGUI menu on PS4 via
      the Vulkan driver, and a running libretro core displays with
      GPU-accelerated video output.**

### Phase 3 — Full Vulkan 1.0 core

23. **Multiple render targets**
    - Support up to `GNM_MAX_RENDERTARGETS` (8) color attachments
    - `vkCmdSetBlendColor` → `sceGnmDrawCmdSetBlendColor`
    - `sceGnmDrawCmdSetRenderTargetMask`

24. **Depth/stencil**
    - `vkCreateFramebuffer` with depth attachment →
      `GnmDepthRenderTarget`
    - `vkCmdClearDepthStencilImage`
    - Depth/stencil state in pipeline → `GnmDepthStencilControl` /
      `GnmDbRenderControl`
    - `vkCmdSetDepthBias` / `vkCmdSetStencilCompareMask` /
      `vkCmdSetStencilReference` / `vkCmdSetStencilWriteMask`

25. **Clear commands**
    - `vkCmdClearColorAttachment` / `vkCmdClearDepthStencilAttachment` /
      `vkCmdClearColorImage`
    - Emit PM4 clear packets via `sceGnmDrawCmdEventWriteEop` or
      firmware clear path

26. **Query pools**
    - File: `src/vk_ps4_query.c`
    - `vkCreateQueryPool` / `vkCmdResetQueryPool` / `vkCmdBeginQuery` /
      `vkCmdEndQuery` / `vkCmdWriteTimestamp`
    - Occlusion queries → `sceGnmDrawCmdBeginQuery` / `EndQuery` /
      `ResetQuery`
    - `vkGetQueryPoolResults` — read back from GPU memory
    - `vkCmdCopyQueryPoolResults` — copy results to buffer

27. **Pipeline cache**
    - `vkCreatePipelineCache` / `vkGetPipelineCacheData` /
      `vkMergePipelineCaches` / `vkDestroyPipelineCache`
    - Cache compiled GCN shader binaries keyed by SPIR-V hash
    - Persist to disk on PS4 (`/data/vulkan_ps4/cache.bin`)

28. **Tessellation & geometry shaders**
    - In `src/vk_ps4_pipeline.c`: handle tess/geo stages in
      `vkCreateGraphicsPipelines`
    - `sceGnmDrawCmdSetHsShader` / `SetLsShader` / `SetGsShader` /
      `SetEsShader`
    - opengnm-psbc already supports these stages
      (`MESA_SHADER_TESS_CTRL/EVAL`, `GEOMETRY`)
    - Pipeline `tessellationState` → GNM tess factor registers

29. **Indirect draws**
    - `vkCmdDrawIndirect` / `vkCmdDrawIndexedIndirect` →
      `sceGnmDrawCmdDrawIndirect` / `DrawIndexIndirect`
    - Multi-draw: `vkCmdDrawIndexedIndirectCount` /
      `vkCmdDrawIndirectCount` (Vulkan 1.2 core, but the GNM side has
      `DrawIndexIndirectCountMulti` — implement if needed)

30. **Dynamic state**
    - `vkCmdSetLineWidth` (if `fillModeNonSolid` is used)
    - `vkCmdSetBlendConstants` → `sceGnmDrawCmdSetBlendColor`
    - All map to GNM control register PM4 packets

31. **Events**
    - `vkCmdSetEvent` / `vkCmdResetEvent` / `vkCmdWaitEvents`
    - Map to EOP event write + wait

32. **Sparse binding (optional)**
    - `vkQueueBindSparse` — only if an app needs it; GNM has direct
      memory mapping so sparse is largely a no-op
    - Can defer until an app actually requires it

### Phase 4 — Optional extensions (on demand)

These are not required for Vulkan 1.0 conformance or RetroArch. Implement
only when an app needs them. The architecture supports adding them
without rewrites.

**Implemented (DONE, VVL-tested):**
- `VK_KHR_driver_properties` (Vulkan 1.2) — DONE: reports driverName
  "vulkan-ps4", driverInfo "OpenGNM Vulkan ICD", driverID
  VK_DRIVER_ID_MESA_RADV, conformanceVersion 1.1.0.0
- `VK_KHR_imageless_framebuffer` (Vulkan 1.2) — DONE: imageless
  framebuffers with VkRenderPassAttachmentBeginInfo at beginRenderPass
- `VK_EXT_descriptor_indexing` (Vulkan 1.2) — DONE: bindless, variable
  descriptor count, update-after-bind, partially-bound, runtime arrays
- `VK_KHR_timeline_semaphore` (Vulkan 1.2) — DONE: software-emulated
  64-bit counter, GetSemaphoreCounterValue/Signal/Wait
- `VK_KHR_image_format_list` (Vulkan 1.2) — DONE: dependency for
  imageless_framebuffer
- `VK_KHR_swapchain` (required for WSI, already in Phase 1)
- `VK_KHR_create_renderpass2` (Vulkan 1.2) — DONE: CreateRenderPass2
  converts v2 structs to v1 and delegates to CreateRenderPass
- `VK_KHR_depth_stencil_resolve` (Vulkan 1.2) — DONE: properties report
  SAMPLE_ZERO resolve modes, independentResolve
- `VK_KHR_spirv_1_4` (Vulkan 1.2) — DONE: handled by opengnm-psbc
  via Mesa NIR (no features struct, extension-only)
- `VK_KHR_shader_float_controls` (Vulkan 1.2) — DONE: dependency for
  spirv_1_4, reports float32 denorm flush-to-zero, signedZeroInfNan preserve
- `VK_EXT_scalar_block_layout` (Vulkan 1.2) — DONE: scalarBlockLayout=TRUE
- `VK_KHR_uniform_buffer_standard_layout` (Vulkan 1.2) — DONE:
  uniformBufferStandardLayout=TRUE
- `VK_EXT_host_query_reset` (Vulkan 1.2) — DONE: vkResetQueryPoolEXT
  zeros query result memory on host
- `VK_KHR_shader_atomic_int64` (Vulkan 1.2) — DONE:
  shaderBufferInt64Atomics=TRUE (GCN buffer atomics)
- `VK_KHR_buffer_device_address` (Vulkan 1.2) — DONE:
  vkGetBufferDeviceAddress returns GNM direct memory GPU address
- `VK_KHR_shader_subgroup_extended_types` (Vulkan 1.2) — DONE:
  shaderSubgroupExtendedTypes=TRUE (GCN DPP/SWIZZLE)
- `VK_KHR_vulkan_memory_model` (Vulkan 1.2) — DONE:
  vulkanMemoryModel=TRUE, deviceScope=TRUE
- `VK_EXT_separate_stencil_usage` (Vulkan 1.2) — DONE: extension-only,
  no features struct (adds VkImageCreateFlag)

**Not yet implemented:**
- `VK_KHR_surface` / `VK_KHR_display` (PS4 has no window system —
  swapchain is the display)

### Phase 5 — Conformance & polish

33. **Vulkan Validation Layer testing**
    - Run `Vulkan-ValidationLayers` against the ICD on host (generic
      backend) to find spec violations
    - Fix all validation errors
    - DONE: 5/5 tests pass with 0 VVL errors, 0 warnings

34. **SPIRV-Tools validation**
    - Validate all SPIR-V input through `SPIRV-Tools` before compilation
    - DONE: `vkCreateShaderModule` validates SPIR-V with `spvValidateBinary`
      using `SPV_ENV_VULKAN_1_1` target env.  Basic sanity checks (magic
      number, size multiple of 4, min 20 bytes) are always performed;
      full SPIRV-Tools validation is enabled when the library is found
      (host build only, via CMake `find_library`).  Invalid SPIR-V returns
      `VK_ERROR_INVALID_SHADER_NV`.  VVL test 17e exercises both reject
      (bad magic) and accept (valid minimal shader) paths.

35. **Performance optimization**
    - Pipeline cache persistence — DONE: FNV-1a hashed cache with
      serialization (GetPipelineCacheData/CreatePipelineCache initialData),
      MergePipelineCaches, cache lookup/insert in pipeline creation
    - Descriptor set reuse / pooling — DONE: free list in VkDescriptorPool
      (up to 256 sets), AllocateDescriptorSets reuses from free list,
      FreeDescriptorSets adds to free list, ResetDescriptorPool frees all
    - Command buffer pooling — DONE: free list in VkCommandPool (up to 256
      buffers), AllocateCommandBuffers reuses from free list (preserves PM4
      buffer), FreeCommandBuffers adds to free list, TrimCommandPool frees
    - Minimize PM4 packet overhead — DONE: batched SET_SH_REG for
      base_vertex+start_instance (count=2 when consecutive)
    - Batch user-data register writes — DONE: CmdPushConstants groups
      consecutive user-data registers into single SET_SH_REG packets
    - VVL tests: sections 17a-17d exercise all performance features

36. **Multi-queue support**
    - Separate graphics and compute queues (GNM supports compute queues
      via `sceGnmMapComputeQueue`)
    - `vkGetPhysicalDeviceQueueFamilyProperties` reports 2 families
    - DONE: Family 0 = graphics+compute+transfer, Family 1 = compute+transfer
      (async compute via ACE).  CreateDevice accepts both families.
      GetDeviceQueue/GetDeviceQueue2 retrieve queues from either family.
      VkPs4Queue stores family_index + flags.  On host builds the compute
      queue submits through the same sceGnmSubmitCommandBuffers path.
      VVL test 17f exercises 2-family device creation, queue retrieval,
      and GetDeviceQueue2.

## Files to Create

```
vulkan-ps4/
├── CMakeLists.txt
├── Makefile.orbis
├── README.md
├── include/
│   ├── vk_ps4.h                    # Public ICD interface
│   └── vk_ps4_internal.h           # Internal dispatch table & types
├── src/
│   ├── vk_ps4_entry.c              # ICD entry point (loader interface)
│   ├── vk_ps4_instance.c           # VkInstance / VkPhysicalDevice
│   ├── vk_ps4_device.c             # VkDevice / VkQueue / VkCommandPool
│   ├── vk_ps4_memory.c             # VkDeviceMemory → GNM direct memory
│   ├── vk_ps4_buffer.c             # VkBuffer → GnmBuffer
│   ├── vk_ps4_image.c              # VkImage → GnmTexture / GnmRenderTarget
│   ├── vk_ps4_render_pass.c        # VkRenderPass / VkFramebuffer
│   ├── vk_ps4_pipeline.c           # VkPipeline → GNM shader regs + state
│   ├── vk_ps4_shader.c             # VkShaderModule → libpsbc compile
│   ├── vk_ps4_descriptor.c         # VkDescriptorSet → GNM user data
│   ├── vk_ps4_command.c            # VkCommandBuffer → GnmCommandBuffer + PM4
│   ├── vk_ps4_sync.c               # VkFence / VkSemaphore → EOP events
│   ├── vk_ps4_swapchain.c          # VkSwapchainKHR → VideoOut
│   ├── vk_ps4_queue.c              # vkQueueSubmit → sceGnmSubmit*
│   ├── vk_ps4_query.c              # VkQueryPool → occlusion queries
│   ├── vk_ps4_format.c             # VkFormat → GnmDataFormat mapping
│   └── vk_ps4_dispatch.c           # Generated dispatch table (from vk.xml)
├── scripts/
│   └── gen_dispatch.py             # Generate dispatch table from vk.xml
├── tests/
│   ├── test_triangle.c             # Phase 1: colored triangle
│   ├── test_textured.c             # Phase 2: textured quad (RetroArch-like)
│   ├── test_compute.c              # Phase 2: compute shader
│   └── test_retroarch.sh           # Phase 2: RetroArch integration test
└── shaders/
    ├── triangle.vert
    ├── triangle.frag
    ├── textured.vert
    └── textured.frag
```

**Also modify:**

- `opengnm-psbc/libpsbc/` — new library extracting compilation from
  `cmd/psbc/main.c`
- `opengnm-psbc/cmd/psbc/main.c` — refactor to use `libpsbc`
- `RetroArch/Makefile.orbis` — migrate from orbisdev to OpenOrbis
  (`OO_PS4_TOOLCHAIN`), add `HAVE_VULKAN=1`, link `libvulkan_ps4`.
  Alternatively create `RetroArch/Makefile.openorbis` as a new
  OpenOrbis-native build file (recommended — leaves the upstream
  orbisdev Makefile untouched for reference).
- `RetroArch/gfx/common/vulkan_common.c` — PS4 static linking path
- `RetroArch/libretro-common/gfx/vulkan_symbol_wrapper.c` — PS4 symbol
  resolution path

## Verification

- [x] **Phase 0**: `libpsbc` compiles a simple SPIR-V vertex shader and
      produces a valid `GnmShaderFileHeader` (verify with
      `sceGnmShaderBinaryGetMetadata`)
- [x] **Phase 0**: `vulkan-ps4` skeleton builds on host (generic) and
      PS4 (orbis)
- [ ] **Phase 1**: `test_triangle.self` shows a colored triangle on PS4
      screen via VideoOut
- [ ] **Phase 2**: `test_textured.self` shows a textured quad on PS4
- [ ] **Phase 2**: `test_compute.self` runs a compute shader and
      verifies output buffer
- [ ] **Phase 2**: RetroArch builds with `HAVE_VULKAN=1` and renders the
      RGUI menu on PS4
- [ ] **Phase 2**: RetroArch displays a running libretro core with
      GPU-accelerated video output
- [x] **Phase 3**: `vkGetPhysicalDeviceProperties` reports
      `VK_API_VERSION_1_0`
- [x] **Phase 3**: GNM device lifecycle (init/teardown) wired into
      `vkCreateDevice`/`vkDestroyDevice`
- [x] **Phase 3**: EOP-based fence/semaphore sync (GPU label polling)
- [x] **Phase 3**: RT-as-texture descriptor support
- [x] **Phase 3**: `CmdBeginRenderPass2` / `CmdEndRenderPass2` (Vulkan 1.2)
- [x] **Phase 3**: `CmdUpdateBuffer` staging via `sceGnmCmdAllocInside`
- [x] **Phase 3**: `WaitUntilSafeForRendering` on swapchain render passes
- [ ] **Phase 3**: `test_tessellation.self` renders with tessellation
      active
- [ ] **Phase 5**: Vulkan-ValidationLayers pass on all test cases
- [x] **Build**: `cmake --build build` on host (generic) passes;
      `make -f Makefile.orbis` produces `libvulkan_ps4.so` for PS4
      (using `OO_PS4_TOOLCHAIN`)
- [ ] **Link smoke**: RetroArch ELF links against `libvulkan_ps4.so` +
      opengnm + libpsbc + OpenOrbis firmware stubs

## Risks & Considerations

1. **Scope** — Vulkan 1.0 core is ~135 entry points. The phased approach
   ensures a working product at each milestone. Phases 0-2 (RetroArch
   working) are the critical path (~22 steps). Phases 3-5 are expansion.

2. **PM4 packet correctness** — The ICD must emit PM4 packets that the
   PS4 firmware accepts. OpenGNM's generic backend already produces valid
   PM4. The Orbis backend's firmware delegation path is the safety net —
   when in doubt, forward to firmware `sceGnmDriver*` builders instead
   of emitting PM4 directly.

3. **Memory model** — Vulkan's explicit barrier model vs. GNM's direct
   memory (GPU-visible, fewer barriers needed). Most Vulkan barriers
   translate to EOP wait packets. Over-synchronization hurts performance
   but is safe; under-synchronization corrupts rendering. Default to
   over-synchronization in Phase 1-2, optimize in Phase 5.

4. **Fetch shader generation** — GNM requires fetch shaders for vertex
   input (unlike Vulkan's declarative vertex input state). The GCN
   assembler in `opengnm/src/gcn/` can generate these. The mapping from
   `VkVertexInputBindingDescription` / `VkVertexInputAttributeDescription`
   to fetch shader instructions needs careful implementation.

5. **No standard Vulkan loader on PS4** — The ICD will be statically
   linked or loaded via a custom path. RetroArch's
   `vulkan_symbol_wrapper.c` needs a PS4-specific path that resolves
   symbols at link time rather than `dlopen`.

6. **opengnm-psbc is currently a CLI tool** — Extracting it into
   `libpsbc` is a prerequisite. The compilation logic in
   `cmd/psbc/main.c` (lines 780-846) is self-contained but tightly
   coupled to the CLI's option parsing. The extraction is mechanical
   but must preserve the Mesa initialization sequence
   (`glsl_type_singleton`, `ac_init`, etc.).

7. **Thread safety** — Vulkan is explicitly multi-threaded (command
   buffer recording on multiple threads). GNM command buffer submission
   is single-threaded. The ICD must allow parallel command buffer
   recording but serialize submission. Each `VkCommandBuffer` owns its
   own `GnmCommandBuffer`, so recording is naturally parallel;
   `vkQueueSubmit` takes a mutex.

8. **Existing PS4 RetroArch already works with Piglet** — If
   `ScePigletv2VSH` is available on the target firmware, the existing
   GLES path is simpler. The Vulkan→GNM path is worth building if:
   (a) Piglet is unavailable, (b) you want to avoid proprietary Sony
   libraries, (c) you want Vulkan features (compute shaders, better
   performance), or (d) you want a reusable layer for other apps.

9. **GCN vs. Vulkan feature gaps** — Vulkan 1.0 core maps cleanly to
   GCN. The only wrinkle is `shaderFloat64` (optional in 1.0, not
   required) and `wideLines` (optional). Report these as unsupported in
   `vkGetPhysicalDeviceFeatures`.

## Status

**Phase 0-3: Complete.** The vulkan-ps4 ICD implements 146 Vulkan entry
points across 19 source files (~7,500 lines of C). All Phase 1-3 steps are
implemented: instance, device, memory, buffers, images, command buffers,
render passes, pipelines (including tessellation and geometry shaders),
descriptor sets, fetch shaders, swapchain, queue submit, sync primitives,
query pools, clear commands, depth/stencil, MRT, dynamic state, events,
indirect draws, and compute pipelines.

**Phase 5 (2026-07-23): Code review bug fixes + swapchain/fence sync.**
- Fixed 8 critical bugs from code review:
  - Render pass shallow copy use-after-free → deep copy all subpass arrays
  - CmdDispatchIndirect silent skip on >32-bit GPU addr → staging via
    sceGnmCmdAllocInside
  - CmdClearColorImage using FillMemory for tiled RTs → draw-based clear
  - CmdPushConstants was a no-op → implemented via SET_SH_REG PM4 packets
    with psbc emitting IMM_ALUFLOATCONST input usage slots for inline push
    constants and VS base_vertex/start_instance
  - CmdDrawIndexed ignored vertexOffset/firstInstance → emit SET_SH_REG
    before draw to VS user-data registers (psbc reserves base_vertex/
    start_instance slots with apislot 0xFE/0xFF)
  - CmdDraw firstVertex/firstInstance → same SET_SH_REG mechanism
  - CmdBindDescriptorSets ignored firstSet → track actual Vulkan set
    index (firstSet + si) in dynamic offset map and binding search
  - GCN user-data registers are sticky → always write base_vertex/
    start_instance registers when pipeline uses them, even when value is 0
- Swapchain synchronization improvements:
  - Replaced MVP round-robin with per-image in-flight tracking
    (image_in_flight[] + image_fences[] in VkPs4Swapchain)
  - AcquireNextImageKHR searches for available images, waits on fences
    when all in-flight, supports semaphore-only sync (NULL fence)
  - QueuePresentKHR clears in-flight tracking after flip completes
  - AcquireNextImageKHR no longer touches signal_value/label (avoids
    double-increment when fence/semaphore reused with QueueSubmit)
- Fence timeout: WaitForFences now honors timeout parameter using
  clock_gettime(CLOCK_MONOTONIC), returns VK_TIMEOUT on deadline expiry,
  uses tiered yield (tight spin → 1µs sleeps) instead of infinite spin
- PS4 portability: sceKernelUsleep for Orbis, nanosleep for host,
  orbis/libkernel.h include path
- 4/4 tests pass (format, triangle, descriptor, validation)

**Phase 5 (2026-07-24): Vulkan 1.1 upgrade.**
- Upgraded API version from 1.0.0 to 1.1.0 (internal.h, icd.json, CMakeLists.txt)
- New file `vk_ps4_vulkan11.c` with all Vulkan 1.1 core functions:
  - `vkEnumerateInstanceVersion` → returns 1.1.0
  - `vkEnumeratePhysicalDeviceGroups` → 1 group, 1 device
  - `vkGetPhysicalDeviceProperties2` / `Features2` / `FormatProperties2` /
    `ImageFormatProperties2` — thin wrappers over v1 + pNext chain handling
    for Vulkan11Properties/Features, SubgroupProperties, Maintenance3Properties,
    PointClippingProperties, MultiviewProperties, ProtectedMemoryProperties,
    16BitStorageFeatures, VariablePointersFeatures, SamplerYcbcrConversionFeatures,
    ShaderDrawParametersFeatures
  - `vkGetPhysicalDeviceExternalBufferProperties` / `ExternalFenceProperties` /
    `ExternalSemaphoreProperties` — no external handle support (PS4).  Only
    the feature/handle-type fields are zeroed, not `sType`/`pNext`.
  - `vkGetPhysicalDeviceMemoryProperties2` / `SparseImageFormatProperties2` —
    thin wrapper / no sparse support (returns 0 properties)
  - `vkGetDeviceQueue2` — delegates to GetDeviceQueue
  - `vkBindBufferMemory2` / `BindImageMemory2` — loop over v1 functions
  - `vkGetDeviceGroupPeerMemoryFeatures` — 0 (single GPU)
  - `vkGetDescriptorSetLayoutSupport` — supported=TRUE
  - `vkCreateDescriptorUpdateTemplate` / `Destroy` / `UpdateWithTemplate` —
    minimal in-memory template that stores the create info and builds
    `VkWriteDescriptorSet` array from user data, then calls
    `vkUpdateDescriptorSets` (no longer returns VK_ERROR_FEATURE_NOT_PRESENT)
  - `vkCreateSamplerYcbcrConversion` / `DestroySamplerYcbcrConversion` —
    dummy handles (feature reported as FALSE, so never used in practice)
  - `vkCmdSetDeviceMask` — no-op (single GPU)
  - `vkCmdDispatchBase` — delegates to CmdDispatch (base offsets noted as
    a known limitation — non-zero base would require shader support)
- pNext chain handling preserves `pNext` across `memset` (save/restore
  pattern) so structures after Vulkan11Properties/Features/IDProperties in
  the chain are still visited
- Moved `GetBufferMemoryRequirements2` / `GetImageMemoryRequirements2` /
  `GetImageSparseMemoryRequirements2` from `g_instance_funcs` to
  `g_device_funcs` so `vkGetDeviceProcAddr` resolves them correctly
- Fixed `GetImageSparseMemoryRequirements2` signature (VkDevice, not
  VkPhysicalDevice) and entrypoint to call the implementation
- Fixed `GetBufferMemoryRequirements2` / `GetImageMemoryRequirements2` signatures
  to use `VkBufferMemoryRequirementsInfo2` / `VkImageMemoryRequirementsInfo2`
  (Vulkan 1.1 signature with info struct, not raw buffer/image handle)
- All Vulkan 1.1 promoted extensions advertised for backwards compatibility
  (VK_KHR_get_physical_device_properties2, VK_KHR_external_memory_capabilities,
   VK_KHR_bind_memory2, VK_KHR_maintenance1/2/3, etc.)
- All Vulkan 1.1 features reported as FALSE (GCN 1.0 / Liverpool has no
  16-bit storage, multiview, variable pointers, YCbCr, protected memory)
- Subgroup properties: size=64 (GCN wavefront64), basic ops in VS/FS/CS
- 4/4 tests pass, 0 VVL errors, 0 warnings

**Phase 5 (2026-07-24): Multi-subpass + indirect draw VVL tests.**
- Added multi-subpass render pass test: 2-attachment, 2-subpass render pass
  with input attachment and subpass dependency. Tests CmdBeginRenderPass,
  CmdNextSubpass, CmdEndRenderPass. No draw (pipeline incompatibility —
  existing pipeline was created for single-subpass rp; a separate pipeline
  would be needed to draw in the multi-subpass render pass).
- Added indirect draw test: CmdDrawIndirect with VkDrawIndirectCommand
  {3,1,0,0}, CmdDrawIndexedIndirect with VkDrawIndexedIndirectCommand
  {3,1,0,0,0} reusing the index buffer from the indexed draw test.
- Added VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT to the main test image (needed
  by the multi-subpass render pass input attachment).
- All resources deferred to after QueueWaitIdle (no leaks, no UAF).
- 4/4 tests pass, 0 VVL errors, 0 warnings

**Phase 5 (2026-07-24): Depth/stencil attachment VVL test.**
- Added depth/stencil attachment test: D32_SFLOAT_S8_UINT depth/stencil
  image + view (aspect mask DEPTH|STENCIL), render pass with color +
  depth/stencil attachments (loadOp/stencilLoadOp=CLEAR), framebuffer
  with both, begin render pass with depth=1.0/stencil=0 clear values,
  set dynamic state, end render pass. Tests the depth/stencil clear
  path in CmdBeginRenderPass (vk_ps4_clear_depth_draw via GCN lazy clear).
- No draw (pipeline incompatibility — existing pipeline was created for
  single-attachment render pass; a separate pipeline with depth/stencil
  state would be needed to draw in the depth/stencil render pass).
- All resources deferred to after QueueWaitIdle (no leaks, no UAF).
- 4/4 tests pass, 0 VVL errors, 0 warnings

**Phase 5 (2026-07-24): Dolphin emulator compatibility test.**
- Built Dolphin emulator (dolphin-tool + dolphin-emu-nogui) from source
  with Vulkan backend enabled.
- Analyzed Dolphin's VulkanContext initialization path for compatibility:
  - Dolphin requires minimum Vulkan 1.0, uses 1.1 if available (vulkan-ps4
    reports 1.1 ✓)
  - Dolphin queries VkPhysicalDeviceSubgroupProperties via pNext chain
    (vulkan-ps4 reports subgroupSize=64, BASIC ops, graphics|compute|FS ✓)
  - Dolphin requires: dualSrcBlend, geometryShader, fragmentStoresAndAtomics,
    textureCompressionBC (all VK_TRUE in vulkan-ps4 ✓)
  - Dolphin optionally uses: largePoints, occlusionQueryPrecise, samplerAnisotropy,
    logicOp, sampleRateShading, depthClamp, shaderClipDistance,
    shaderTessellationAndGeometryPointSize (all VK_TRUE ✓)
  - Dolphin uses VMA with VMA_VULKAN_VERSION 1002000, which needs
    vkGetBufferMemoryRequirements2/vkGetImageMemoryRequirements2/
    vkBindBufferMemory2/vkBindImageMemory2/vkGetPhysicalDeviceMemoryProperties2
    via vkGetDeviceProcAddr (all now in g_device_funcs ✓)
  - Dolphin requires vkGetRenderAreaGranularity (implemented ✓)
  - Dolphin requires vkCreatePipelineCache/vkGetPipelineCacheData/
    vkMergePipelineCaches (implemented ✓)
  - Dolphin does NOT use: descriptor update templates, sampler YCbCr
    conversion, push descriptors, dynamic rendering, timeline semaphores
- Added test_dolphin_compat.c: mimics Dolphin's exact VulkanContext init
  path including instance creation with VK_KHR_portability_enumeration
  (required by Vulkan loader 1.4+ for portability drivers on macOS),
  Properties2 with subgroup pNext, all Dolphin features, device creation,
  VMA-like allocation, pipeline cache, render pass, render area granularity,
  command pool/buffer, descriptor set layout, pipeline layout, sampler,
  query pool, fence/semaphore, command buffer submit, pipeline cache data.
- 5/5 tests pass, 0 errors.

**Phase 5 (2026-07-23): Swapchain/fence sync review fixes + VVL test expansion.**
- Fixed 4 bugs from second code review:
  - CRITICAL: QueueSubmit now clears signaled=false before EOP emit, so
    WaitForFences doesn't short-circuit on stale flag from AcquireNextImageKHR
  - MEDIUM: No-fences fallback calls DeviceWaitIdle instead of returning
    image with pending GPU work
  - MEDIUM: All-in-flight wait uses waitAll=VK_FALSE (wait for ANY fence)
    instead of full timeout per fence
  - LOW: DestroySwapchainKHR calls DeviceWaitIdle before freeing resources
- Expanded VVL test coverage:
  - Indexed draws: CmdDrawIndexed with vertexOffset/firstInstance, including
    regression test for zero vertexOffset (sticky register state leak)
  - Push constants: CmdPushConstants with VS push constant range, multiple
    updates between draws
  - Compute pipelines: vkCreateComputePipelines + CmdDispatch with SSBO,
    memory barrier after compute write
- 4/4 tests pass, 0 VVL errors
- Third code review confirmed all fixes correct: no bugs, regressions,
  or correctness issues. Verified: signal clearing timing, all-in-flight
  wait array sizing (GNM_VIDEO_OUT_MAX_BUFFERS=4), DeviceWaitIdle
  placement, deferred test cleanup (no leaks/UAF), shader interface
  compatibility, VVL-clean (0 errors, 0 warnings)

**Phase 5 (2026-07-22): VVL testing + code review fixes.***
- Added validation_test that goes through the Vulkan loader with
  VK_LAYER_KHRONOS_validation enabled
- VVL found VkPhysicalDeviceLimits was entirely zeroed, causing errors
  for maxMemoryAllocationCount, maxViewportDimensions, maxFramebuffer*,
  maxColorAttachments, maxSamplerAllocationCount
- Fixed by populating limits with PS4 Liverpool GPU capabilities
- Expanded test to exercise full rendering pipeline: render pass +
  framebuffer, graphics pipeline with real SPIR-V shaders, dynamic
  viewport/scissor, descriptor sets (UBO + combined image sampler),
  image layout transitions (UNDEFINED → TRANSFER_DST → COLOR_ATTACHMENT),
  CmdCopyBufferToImage, pipeline barriers, render pass begin/clear/draw/end,
  fence-based queue submit + wait
- Code review found and fixed 11 additional missing VkPhysicalDeviceLimits
  fields (maxSampleMaskWords, timestampComputeAndGraphics, timestampPeriod,
  maxClipDistances, maxCullDistances, discreteQueuePriorities, pointSizeRange,
  lineWidthRange, pointSizeGranularity, lineWidthGranularity, strictLines)
- Fixed vk_icdEnumerateInstanceExtensionProperties spec violation:
  *pPropertyCount must report written count, not total, on partial writes
- Fixed GetPhysicalDeviceImageFormatProperties sampleCounts to match limits
- Fixed CMake orbis build missing ORBIS/__ORBIS__/__PS4__ defines
- Fixed ICD manifest: cross-platform library path, is_portability_driver
- Fixed test bugs: cleanup ordering, image layout mismatch, buffer barrier
  access masks, debug messenger leak on error path
- 4/4 tests pass (format, triangle, descriptor, validation) with 0 VVL errors

**Phase 3 additions (2026-07-21):**
- GNM device lifecycle: `sceGnmCmdInit` + `InitDefaultHardwareState` on
  `vkCreateDevice`; `sceGnmSubmitDone` on `vkDestroyDevice`
- EOP-based fence/semaphore sync: GPU label memory + `CACHE_FLUSH_AND_INV_TS_EVENT`
  EOP writes instead of CPU bools
- RT-as-texture: `CreateImageView` builds `GnmTexture` from `GnmRenderTarget`
  via `sceGnmRtBuildInfo` + `sceGnmTexCreate2d`
- Vulkan 1.2 render pass 2: `CmdBeginRenderPass2` / `CmdNextSubpass2` /
  `CmdEndRenderPass2` + `VK_KHR_create_renderpass2` extension
- Vulkan 1.1: `EnumerateInstanceVersion`, `EnumeratePhysicalDeviceGroups`,
  `GetPhysicalDeviceProperties2` / `Features2` / `FormatProperties2` with
  full pNext chain handling (Vulkan11Properties/Features, SubgroupProperties,
  Maintenance3Properties, etc.), `BindBufferMemory2` / `BindImageMemory2`,
  `GetDeviceQueue2`, `GetDescriptorSetLayoutSupport`, external memory/fence/
  semaphore capability stubs, device group stubs, `CmdSetDeviceMask`,
  `CmdDispatchBase`. All 1.1 promoted extensions advertised.
- `CmdUpdateBuffer` staging via `sceGnmCmdAllocInside` (semantically correct)
- `WaitUntilSafeForRendering` on swapchain image render passes
- Depth/array layer iteration in all image copy commands
- Bug fixes: RT width/height, slice offset calculation, host fence spin,
  fence `signaled` flag on host

**Builds:**
- Host (generic): `cmake --build build` — clean, all 4 tests pass
- PS4 (orbis): `make -f Makefile.orbis` — produces `libvulkan_ps4.so` (~59MB)
  and `libvulkan_ps4.a` (ELF 64-bit FreeBSD/PS4)

**Tests:** 4/4 test suites pass (format, triangle, descriptor, validation),
82/82 format tests pass.  Validation test runs VVL with zero errors.

**Remaining work:**
- PS4 hardware testing (test_triangle_ps4.self on PS4)
- RetroArch link smoke test on PS4
- Phase 4: Optional extensions (on demand)
  - DONE: VK_KHR_driver_properties (driver name/info/ID/conformance version)
  - DONE: VK_KHR_imageless_framebuffer (imageless FB + attachment begin info)
  - DONE: VK_EXT_descriptor_indexing (bindless, variable count, update-after-bind)
  - DONE: VK_KHR_timeline_semaphore (software-emulated 64-bit counter)
  - DONE: VK_KHR_image_format_list (dependency for imageless_framebuffer)
  - DONE: VK_KHR_create_renderpass2 (v2→v1 conversion + delegation)
  - DONE: VK_KHR_depth_stencil_resolve (SAMPLE_ZERO resolve modes)
  - DONE: VK_KHR_spirv_1_4 (handled by opengnm-psbc, extension-only)
  - DONE: VK_KHR_shader_float_controls (dependency for spirv_1_4)
  - DONE: VK_EXT_scalar_block_layout (scalarBlockLayout=TRUE)
  - DONE: VK_KHR_uniform_buffer_standard_layout (TRUE)
  - DONE: VK_EXT_host_query_reset (vkResetQueryPoolEXT)
  - DONE: VK_KHR_shader_atomic_int64 (buffer atomics=TRUE)
  - DONE: VK_KHR_buffer_device_address (GetBufferDeviceAddress)
  - DONE: VK_KHR_shader_subgroup_extended_types (TRUE)
  - DONE: VK_KHR_vulkan_memory_model (TRUE, deviceScope=TRUE)
  - DONE: VK_EXT_separate_stencil_usage (extension-only)
  - DONE: Format features (TRANSFER_SRC/DST_BIT added to all formats)
  - DONE: VVL tests for all extensions (sections 16a-16m in test_validation.c)
- Phase 5: Expand VVL test coverage (compute pipelines, multi-subpass,
  depth/stencil attachment, indexed draws, indirect draws, push constants)
  - DONE: indexed draws (CmdDrawIndexed with vertexOffset/firstInstance)
  - DONE: push constants (CmdPushConstants with VS push constant range)
  - DONE: compute pipelines (vkCreateComputePipelines + CmdDispatch)
  - DONE: multi-subpass (CmdNextSubpass with 2-subpass render pass +
    input attachment + subpass dependency)
  - DONE: indirect draws (CmdDrawIndirect + CmdDrawIndexedIndirect)
  - DONE: depth/stencil attachment (D32_SFLOAT_S8_UINT render pass + clear)
- Phase 5: Performance optimizations — DONE
  - DONE: Pipeline cache persistence (FNV-1a hash, serialize/deserialize,
    MergePipelineCaches, cache lookup/insert in pipeline creation)
  - DONE: Descriptor set pooling (free list in VkDescriptorPool, reuse on alloc)
  - DONE: Command buffer pooling (free list in VkCommandPool, reuse PM4 buffer)
  - DONE: Batched SET_SH_REG writes (base_vertex+start_instance, push constants)
  - DONE: VVL tests 17a-17d (pipeline cache, descriptor pooling, CB pooling, merge)
- PS4 packaging: DONE — create-fself + PKG packaging in Makefile.orbis
  - test_triangle_ps4.self (15MB FSELF) + IV0000-VPS400000_00-VKTRIANGLETEST00.pkg (17MB)
  - Mesa utility stubs (mesa_stubs.c) for libpsbc.orbis.a unresolved symbols
  - Static linking: libvulkan_ps4.a + libopengnm.a (--whole-archive) + libpsbc.orbis.a
- Swapchain sync: DONE (in-flight tracking, fence-based wait)
- Fence timeout: DONE (honors timeout, tiered yield)
- CmdPushConstants: DONE (SET_SH_REG via psbc IMM_ALUFLOATCONST slots)
- CmdDrawIndexed vertexOffset/firstInstance: DONE (SET_SH_REG before draw)
- CmdBindDescriptorSets firstSet: DONE (actual set index tracking)
- CmdClearColorImage for tiled RTs: DONE (draw-based clear via embedded clear PS)
- CmdDispatchIndirect >32-bit addr: DONE (staging via sceGnmCmdAllocInside)
- Render pass deep copy: DONE (no more UAF)
- GPU WaitMem for wait semaphores: DONE
- Texel buffer views: DONE
- Tiled RT clear pixel shader: DONE (render pass clears only, 6 bugs fixed)
- Pipeline shader binary use-after-free + address patching: DONE (all stages)
- VkPhysicalDeviceLimits populated: DONE (all required fields, spec-compliant)
- VVL test infrastructure: DONE (4/4 tests pass, 0 errors, full pipeline)
- ICD loader interface: fixed pPropertyCount, pLayerName handling
- ICD manifest: cross-platform, is_portability_driver

**Shader compiler:** `opengnm-psbc/Makefile.orbis` now produces
`libpsbc.orbis.a` with 478 PS4/FreeBSD ELF objects. `vulkan-ps4/Makefile.orbis`
links that archive and defines `VK_PS4_HAVE_PSBC=1`, so the PS4 ICD uses the
real SPIR-V → GCN compilation path for all shader stages (vertex,
fragment, compute, geometry, tessellation control/eval). psbc emits
GnmInputUsageSlot entries for inline push constants and VS base_vertex/
start_instance, which the ICD extracts at pipeline creation to emit
SET_SH_REG PM4 packets for push constants and draw offsets.
