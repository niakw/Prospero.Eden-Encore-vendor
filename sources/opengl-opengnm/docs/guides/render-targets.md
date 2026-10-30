# Render Targets

This guide covers color render targets and depth render targets — their
creation, sizing, alignment, and binding to the pipeline.

---

## Color Render Targets

### GnmRenderTarget

`GnmRenderTarget` is a 0x40-byte (64 bytes) descriptor that the GPU reads
directly. It encodes the color buffer address, format, dimensions, tiling,
MSAA, compression (DCC/Fmask/Cmask), and clear values.

### Creating a Render Target

#### Using the helper API

```c
#include <gnm_helpers.h>

GnmRenderTargetCreateInfo info;
sceGnmRtInitColorTargetCreateInfo(
    &info,
    GNM_FMT_R8G8B8A8_SRGB,    // format
    1920, 1080,               // width, height
    1,                        // num slices
    1, 1,                     // num samples, num fragments
    GNM_TM_DISPLAY_2D_THIN,   // tile mode
    GNM_GPU_BASE              // min GPU mode
);

uint64_t rtSize;
uint32_t rtAlign;
GnmRenderTarget rt;
sceGnmRtCreateColorTarget(
    &rt, baseAddr,
    GNM_FMT_R8G8B8A8_SRGB, 1920, 1080, 1, 1, 1,
    GNM_TM_DISPLAY_2D_THIN, GNM_GPU_BASE,
    &rtSize, &rtAlign
);
```

#### Using the core API

```c
#include <gnm_rendertarget.h>

GnmRenderTargetCreateInfo createInfo = {
    .colorfmt = GNM_FMT_R8G8B8A8_SRGB,
    .width = 1920,
    .height = 1080,
    .pitch = 1920,
    .numslices = 1,
    .numsamples = 1,
    .numfragments = 1,
    .colortilemodehint = GNM_TM_DISPLAY_2D_THIN,
    .mingpumode = GNM_GPU_BASE,
};

GnmRenderTarget rt;
GnmError err = sceGnmCreateRenderTarget(&rt, &createInfo);
```

### Render Target Accessors

```c
void* baseAddr = sceGnmRtGetBaseAddr(&rt);       // << 8
uint32_t pitch = sceGnmRtGetPitch(&rt);          // (tilemax+1)*8
uint32_t sliceSize = sceGnmRtGetSliceSize(&rt);
uint32_t numSlices = sceGnmRtGetNumSlices(&rt);
uint8_t numSamples = sceGnmRtGetNumSamples(&rt);
uint8_t numFrags = sceGnmRtGetNumFragments(&rt);
GnmDataFormat fmt = sceGnmRtGetFormat(&rt);
```

### Setting the Base Address

```c
sceGnmRtSetBaseAddr(&rt, gpuMemoryPtr);
```

The base address is stored as `address >> 8` internally (256-byte alignment).

### Calculating Size

```c
uint64_t size;
uint32_t alignment;
sceGnmRtCalcByteSize(&size, &alignment, &rt);
```

### Building GpaTextureInfo

```c
GpaTextureInfo info = sceGnmRtBuildInfo(&rt);
// Use with sceGpa* functions for surface computation
```

### Compression Features

The `GnmRenderTargetCreateInfoFlags` struct controls:

- `enable_cmask_fastclear` — Cmask-based fast clear
- `enable_fmask_compression` — Fmask compression for MSAA
- `enable_colortexture_without_decompress` — Allow texture reads without decompress
- `enable_fmasktexture_without_decompress` — Allow Fmask texture reads
- `enable_dcc_compression` — Delta Color Compression

---

## Depth Render Targets

### GnmDepthRenderTarget

`GnmDepthRenderTarget` is a 0x34-byte (52 bytes) descriptor encoding separate
read/write addresses for Z and stencil, HTile acceleration, and tiling.

### Creating a Depth Render Target

```c
#include <gnm_depthrendertarget.h>

GnmDepthRenderTargetCreateInfo createInfo = {
    .width = 1920,
    .height = 1080,
    .pitch = 1920,
    .numslices = 1,
    .zfmt = GNM_Z_32_FLOAT,
    .stencilfmt = GNM_STENCIL_8,
    .tilemodehint = GNM_TM_DEPTH_2D_THIN_128,
    .mingpumode = GNM_GPU_BASE,
    .numfragments = 1,
    .flags = { .enable_htile_acceleration = 1 },
};

GnmDepthRenderTarget drt;
GnmError err = sceGnmCreateDepthRenderTarget(&drt, &createInfo);
```

### Z and Stencil Addresses

The depth render target has **separate** read and write addresses for Z and
stencil, allowing different memory layouts for read vs. write paths:

```c
sceGnmDrtSetZReadAddress(&drt, zReadAddr);
sceGnmDrtSetZWriteAddress(&drt, zWriteAddr);
sceGnmDrtSetStencilReadAddress(&drt, sReadAddr);
sceGnmDrtSetStencilWriteAddress(&drt, sWriteAddr);

void* zRead = sceGnmDrtGetZReadAddress(&drt);
void* zWrite = sceGnmDrtGetZWriteAddress(&drt);
```

### HTile

HTile is a depth-stencil acceleration buffer:

```c
sceGnmDrtSetHtileAddress(&drt, htileAddr);
void* htileAddr = sceGnmDrtGetHtileAddress(&drt);
```

The HTile address must be 256-byte aligned.

### Sizing

```c
uint64_t size;
uint32_t alignment;
sceGnmDrtCalcByteSize(&size, &alignment, &drt);

uint64_t stencilOffset;
sceGnmDrtCalcStencilByteOffset(&stencilOffset, &drt);
```

### Dimensions and Padding

```c
uint16_t width = sceGnmDrtGetWidth(&drt);
uint16_t height = sceGnmDrtGetHeight(&drt);
uint16_t paddedWidth = sceGnmDrtGetPaddedWidth(&drt);
uint16_t paddedHeight = sceGnmDrtGetPaddedHeight(&drt);
uint16_t sliceSize = sceGnmDrtGetSliceSize(&drt);
uint16_t numSlices = sceGnmDrtGetNumSlices(&drt);
```

### MSAA

```c
uint8_t numFrags = sceGnmDrtGetNumFragments(&drt);
sceGnmDrtSetNumFragments(&drt, 4);  // 4x MSAA
```

### GPU Mode

```c
GnmGpuMode mode = sceGnmDrtGetMinGpuMode(&drt);
// GNM_GPU_BASE or GNM_GPU_NEO
```

---

## Binding Render Targets to the Pipeline

```c
// Color render target
sceGnmDrawCmdSetRenderTarget(&cmd, 0, &rt);  // slot 0
sceGnmDrawCmdSetRenderTargetMask(&cmd, 0x1); // enable slot 0

// Depth render target
sceGnmDrawCmdSetDepthRenderTarget(&cmd, &drt);

// Depth clear value
sceGnmDrawCmdSetDepthClearValue(&cmd, 1.0f);
```

Up to `GNM_MAX_RENDERTARGETS` (8) color render targets can be bound
simultaneously.

---

## Render Target Formats

Common color render target formats:

| Format | Bits/Pixel | Use Case |
|--------|-----------|----------|
| `GNM_FMT_R8G8B8A8_SRGB` | 32 | Display, sRGB |
| `GNM_FMT_R8G8B8A8_UNORM` | 32 | Display, linear |
| `GNM_FMT_R16G16B16A16_FLOAT` | 64 | HDR rendering |
| `GNM_FMT_R32G32B32A32_FLOAT` | 128 | High precision |

Common depth formats:

| Format | Description |
|--------|-------------|
| `GNM_Z_16` | 16-bit depth |
| `GNM_Z_24` | 24-bit depth |
| `GNM_Z_32_FLOAT` | 32-bit float depth |

---

## See Also

- [Render Target Reference](../reference/rendertarget.md)
- [Depth Render Target Reference](../reference/depthrendertarget.md)
- [Rendering Pipeline Guide](rendering-pipeline.md)
- [Surface Computation Guide](surface-computation.md)
