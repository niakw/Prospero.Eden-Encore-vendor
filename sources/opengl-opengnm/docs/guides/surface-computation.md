# Surface Computation

This guide covers the `sceGpa*` surface computation API — the opengnm
equivalent of AMD's AddrLib. It handles surface sizing, tiling parameter
computation, address swizzling, and texture decompression.

---

## Overview

The PS4 GPU uses tiled memory layouts for surfaces (textures, render targets,
depth buffers). The `sceGpa*` API computes:

- **Surface size and alignment** — how much memory a surface needs
- **Tiling parameters** — how texels map to memory addresses
- **Tile mode selection** — optimal tiling for a given format and usage
- **Surface coordinates** — physical offset of a given (x, y, z, sample)
- **Decompression** — convert BC-compressed textures to linear

---

## Surface Info

The primary function is `sceGpaComputeSurfaceInfo`, which computes the full
layout of a surface:

```c
#include <gpuaddr.h>

GpaTilingParams tp;
sceGpaTpInit(&tp, &texInfo, mipLevel, arraySlice);

GpaSurfaceInfo surfInfo;
GpaError err = sceGpaComputeSurfaceInfo(&surfInfo, &tp);
```

### GpaSurfaceInfo

| Field | Description |
|-------|-------------|
| `pitch` | Padded pitch in pixels |
| `height` | Padded height in pixels |
| `depth` | Padded depth |
| `surfacesize` | Total surface size in bytes |
| `basealign` | Base address alignment |
| `pitchalign` | Pitch alignment |
| `heightalign` | Height alignment |
| `depthalign` | Depth alignment |
| `bitsperelem` | Bits per element |
| `blockwidth` | Block width (for BC) |
| `blockheight` | Block height (for BC) |
| `tilemode` | Computed tile mode |
| `tileinfo` | Detailed tiling info (GpaTileInfo) |

---

## Tiling Parameters

### GpaTilingParams

```c
typedef struct {
    GnmTileMode tilemode;
    GnmGpuMode mingpumode;
    uint32_t linearwidth;
    uint32_t linearheight;
    uint32_t lineardepth;
    uint32_t numfragsperpixel;
    uint32_t basetiledpitch;
    uint32_t miplevel;
    uint32_t arrayslice;
    GpaSurfaceFlags surfaceflags;
    uint32_t bitsperfrag;
    bool isblockcompressed;
} GpaTilingParams;
```

### Initializing Tiling Params

```c
GpaTilingParams tp;
sceGpaTpInit(&tp, &texInfo, 0, 0);  // mip 0, slice 0
```

### GpaTextureInfo

`GpaTextureInfo` is the input to tiling parameter initialization. It can be
built from a `GnmTexture` or `GnmRenderTarget`:

```c
// From a texture
GpaTextureInfo info = sceGnmTexBuildInfo(&tex);

// From a render target
GpaTextureInfo info = sceGnmRtBuildInfo(&rt);
```

---

## Tile Mode Selection

Let the API choose an optimal tile mode:

```c
GnmTileMode tileMode;
GpaSurfaceFlags flags = { .texture = 1 };

sceGpaComputeSurfaceTileMode(
    &tileMode,
    GNM_GPU_BASE,               // min GPU mode
    GNM_ARRAY_2D_TILED_THIN1,  // array mode
    flags,                      // surface flags
    GNM_FMT_R8G8B8A8_SRGB,     // format
    1,                          // num fragments per pixel
    GNM_SURF_THIN_MICRO_TILING // micro tile mode
);
```

### Finding Optimal Surface

```c
GpaSurfaceProperties props;
sceGpaFindOptimalSurface(
    &props,
    GPA_SURFACE_COLOR,  // surface type
    32,                 // bits per pixel
    1,                  // num fragments
    false,              // mipmapped
    GNM_GPU_BASE        // min GPU mode
);
```

### Surface Types

| Enum | Description |
|------|-------------|
| `GPA_SURFACE_COLORDISPLAY` | Color display surface |
| `GPA_SURFACE_COLOR` | Color render target |
| `GPA_SURFACE_DEPTHSTENCIL` | Depth-stencil surface |
| `GPA_SURFACE_DEPTH` | Depth-only surface |
| `GPA_SURFACE_STENCIL` | Stencil-only surface |
| `GPA_SURFACE_FMASK` | Fmask surface |
| `GPA_SURFACE_TEXTUREFLAT` | 2D texture |
| `GPA_SURFACE_TEXTUREVOLUME` | 3D texture |
| `GPA_SURFACE_TEXTURECUBEMAP` | Cubemap |
| `GPA_SURFACE_RWTEXTUREFLAT` | RW 2D texture |
| `GPA_SURFACE_RWTEXTUREVOLUME` | RW 3D texture |
| `GPA_SURFACE_RWTEXTURECUBEMAP` | RW cubemap |

---

## Tile Info

Get detailed tiling parameters:

```c
GpaTileInfo tileInfo;
sceGpaGetTileInfo(
    &tileInfo,
    GNM_TM_THIN_2D_THIN,   // tile mode
    32,                      // bits per pixel
    1,                       // num fragments
    GNM_GPU_BASE             // GPU mode
);
```

### GpaTileInfo

| Field | Description |
|-------|-------------|
| `arraymode` | `GnmArrayMode` |
| `banks` | `GnmNumBanks` |
| `bankwidth` | `GnmBankWidth` |
| `bankheight` | `GnmBankHeight` |
| `macroaspectratio` | `GnmMacroTileAspect` |
| `tilesplit` | `GnmTileSplit` |
| `pipeconfig` | `GnmPipeConfig` |

---

## Base Swizzle

Compute the base swizzle for a surface:

```c
uint32_t swizzle;
sceGpaComputeBaseSwizzle(
    &swizzle,
    GNM_TM_THIN_2D_THIN,  // tile mode
    0,                      // surface index
    32,                     // bits per pixel
    1,                      // num fragments
    GNM_GPU_BASE            // GPU mode
);
```

---

## Surface Coordinates

Compute the physical offset of a specific texel:

```c
GpaSurfaceContext ctx;
sceGpaInitSurfaceContext(&ctx, surfSize, &tp);

uint64_t offset, bitOffset;
sceGpaComputeSurfaceCoord(
    &offset, &bitOffset, &ctx,
    x, y, z, fragIndex
);
```

### Size and Offset by Mip Level

```c
uint64_t mipSize, mipOffset;
sceGpaComputeSurfaceSizeOffset(
    &mipSize, &mipOffset, &texInfo,
    mipLevel, arraySlice
);
```

---

## HTile, Cmask, Fmask

### HTile (Depth Acceleration)

```c
GpaHtileParams htileParams = {
    .pitch = 1920,
    .height = 1080,
    .numslices = 1,
    .numfrags = 1,
    .bpp = 32,
    .arraymode = GNM_ARRAY_2D_TILED_THIN1,
    .banks = GNM_SURF_8_BANK,
    .pipeconfig = GNM_ADDR_SURF_P8_32x32_8x16,
    .mingpumode = GNM_GPU_BASE,
};

GpaHtileInfo htileInfo;
sceGpaComputeHtileInfo(&htileInfo, &htileParams);
```

### Cmask (Color Compression)

```c
GpaCmaskParams cmaskParams = { /* ... */ };
GpaCmaskInfo cmaskInfo;
sceGpaComputeCmaskInfo(&cmaskInfo, &cmaskParams);
```

### Fmask (MSAA Compression)

```c
GpaFmaskParams fmaskParams = { /* ... */ };
GpaFmaskInfo fmaskInfo;
sceGpaComputeFmaskInfo(&fmaskInfo, &fmaskParams);
```

---

## Tiling / Retiling

Convert between tiling modes:

```c
// Tile a linear texture to GPU tiled format
GpaTilingParams srcTp, dstTp;
sceGpaTpInit(&srcTp, &texInfo, 0, 0);
// Set dstTp for the target tiling mode...

sceGpaTileSurface(
    outBuf, outLen,
    inBuf, inLen,
    &srcTp, &dstTp
);
```

### Tile a Specific Mip/Slice

```c
sceGpaTileTextureIndexed(
    inBuf, inLen, outBuf, outLen,
    &texInfo,
    GNM_TM_THIN_2D_THIN,  // new tiling
    mipLevel, sliceIndex
);
```

### Tile All Mips/Slices

```c
sceGpaTileTextureAll(
    inBuf, inLen, outBuf, outLen,
    &texInfo,
    GNM_TM_THIN_2D_THIN
);
```

### Tile a Region

```c
GpaSurfaceRegion region = {
    .left = 0, .top = 0, .front = 0,
    .right = 256, .bottom = 256, .back = 1
};
sceGpaTileSurfaceRegion(
    outBuf, outLen, inBuf, inLen,
    &srcTp, &dstTp, &region
);
```

---

## Decompression

Decompress BC-compressed textures to linear format:

```c
uint64_t decompressedSize;
sceGpaGetDecompressedSize(&decompressedSize, &texInfo);

void* outBuf = malloc(decompressedSize);
GnmDataFormat outFmt;
GpaError err = sceGpaDecompressTexture(
    outBuf, decompressedSize,
    inBuf, inSize,
    &texInfo, &outFmt
);
```

This uses the bundled `bcdec` library for BC1-BC7 decompression.

---

## Error Handling

All `sceGpa*` functions return `GpaError`:

| Error | Value | Description |
|-------|-------|-------------|
| `GPA_ERR_OK` | 0 | Success |
| `GPA_ERR_INVALID_ARGS` | 1 | Invalid arguments |
| `GPA_ERR_OVERFLOW` | 2 | Buffer overflow |
| `GPA_ERR_TILING_ERROR` | 3 | Tiling operation failed |
| `GPA_ERR_UNSUPPORTED` | 4 | Unsupported format/mode |
| `GPA_ERR_INTERNAL_ERROR` | 5 | Internal error |
| `GPA_ERR_NOT_COMPRESSED` | 6 | Texture is not compressed |

```c
const char* msg = sceGpaStrError(err);
```

---

## See Also

- [Surface Computation Reference](../reference/gpuaddr.md)
- [Textures & Formats Guide](textures-and-formats.md)
- [Render Targets Guide](render-targets.md)
