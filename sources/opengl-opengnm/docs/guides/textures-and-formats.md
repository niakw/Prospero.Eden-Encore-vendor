# Textures & Formats

This guide covers data formats, texture descriptor creation, tiling modes, and
block-compressed textures in opengnm.

---

## GnmDataFormat

`GnmDataFormat` is the core format descriptor used throughout opengnm. It is a
4-byte union that packs a surface format, channel type, and channel mapping:

```c
typedef union {
    struct {
        GnmImageFormat surfacefmt : 8;   // e.g. GNM_IMG_DATA_FORMAT_8_8_8_8
        GnmImgNumFormat chantype : 4;   // e.g. GNM_IMG_NUM_FORMAT_UNORM
        GnmChannel chanx : 3;
        GnmChannel chany : 3;
        GnmChannel chanz : 3;
        GnmChannel chanw : 3;
        uint32_t _unused : 8;
    };
    uint32_t asuint;
} GnmDataFormat;
```

### Predefined Format Constants

opengnm provides 30+ predefined format constants. Common ones:

| Constant | Format |
|----------|--------|
| `GNM_FMT_R8_UNORM` | 8-bit red, unorm |
| `GNM_FMT_A8_UNORM` | 8-bit alpha, unorm |
| `GNM_FMT_R8G8B8A8_UNORM` | RGBA 8-bit, unorm |
| `GNM_FMT_R8G8B8A8_SRGB` | RGBA 8-bit, sRGB |
| `GNM_FMT_B8G8R8A8_UNORM` | BGRA 8-bit, unorm |
| `GNM_FMT_B8G8R8A8_SRGB` | BGRA 8-bit, sRGB |
| `GNM_FMT_R16G16_FLOAT` | RG 16-bit float |
| `GNM_FMT_R16G16B16A16_FLOAT` | RGBA 16-bit float |
| `GNM_FMT_R32_FLOAT` | 32-bit red float |
| `GNM_FMT_R32G32_FLOAT` | RG 32-bit float |
| `GNM_FMT_R32G32B32A32_FLOAT` | RGBA 32-bit float |
| `GNM_FMT_BC1_UNORM` | BC1 (DXT1), unorm |
| `GNM_FMT_BC3_UNORM` | BC3 (DXT5), unorm |
| `GNM_FMT_BC7_UNORM` | BC7, unorm |
| `GNM_FMT_BC7_SRGB` | BC7, sRGB |

See the [Data Formats Reference](../reference/dataformat.md) for the complete list.

### Creating a Custom Format

```c
GnmDataFormat fmt = GNM_DATA_FORMAT_INIT(
    GNM_IMG_DATA_FORMAT_8_8_8_8,  // surface format
    GNM_IMG_NUM_FORMAT_UNORM,     // channel type
    GNM_CHAN_X, GNM_CHAN_Y,       // channel mapping
    GNM_CHAN_Z, GNM_CHAN_W
);
```

### Format Queries

```c
uint32_t bits = sceGnmDfGetBitsPerElement(fmt);
uint32_t bytes = sceGnmDfGetBytesPerElement(fmt);
uint32_t components = sceGnmDfGetNumComponents(fmt);
bool isBC = sceGnmDfIsBlockCompressed(fmt);
uint32_t texelsPerElem = sceGnmDfGetTexelsPerElement(fmt);
```

### Depth/Stencil Formats

```c
GnmDataFormat zFmt = sceGnmDfInitFromZ(GNM_Z_32_FLOAT);
GnmDataFormat sFmt = sceGnmDfInitFromStencil(GNM_STENCIL_8, GNM_IMG_NUM_FORMAT_UINT);
```

---

## GnmTexture

`GnmTexture` is the 0x20-byte (32 bytes) texture descriptor that the GPU reads
directly. It encodes the base address, format, dimensions, tiling, mip levels,
array slices, and MSAA parameters.

### Creating a Texture

```c
#include <gnm_texture.h>

GnmTextureCreateInfo createInfo = {
    .format = GNM_FMT_R8G8B8A8_SRGB,
    .texturetype = GNM_TEXTURE_2D,
    .width = 1920,
    .height = 1080,
    .depth = 1,
    .pitch = 1920,
    .nummiplevels = 1,
    .numslices = 1,
    .numfragments = 1,
    .tilemodehint = GNM_TM_THIN_2D_THIN,
    .mingpumode = GNM_GPU_BASE
};

GnmTexture tex;
GnmError err = sceGnmCreateTexture(&tex, &createInfo);
```

### Using the Helper API

```c
#include <gnm_helpers.h>

GnmTextureCreateInfo info;
sceGnmTexInit2dCreateInfo(
    &info,
    GNM_FMT_R8G8B8A8_SRGB,
    1920, 1080,
    1,                      // mip count
    GNM_TM_THIN_2D_THIN,   // tile mode
    GNM_GPU_BASE
);

uint64_t texSize;
uint32_t texAlign;
GnmTexture tex;
sceGnmTexCreate2d(
    &tex, baseAddr,
    GNM_FMT_R8G8B8A8_SRGB, 1920, 1080, 1,
    GNM_TM_THIN_2D_THIN, GNM_GPU_BASE,
    &texSize, &texAlign
);
```

### Texture Accessors

```c
void* baseAddr = sceGnmTexGetBaseAddress(&tex);  // << 8
uint32_t width = sceGnmTexGetWidth(&tex);         // stored as width-1
uint32_t height = sceGnmTexGetHeight(&tex);
uint32_t depth = sceGnmTexGetDepth(&tex);
uint32_t pitch = sceGnmTexGetPitch(&tex);
uint32_t numMips = sceGnmTexGetNumMips(&tex);
uint32_t numSlices = sceGnmTexGetNumArraySlices(&tex);
uint8_t numFrags = sceGnmTexGetNumFragments(&tex);
GnmDataFormat fmt = sceGnmTexGetFormat(&tex);
```

### Setting the Base Address

```c
sceGnmTexSetBaseAddress(&tex, gpuMemoryPtr);
```

The base address is stored as `address >> 8` internally, so it must be
256-byte aligned.

### Calculating Texture Size

```c
uint64_t size;
uint32_t alignment;
sceGnmTexCalcByteSize(&size, &alignment, &tex);
```

### Building GpaTextureInfo

To use the texture with the surface computation API:

```c
GpaTextureInfo info = sceGnmTexBuildInfo(&tex);
```

---

## Tiling Modes

Tiling modes control how texels are arranged in memory. The PS4 GPU uses a
tiled memory layout for optimal cache utilization.

### Common Tiling Modes

| Mode | Use Case |
|------|----------|
| `GNM_TM_DISPLAY_LINEAR_ALIGNED` | Display surfaces, linear layout |
| `GNM_TM_DISPLAY_2D_THIN` | Display surfaces, 2D tiled |
| `GNM_TM_THIN_2D_THIN` | General 2D textures |
| `GNM_TM_DEPTH_2D_THIN_128` | Depth targets, 128B tile split |
| `GNM_TM_DEPTH_2D_THIN_256` | Depth targets, 256B tile split |
| `GNM_TM_THICK_2D_THICK` | Thick 2D textures |
| `GNM_TM_DISPLAY_LINEAR_GENERAL` | Linear, no alignment |

### Choosing a Tiling Mode

Use `sceGpaComputeSurfaceTileMode` to let the surface computation library
choose an optimal tiling mode:

```c
GnmTileMode tileMode;
sceGpaComputeSurfaceTileMode(
    &tileMode,
    GNM_GPU_BASE,
    GNM_ARRAY_2D_TILED_THIN1,  // array mode
    flags,                      // surface flags
    GNM_FMT_R8G8B8A8_SRGB,     // format
    1,                          // num fragments per pixel
    GNM_SURF_THIN_MICRO_TILING // micro tile mode
);
```

---

## Block-Compressed Textures

opengnm supports BC1-BC7 block-compressed texture formats. These formats
compress 4x4 pixel blocks into 8-16 bytes.

| Format | Block Size | Compression |
|--------|-----------|-------------|
| BC1 | 8 bytes / 4x4 | 1-bit alpha, 4:1 compression |
| BC2 | 16 bytes / 4x4 | Explicit 4-bit alpha |
| BC3 | 16 bytes / 4x4 | Interpolated alpha |
| BC4 | 8 bytes / 4x4 | Single channel (red) |
| BC5 | 16 bytes / 4x4 | Two channels (red, green) |
| BC6 | 16 bytes / 4x4 | HDR RGB |
| BC7 | 16 bytes / 4x4 | High quality RGBA |

### Checking for Block Compression

```c
if (sceGnmDfIsBlockCompressed(fmt)) {
    uint32_t texelsPerElem = sceGnmDfGetTexelsPerElement(fmt);  // 16 for BC
    uint32_t wide = sceGnmDfGetTexelsPerElementWide(fmt);       // 4 for BC
    uint32_t tall = sceGnmDfGetTexelsPerElementTall(fmt);       // 4 for BC
}
```

### Decompressing Textures

Use the GpuAddr API to decompress BC textures to linear format:

```c
uint64_t decompressedSize;
sceGpaGetDecompressedSize(&decompressedSize, &texInfo);

void* outBuf = malloc(decompressedSize);
GnmDataFormat outFmt;
sceGpaDecompressTexture(outBuf, decompressedSize,
    inBuf, inSize, &texInfo, &outFmt);
```

---

## Texture Types

| Enum | Description |
|------|-------------|
| `GNM_TEXTURE_1D` | 1D texture |
| `GNM_TEXTURE_2D` | 2D texture |
| `GNM_TEXTURE_3D` | 3D texture |
| `GNM_TEXTURE_CUBEMAP` | Cubemap (6 faces) |
| `GNM_TEXTURE_1D_ARRAY` | 1D texture array |
| `GNM_TEXTURE_2D_ARRAY` | 2D texture array |
| `GNM_TEXTURE_2D_MSAA` | 2D MSAA texture |
| `GNM_TEXTURE_2D_ARRAY_MSAA` | 2D MSAA texture array |

---

## Binding Textures to Shaders

In the draw command buffer:

```c
// Bind a texture (T#) to a shader stage's user data slot
sceGnmDrawCmdSetTsharpUserData(&cmd, GNM_STAGE_PS, 0, &tex);

// Bind a sampler (S#) to a shader stage's user data slot
sceGnmDrawCmdSetSsharpUserData(&cmd, GNM_STAGE_PS, 0, &sampler);
```

---

## See Also

- [Data Formats Reference](../reference/dataformat.md)
- [Texture Reference](../reference/texture.md)
- [Surface Computation Guide](surface-computation.md)
- [Surface Computation Reference](../reference/gpuaddr.md)
