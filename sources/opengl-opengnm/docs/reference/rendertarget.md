# Render Target

API reference for color render targets in the opengnm PS4 GNM library. Render targets describe the surface configuration for color output, including tiling, compression (DCC/CMask/FMask), and addressing.

Header: `gnm_rendertarget.h`

---

## GnmRenderTargetCreateInfoFlags

Bitfield flags controlling optional features when creating a render target.

```c
typedef struct {
    uint32_t enable_cmask_fastclear : 1;
    uint32_t enable_fmask_compression : 1;
    uint32_t enable_colortexture_without_decompress : 1;
    uint32_t enable_fmasktexture_without_decompress : 1;
    uint32_t enable_dcc_compression : 1;
    uint32_t _unused : 27;
} GnmRenderTargetCreateInfoFlags;
```

| Field | Bits | Description |
|-------|------|-------------|
| `enable_cmask_fastclear` | 1 | Enable CMask fast-clear optimization |
| `enable_fmask_compression` | 1 | Enable FMask compression for MSAA surfaces |
| `enable_colortexture_without_decompress` | 1 | Allow sampling the color texture without triggering decompression |
| `enable_fmasktexture_without_decompress` | 1 | Allow sampling the FMask texture without triggering decompression |
| `enable_dcc_compression` | 1 | Enable Delta Color Compression (DCC) |
| `_unused` | 27 | Reserved |

---

## GnmRenderTargetCreateInfo

Parameters used by `sceGnmCreateRenderTarget` to initialize a `GnmRenderTarget`.

```c
typedef struct {
    GnmDataFormat colorfmt;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t numslices;

    uint32_t numsamples;
    uint32_t numfragments;

    GnmTileMode colortilemodehint;
    GnmGpuMode mingpumode;
    GnmRenderTargetCreateInfoFlags flags;
} GnmRenderTargetCreateInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `colorfmt` | `GnmDataFormat` | Color surface data format |
| `width` | `uint32_t` | Surface width in pixels |
| `height` | `uint32_t` | Surface height in pixels |
| `pitch` | `uint32_t` | Surface pitch in pixels |
| `numslices` | `uint32_t` | Number of array slices |
| `numsamples` | `uint32_t` | Number of MSAA samples |
| `numfragments` | `uint32_t` | Number of MSAA fragments |
| `colortilemodehint` | `GnmTileMode` | Tile mode hint for the color surface |
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode (BASE or NEO) |
| `flags` | `GnmRenderTargetCreateInfoFlags` | Feature enable flags |

---

## GnmRenderTarget

The render target descriptor (0x40 bytes). Contains packed register-compatible fields for the GPU's color render target state.

```c
_Static_assert(sizeof(GnmRenderTarget) == 0x40, "");
```

### Top-level Fields

| Field | Type | Description |
|-------|------|-------------|
| `base_base256b` | `uint32_t` | Base address of the color surface, in 256-byte blocks |
| `pitch` | union | Pitch configuration |
| `slice` | union | Slice size configuration |
| `view` | union | View (slice range) configuration |
| `info` | union | Format and feature info |
| `attrib` | union | Tiling and sampling attributes |
| `dcc_control` | union | DCC compression control |
| `cmask_base256b` | `uint32_t` | CMask base address in 256-byte blocks |
| `cmask_slice` | union | CMask slice configuration |
| `fmask_base256b` | `uint32_t` | FMask base address in 256-byte blocks |
| `fmask_slice` | union | FMask slice configuration |
| `clear_word0` | `uint32_t` | Fast-clear word 0 |
| `clear_word1` | `uint32_t` | Fast-clear word 1 |
| `dccbase_base256b` | `uint32_t` | DCC base address in 256-byte blocks |
| `_unusedreg` | `uint32_t` | Reserved register |
| `size` | union | Packed width/height |

### `pitch` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `tilemax` | 11 | Pitch in tiles minus 1 |
| `_unused` | 9 | Reserved |
| `fmask_tilemax` | 11 | FMask pitch in tiles minus 1 |
| `_unused2` | 1 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `slice` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `tilemax` | 22 | Slice size in tiles minus 1 |
| `_unused` | 10 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `view` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `slicestart` | 11 | Starting slice index |
| `_unused` | 2 | Reserved |
| `slicemax` | 11 | Maximum slice index |
| `_unused2` | 8 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `info` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `_unused` | 2 | Reserved |
| `format` | 5 | Surface format enum |
| `_unused2` | 1 | Reserved |
| `channeltype` | 3 | `GnmSurfaceNumber` — channel number type |
| `channelorder` | 2 | `GnmSurfaceSwap` — channel swap order |
| `fast_clear` | 1 | Fast-clear enabled |
| `compression` | 1 | Compression enabled |
| `is_normalized` | 1 | Surface is normalized |
| `is_int` | 1 | Surface is integer |
| `simple_float` | 1 | Surface uses simple float |
| `is_scaled` | 1 | Surface is scaled |
| `cmask_is_linear` | 1 | CMask is linear |
| `_unused4` | 6 | Reserved |
| `fmask_compression_mode` | 2 | FMask compression mode |
| `dcc_enable` | 1 | DCC enabled |
| `cmask_addr_type` | 2 | CMask address type |
| `alt_tile_mode` | 1 | Alternate tile mode (NEO) |
| `asuint` | 32 | Raw uint32 view |

### `attrib` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `tilemode_index` | 5 | `GnmTileMode` — color tile mode index |
| `fmask_tilemode_index` | 5 | `GnmTileMode` — FMask tile mode index |
| `_unused` | 2 | Reserved |
| `num_samples` | 3 | Log2 of sample count |
| `num_fragments` | 2 | Log2 of fragment count |
| `force_dst_alpha_1` | 1 | Force destination alpha to 1 |
| `_unused2` | 14 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `dcc_control` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `overwrite_combine_disabler` | 1 | Disable overwrite combine |
| `_unused` | 1 | Reserved |
| `max_uncompressed_blocksize` | 2 | Maximum uncompressed block size |
| `min_compressed_blocksize` | 1 | Minimum compressed block size |
| `max_compressed_blocksize` | 2 | Maximum compressed block size |
| `color_transform` | 2 | Color transform mode |
| `independent_64b_blocks` | 1 | Use independent 64-byte blocks |
| `_unused5` | 22 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `cmask_slice` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `tilemax` | 14 | CMask slice size in tiles minus 1 |
| `_unused` | 18 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `fmask_slice` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `tilemax` | 22 | FMask slice size in tiles minus 1 |
| `_unused` | 10 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `size` Union

| Sub-field | Type | Description |
|-----------|------|-------------|
| `width` | `uint16_t` | Surface width |
| `height` | `uint16_t` | Surface height |
| `asuint` | `uint32_t` | Raw uint32 view |

---

## Functions

### sceGnmCreateRenderTarget

Initializes a `GnmRenderTarget` from the given create info.

```c
GnmError sceGnmCreateRenderTarget(
    GnmRenderTarget* rt, const GnmRenderTargetCreateInfo* createinfo
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `rt` | `GnmRenderTarget*` | Output render target |
| `createinfo` | `const GnmRenderTargetCreateInfo*` | Creation parameters |
| **Returns** | `GnmError` | `GNM_ERROR_OK` on success |

### sceGnmRtGetFormat

Returns the color data format of the render target.

```c
GnmDataFormat sceGnmRtGetFormat(const GnmRenderTarget* rt);
```

### sceGnmRtGetBaseAddr

Returns the base address of the color surface.

```c
static inline void* sceGnmRtGetBaseAddr(const GnmRenderTarget* rt);
```

### sceGnmRtSetBaseAddr

Sets the base address of the color surface.

```c
static inline void sceGnmRtSetBaseAddr(GnmRenderTarget* rt, void* baseaddr);
```

### sceGnmRtGetPitch

Returns the pitch in pixels.

```c
static inline uint32_t sceGnmRtGetPitch(const GnmRenderTarget* rt);
```

### sceGnmRtGetSliceSize

Returns the slice size (height of a single slice in pixels).

```c
static inline uint32_t sceGnmRtGetSliceSize(const GnmRenderTarget* rt);
```

### sceGnmRtGetNumSlices

Returns the number of array slices.

```c
static inline uint32_t sceGnmRtGetNumSlices(const GnmRenderTarget* rt);
```

### sceGnmRtGetNumSamples

Returns the number of MSAA samples.

```c
static inline uint8_t sceGnmRtGetNumSamples(const GnmRenderTarget* rt);
```

### sceGnmRtGetNumFragments

Returns the number of MSAA fragments.

```c
static inline uint8_t sceGnmRtGetNumFragments(const GnmRenderTarget* rt);
```

### sceGnmRtBuildInfo

Builds a `GpaTextureInfo` from the render target for texture binding.

```c
static inline GpaTextureInfo sceGnmRtBuildInfo(const GnmRenderTarget* rt);
```

### sceGnmRtCalcByteSize

Calculates the total byte size and alignment of the render target surface.

```c
GnmError sceGnmRtCalcByteSize(
    uint64_t* outsize, uint32_t* outalign, const GnmRenderTarget* rt
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `outsize` | `uint64_t*` | Output total size in bytes |
| `outalign` | `uint32_t*` | Output alignment requirement |
| `rt` | `const GnmRenderTarget*` | Render target to measure |
| **Returns** | `GnmError` | `GNM_ERROR_OK` on success |

---

## Static Assertions

```c
_Static_assert(sizeof(GnmRenderTarget) == 0x40, "");
```

---

## See Also

- [Depth Render Target](depthrendertarget.md)
- [Controls](controls.md)
- [Shader](shader.md)
- [Shader Binary](shaderbinary.md)
