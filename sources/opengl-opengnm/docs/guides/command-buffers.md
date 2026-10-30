# Command Buffers

This guide covers command buffer allocation, initialization, and building draw
commands using the `sceGnmDrawCmd*` API.

---

## Two Command Buffer Layers

opengnm has two layers of command buffer APIs:

| Layer | Header | Description |
|-------|--------|-------------|
| **Command Buffer** | `gnm_commandbuffer.h` | Low-level buffer management (`GnmCommandBuffer`) |
| **Draw Command Buffer** | `gnm_drawcommandbuffer.h` | High-level draw/state commands (`sceGnmDrawCmd*`) |

The Draw Command Buffer API wraps the Command Buffer and provides typed
functions for each GPU operation.

---

## GnmCommandBuffer

`GnmCommandBuffer` (0x40 bytes) is the low-level command buffer container:

```c
typedef struct {
    uint32_t* beginptr;      // Start of command memory
    uint32_t* endptr;        // End of command memory
    uint32_t* cmdptr;        // Current write position

    GnmCommandCallback callback;
    GnmCommandBufferFlags flags;

    uint64_t _unused;
    uint32_t sizedwords;     // Number of dwords written
    uint32_t _unused2;
} GnmCommandBuffer;
```

### Initialization

```c
#include <gnm_commandbuffer.h>

uint32_t cmdMem[65536];  // 256KB
GnmCommandBuffer cmd = sceGnmCmdInit(
    cmdMem,
    sizeof(cmdMem),
    NULL,   // optional callback
    NULL    // callback user data
);
```

### Reset

```c
sceGnmCmdReset(&cmd);
// cmd.cmdptr = cmd.beginptr;
```

### Allocate Inside

Allocate a sub-region within the command buffer (for indirect buffers):

```c
void* ptr = sceGnmCmdAllocInside(&cmd, sizeInDwords, alignmentInDwords);
```

### Validation

```c
#include <gnm_helpers.h>

GnmCommandBufferValidationInfo info;
GnmError err = sceGnmCmdValidate(&cmd, &info);
// info.capacitydwords, info.useddwords, info.remainingdwords, info.message
```

---

## Draw Command Buffer API

The `sceGnmDrawCmd*` functions build PM4 packets into a `GnmCommandBuffer`.
They are the primary way to construct GPU command streams.

### Init Default Hardware State

Before any draw, initialize the GPU to its default state:

```c
sceGnmDrawCmdInitDefaultHardwareState(&cmd);
```

This emits the full default hardware state packet (thousands of dwords).

---

## Draw Commands

### Indexed Draw

```c
sceGnmDrawCmdSetIndexBuffer(&cmd, indexBufferAddr);
sceGnmDrawCmdSetIndexSize(&cmd, GNM_INDEX_32, GNM_POLICY_LRU);
sceGnmDrawCmdSetIndexCount(&cmd, indexCount);
sceGnmDrawCmdDrawIndex(&cmd, indexCount, indexBufferAddr);
```

### Auto-Indexed Draw (no index buffer)

```c
sceGnmDrawCmdDrawIndexAuto(&cmd, vertexCount);
```

### Draw with Modifier

```c
GnmDrawModifier mod = { .rendertargetsliceoffset = 0 };
sceGnmDrawCmdDrawIndexAuto2(&cmd, vertexCount, mod);
```

### Indirect Draws

```c
// Indirect draw (args in GPU memory)
sceGnmDrawCmdDrawIndirect(
    &cmd, dataOffset, GNM_STAGE_VS, vertexSgprOffset, instanceSgprOffset
);

// Indirect indexed draw
sceGnmDrawCmdDrawIndexIndirect(
    &cmd, dataOffset, GNM_STAGE_VS, vertexSgprOffset, instanceSgprOffset
);

// Multi-draw indirect
sceGnmDrawCmdDrawIndirectMulti(
    &cmd, dataOffset, maxCount, GNM_STAGE_VS, vertexSgprOffset, instanceSgprOffset
);
```

### Draw Offset

```c
sceGnmDrawCmdDrawIndexOffset(&cmd, indexOffset, indexCount, mod);
```

---

## State Setup

### Render Targets

```c
sceGnmDrawCmdSetRenderTarget(&cmd, 0, &rt);
sceGnmDrawCmdSetRenderTargetMask(&cmd, 0x1);
sceGnmDrawCmdSetDepthRenderTarget(&cmd, &drt);
sceGnmDrawCmdSetDepthClearValue(&cmd, 1.0f);
```

### Viewport and Scissor

```c
GnmSetViewportInfo vp = {
    .dmin = 0.0f, .dmax = 1.0f,
    .scale = {960.0f, 540.0f, 0.5f},
    .offset = {960.0f, 540.0f, 0.5f}
};
sceGnmDrawCmdSetViewport(&cmd, 0, &vp);

sceGnmDrawCmdSetScreenScissor(&cmd, 0, 0, 1920, 1080);
```

### Guard Bands

```c
sceGnmDrawCmdSetGuardBands(&cmd, 1.0f, 1.0f, 1.0f, 1.0f);
```

### Hardware Screen Offset

```c
sceGnmDrawCmdSetHwScreenOffset(&cmd, offsetX, offsetY);
```

### Primitive Type

```c
sceGnmDrawCmdSetPrimitiveType(&cmd, GNM_PT_TRILIST);
```

### Instancing

```c
sceGnmDrawCmdSetNumInstances(&cmd, instanceCount);
sceGnmDrawCmdSetInstanceStepRate(&cmd, rate0, rate1);
```

### Indirect Args

```c
sceGnmDrawCmdSetIndirectArgs(&cmd, &drawArgs);
sceGnmDrawCmdSetIndexedIndirectArgs(&cmd, &drawIndexedArgs);
```

---

## Control Registers

### Blend Control

```c
GnmBlendControl blend = {
    .blendenabled = true,
    .colorfunc = GNM_COMB_DST_PLUS_SRC,
    .colorsrcmult = GNM_BLEND_SRC_ALPHA,
    .colordstmult = GNM_BLEND_ONE_MINUS_SRC_ALPHA,
    .separatealphaenable = false,
};
sceGnmDrawCmdSetBlendControl(&cmd, 0, &blend);

sceGnmDrawCmdSetBlendColor(&cmd, 0.5f, 0.5f, 0.5f, 0.5f);
```

### Depth-Stencil Control

```c
GnmDepthStencilControl ds = {
    .depthenable = true,
    .zwrite = true,
    .zfunc = GNM_DEPTH_COMPARE_LESS,
    .stencilenable = false,
};
sceGnmDrawCmdSetDepthStencilControl(&cmd, &ds);
```

### DB Render Control

```c
GnmDbRenderControl dbCtrl = {
    .depthclearenable = true,
};
sceGnmDrawCmdSetDbRenderControl(&cmd, &dbCtrl);
```

### Primitive Setup

```c
GnmPrimitiveSetup setup = {
    .cullmode = GNM_CULL_BACK,
    .frontface = GNM_FACE_CCW,
    .frontmode = GNM_FILL_SOLID,
    .backmode = GNM_FILL_SOLID,
};
sceGnmDrawCmdSetPrimitiveSetup(&cmd, &setup);
```

### Viewport Transform

```c
GnmViewportTransformControl vtc = {
    .scalex = true, .offsetx = true,
    .scaley = true, .offsety = true,
    .scalez = true, .offsetz = true,
};
sceGnmDrawCmdSetViewportTransformControl(&cmd, &vtc);
```

---

## Resource Binding (User Data)

Shader user data registers are how resources are bound to shader stages:

```c
// Buffer (V#)
sceGnmDrawCmdSetVsharpUserData(&cmd, GNM_STAGE_PS, 0, &buffer);

// Texture (T#)
sceGnmDrawCmdSetTsharpUserData(&cmd, GNM_STAGE_PS, 1, &texture);

// Sampler (S#)
sceGnmDrawCmdSetSsharpUserData(&cmd, GNM_STAGE_PS, 2, &sampler);

// Raw pointer
sceGnmDrawCmdSetPointerUserData(&cmd, GNM_STAGE_PS, 3, ptr);
```

### User Data Slot Limits

| Slot Type | Max Slots |
|-----------|-----------|
| T# (texture) | 8 |
| S# (sampler) | 12 |
| V# (buffer) | 12 |
| Pointer | 14 |

---

## Synchronization

### Event Write (End of Pipeline)

```c
sceGnmDrawCmdEventWriteEop(
    &cmd,
    GNM_CACHE_FLUSH_AND_INV_TS_EVENT,  // event type
    gpuAddr,                            // GPU address to write to
    GNM_DATA_SEL_SEND_DATA32,          // data selection
    1                                   // immediate value
);
```

### Wait on Memory

```c
sceGnmDrawCmdWaitMem(
    &cmd,
    GNM_WAIT_REG_MEM_FUNC_EQUAL,  // comparison function
    gpuAddr,                       // GPU address
    1,                             // reference value
    0xffffffff                     // mask
);
```

### Wait for Graphics Write

```c
sceGnmDrawCmdWaitGraphicsWrite(&cmd, GNM_ACQUIRE_TARGET_CB0);
```

### Wait for Safe Rendering

```c
sceGnmDrawCmdWaitUntilSafeForRendering(&cmd, videoHandle, bufferIndex);
```

### Memory Operations

```c
// Fill a GPU memory range with a value
sceGnmDrawCmdFillMemory(&cmd, gpuAddr, sizeBytes, 0);

// Copy GPU memory
sceGnmDrawCmdCopyMemory(&cmd, dstAddr, srcAddr, sizeBytes);
```

---

## Transform Feedback (Stream-Out)

```c
sceGnmDrawCmdSetStreamOutConfig(&cmd, streamEn, rastStream, bufferEn);
sceGnmDrawCmdSetStreamOutBuffer(&cmd, slot, gpuAddr, size, stride);
```

---

## Occlusion Queries

```c
sceGnmDrawCmdResetQuery(&cmd, queryGpuAddr);
sceGnmDrawCmdBeginQuery(&cmd, queryGpuAddr);
// ... draw ...
sceGnmDrawCmdEndQuery(&cmd, queryGpuAddr);
```

---

## Dispatch (Compute)

```c
sceGnmDrawCmdSetCsShader(&cmd, &csRegs);
sceGnmDrawCmdDispatchDirect(&cmd, threadsX, threadsY, threadsZ, 0);

// Or indirect
sceGnmDrawCmdDispatchIndirect(&cmd, dataOffset, 0);
```

---

## Submission

After building the command buffer, submit it to the GPU:

```c
#include <gnmdriver.h>

sceGnmSubmitCommandBuffers(
    1,
    (void* const[]){cmdMem},
    (uint32_t[]){cmd.sizedwords * 4},
    NULL, NULL
);
```

See the [Rendering Pipeline Guide](rendering-pipeline.md) for the complete
submit and flip flow.

---

## See Also

- [Command Buffer Reference](../reference/commandbuffer.md)
- [Draw Command Buffer Reference](../reference/drawcommandbuffer.md)
- [Control Registers Reference](../reference/controls.md)
- [Rendering Pipeline Guide](rendering-pipeline.md)
