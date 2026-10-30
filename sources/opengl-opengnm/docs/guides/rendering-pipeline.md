# Rendering Pipeline

This guide walks through the complete rendering pipeline: from VideoOut setup
to command buffer building, draw calls, submission, and frame flipping.

---

## Overview

A typical opengnm rendering frame follows this sequence:

1. **Open VideoOut** — allocate display buffers
2. **Allocate direct memory** — GPU-accessible memory for resources
3. **Create render target** — set up the color/depth targets
4. **Initialize command buffer** — allocate a command buffer in direct memory
5. **Build draw commands** — set state, shaders, and issue draws
6. **Submit command buffers** — send to the GPU via `sceGnmSubmit*`
7. **Flip** — present the rendered frame to the display

---

## 1. Open VideoOut

Use the helper API to open a VideoOut handle with default settings:

```c
#include <gnm_helpers.h>

GnmVideoOutCreateInfo voInfo;
sceGnmVideoOutInitDefaultCreateInfo(&voInfo, 1920, 1080);

GnmVideoOut videoOut;
GnmError err = sceGnmVideoOutOpen(&videoOut, &voInfo);
if (err != GNM_ERROR_OK) {
    // handle error
}
```

`sceGnmVideoOutInitDefaultCreateInfo` sets up a 1920x1080 A8B8G8R8 SRGB
display configuration with 2 buffers, linear tiling, 60Hz flip rate.

### Buffer layout

```c
uint64_t bufferSize, bufferStride;
sceGnmVideoOutCalcBufferLayout(&voInfo, &bufferSize, &bufferStride);
```

### Get a display buffer

```c
void* displayBuffer = sceGnmVideoOutGetBuffer(&videoOut, 0);
```

---

## 2. Allocate Direct Memory

GPU resources live in direct (physical) memory, not regular heap memory:

```c
GnmDirectMemory mem;
sceGnmDirectMemoryAllocate(
    &mem,
    bufferSize * 2,           // size
    GNM_VIDEO_OUT_MEMORY_ALIGNMENT,  // alignment (64KB)
    GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, // memory type
    GNM_PROT_CPU_GPU_RW      // protection
);
```

### Memory types

| Type | Value | Description |
|------|-------|-------------|
| `GNM_DIRECT_MEMORY_TYPE_WC_GARLIC` | 3 | Write-combined Garlic memory |

### Protection flags

| Flag | Value | Description |
|------|-------|-------------|
| `GNM_PROT_CPU_READ` | 0x01 | CPU read access |
| `GNM_PROT_CPU_RW` | 0x02 | CPU read/write |
| `GNM_PROT_GPU_READ` | 0x10 | GPU read access |
| `GNM_PROT_GPU_WRITE` | 0x20 | GPU write access |
| `GNM_PROT_CPU_GPU_RW` | combined | Full CPU+GPU read/write |

---

## 3. Create Render Target

Set up a color render target for the display buffer:

```c
#include <gnm_rendertarget.h>

GnmRenderTargetCreateInfo rtInfo;
sceGnmRtInitColorTargetCreateInfo(
    &rtInfo,
    GNM_FMT_R8G8B8A8_SRGB,  // format
    1920, 1080,             // width, height
    1,                      // num slices
    1, 1,                   // num samples, num fragments
    GNM_TM_DISPLAY_LINEAR_ALIGNED,  // tile mode
    GNM_GPU_BASE            // min GPU mode
);

uint64_t rtSize;
uint32_t rtAlign;
GnmRenderTarget rt;
sceGnmRtCreateColorTarget(
    &rt, displayBuffer,
    GNM_FMT_R8G8B8A8_SRGB, 1920, 1080, 1, 1, 1,
    GNM_TM_DISPLAY_LINEAR_ALIGNED, GNM_GPU_BASE,
    &rtSize, &rtAlign
);
```

---

## 4. Initialize Command Buffer

Command buffers are allocated in direct memory and used to build GPU command
streams:

```c
#include <gnm_commandbuffer.h>

uint32_t cmdBufferMemory[65536];  // 256KB command buffer
GnmCommandBuffer cmd = sceGnmCmdInit(
    cmdBufferMemory,
    sizeof(cmdBufferMemory),
    NULL,   // callback
    NULL    // callback data
);
```

### Reset for a new frame

```c
sceGnmCmdReset(&cmd);
```

---

## 5. Build Draw Commands

Use the `sceGnmDrawCmd*` API to build the command buffer:

```c
#include <gnm_drawcommandbuffer.h>

// Initialize GPU to default hardware state
sceGnmDrawCmdInitDefaultHardwareState(&cmd);

// Set the render target
sceGnmDrawCmdSetRenderTarget(&cmd, 0, &rt);

// Set the screen scissor
sceGnmDrawCmdSetScreenScissor(&cmd, 0, 0, 1920, 1080);

// Set viewport
GnmSetViewportInfo vp = {
    .dmin = 0.0f, .dmax = 1.0f,
    .scale = {960.0f, 540.0f, 0.5f},
    .offset = {960.0f, 540.0f, 0.5f}
};
sceGnmDrawCmdSetViewport(&cmd, 0, &vp);

// Set shaders
sceGnmDrawCmdSetVsShader(&cmd, &vsRegs, 0);
sceGnmDrawCmdSetPsShader(&cmd, &psRegs);

// Set primitive type
sceGnmDrawCmdSetPrimitiveType(&cmd, GNM_PT_TRILIST);

// Draw
sceGnmDrawCmdDrawIndexAuto(&cmd, vertexCount);
```

---

## 6. Submit Command Buffers

Submit the command buffer to the GPU:

```c
#include <gnmdriver.h>

// Submit without flip
sceGnmSubmitCommandBuffers(
    1,                          // count
    (void* const[]){cmdBufferMemory},  // draw command buffer addresses
    (uint32_t[]){cmd.sizedwords * 4},  // DCB sizes in bytes
    NULL,                       // constant command buffer addresses
    NULL                        // CCB sizes
);
```

### Submit with flip

To submit and flip in one call:

```c
sceGnmSubmitAndFlipCommandBuffers(
    1,
    (void* const[]){cmdBufferMemory},
    (uint32_t[]){cmd.sizedwords * 4},
    NULL, NULL,
    videoOut.handle,            // VideoOut handle
    0,                          // buffer index to flip to
    GNM_VIDEO_OUT_FLIP_VSYNC,   // flip mode
    (int64_t)videoOut.frame     // flip arg (frame number)
);
```

### Submit done

After submitting, call:

```c
sceGnmSubmitDone();
```

---

## 7. Flip and Wait

Use the helper to flip and wait for vsync:

```c
sceGnmVideoOutSubmitFlipAndWait(
    &videoOut,
    0,                          // buffer index
    (int64_t)videoOut.frame,    // flip arg
    GNM_VIDEO_OUT_FLIP_VSYNC    // flip mode
);
videoOut.frame++;
videoOut.currentbuffer = (videoOut.currentbuffer + 1) % videoOut.numbuffers;
```

---

## Complete Frame Loop

```c
// One-time setup
GnmVideoOut videoOut;
sceGnmVideoOutOpen(&videoOut, &voInfo);

GnmRenderTarget rt;
sceGnmRtCreateColorTarget(&rt, ...);

uint32_t cmdMem[65536];
GnmCommandBuffer cmd = sceGnmCmdInit(cmdMem, sizeof(cmdMem), NULL, NULL);

// Per-frame
while (running) {
    sceGnmCmdReset(&cmd);
    sceGnmDrawCmdInitDefaultHardwareState(&cmd);
    sceGnmDrawCmdSetRenderTarget(&cmd, 0, &rt);
    sceGnmDrawCmdSetScreenScissor(&cmd, 0, 0, 1920, 1080);

    // ... set shaders, state, draw ...

    sceGnmDrawCmdDrawIndexAuto(&cmd, vertexCount);

    sceGnmSubmitAndFlipCommandBuffers(
        1, (void* const[]){cmdMem},
        (uint32_t[]){cmd.sizedwords * 4},
        NULL, NULL,
        videoOut.handle,
        videoOut.currentbuffer,
        GNM_VIDEO_OUT_FLIP_VSYNC,
        (int64_t)videoOut.frame
    );
    sceGnmSubmitDone();

    videoOut.frame++;
    videoOut.currentbuffer =
        (videoOut.currentbuffer + 1) % videoOut.numbuffers;
}

// Cleanup
sceGnmVideoOutClose(&videoOut);
```

---

## Waiting for Rendering to Complete

Before writing to a display buffer that the GPU may still be using, insert a
wait:

```c
sceGnmDrawCmdWaitUntilSafeForRendering(
    &cmd, videoOut.handle, videoOut.currentbuffer
);
```

For GPU-side synchronization, use event writes and memory waits:

```c
// Write an event at end of pipeline
sceGnmDrawCmdEventWriteEop(
    &cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT,
    gpuAddr, GNM_DATA_SEL_SEND_DATA32, 1
);

// Wait for it on the GPU
sceGnmDrawCmdWaitMem(&cmd, GNM_WAIT_REG_MEM_FUNC_EQUAL, gpuAddr, 1, 0xffffffff);
```

---

## See Also

- [Command Buffers Guide](command-buffers.md) — detailed command buffer usage
- [Render Targets Guide](render-targets.md) — RT and depth RT setup
- [Shaders Guide](shaders.md) — shader binary and stage register setup
- [Driver Runtime Reference](../reference/driver.md) — all `sceGnm*` functions
- [Helpers Reference](../reference/helpers.md) — VideoOut and memory helpers
