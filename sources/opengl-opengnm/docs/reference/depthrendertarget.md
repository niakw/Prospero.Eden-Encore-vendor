# Depth Render Target

API reference for depth/stencil render targets in the opengnm PS4 GNM library. Depth render targets describe the surface configuration for depth and stencil output, including HTile acceleration and tiling.

Header: `gnm_depthrendertarget.h`

---

## GnmDepthRenderTargetCreateInfoFlags

Bitfield flags controlling optional features when creating a depth render target.

```c
typedef struct {
    uint32_t enable_htile_acceleration : 1;
    uint32_t enable_texture_without_decompress : 1;
    uint32_t _unused : 30;
} GnmDepthRenderTargetCreateInfoFlags;
```

| Field | Bits | Description |
|-------|------|-------------|
| `enable_htile_acceleration` | 1 | Enable HTile depth acceleration |
| `enable_texture_without_decompress` | 1 | Allow sampling the depth/stencil texture without triggering decompression |
| `_unused` | 30 | Reserved |

---

## GnmDepthRenderTargetCreateInfo

Parameters used by `sceGnmCreateDepthRenderTarget` to initialize a `GnmDepthRenderTarget`.

```c
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t numslices;

    GnmZFormat zfmt;
    GnmStencilFormat stencilfmt;
    GnmTileMode tilemodehint;
    GnmGpuMode mingpumode;
    uint32_t numfragments;
    GnmDepthRenderTargetCreateInfoFlags flags;
} GnmDepthRenderTargetCreateInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `width` | `uint32_t` | Surface width in pixels |
| `height` | `uint32_t` | Surface height in pixels |
| `pitch` | `uint32_t` | Surface pitch in pixels |
| `numslices` | `uint32_t` | Number of array slices |
| `zfmt` | `GnmZFormat` | Depth buffer format |
| `stencilfmt` | `GnmStencilFormat` | Stencil buffer format |
| `tilemodehint` | `GnmTileMode` | Tile mode hint |
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode (BASE or NEO) |
| `numfragments` | `uint32_t` | Number of MSAA fragments |
| `flags` | `GnmDepthRenderTargetCreateInfoFlags` | Feature enable flags |

---

## GnmDepthRenderTarget

The depth render target descriptor (0x34 bytes). Contains packed register-compatible fields for the GPU's depth/stencil render target state.

```c
_Static_assert(sizeof(GnmDepthRenderTarget) == 0x34, "");
```

### Top-level Fields

| Field | Type | Description |
|-------|------|-------------|
| `zinfo` | union | Z (depth) info configuration |
| `stencilinfo` | union | Stencil info configuration |
| `zreadbase256b` | `uint32_t` | Z read base address in 256-byte blocks |
| `stencilreadbase256b` | `uint32_t` | Stencil read base address in 256-byte blocks |
| `zwritebase256b` | `uint32_t` | Z write base address in 256-byte blocks |
| `stencilwritebase256b` | `uint32_t` | Stencil write base address in 256-byte blocks |
| `depthsize` | union | Depth surface pitch/height |
| `depthslice` | union | Depth slice size |
| `depthview` | union | Depth view (slice range) |
| `htiledatabase256b` | `uint32_t` | HTile base address in 256-byte blocks |
| `htilesurface` | union | HTile surface flags |
| `depthinfo` | union | Depth tiling configuration |
| `size` | union | Packed width/height |

### `zinfo` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `format` | 2 | `GnmZFormat` — depth format |
| `numsamples` | 2 | Log2 of sample/fragment count |
| `_unused` | 9 | Reserved |
| `tilesplit` | 3 | Tile split value |
| `_unused2` | 4 | Reserved |
| `tilemodeindex` | 3 | `GnmTileMode` — tile mode index |
| `_unused3` | 4 | Reserved |
| `allowexpclear` | 1 | Allow explicit clear |
| `_unused4` | 1 | Reserved |
| `tilesurfaceenable` | 1 | Tile surface enable (HTile) |
| `_unused5` | 1 | Reserved |
| `zrangeprecision` | 1 | Z range precision |
| `asuint` | 32 | Raw uint32 view |

### `stencilinfo` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `format` | 1 | `GnmStencilFormat` — stencil format |
| `_unused` | 12 | Reserved |
| `tilesplit` | 3 | Tile split value |
| `_unused2` | 4 | Reserved |
| `tilemodeindex` | 3 | `GnmTileMode` — tile mode index |
| `_unused3` | 4 | Reserved |
| `allowexpclear` | 1 | Allow explicit clear |
| `_unused4` | 1 | Reserved |
| `tilestencildisable` | 1 | Disable stencil tiling |
| `_unused5` | 2 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `depthsize` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `pitchtilemax` | 11 | Pitch in tiles minus 1 |
| `heighttilemax` | 11 | Height in tiles minus 1 |
| `_unused` | 10 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `depthslice` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `slicetile` | 22 | Slice size in tiles minus 1 |
| `_unused` | 10 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `depthview` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `slicestart` | 11 | Starting slice index |
| `_unused` | 2 | Reserved |
| `slicemax` | 11 | Maximum slice index |
| `_unused2` | 8 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `htilesurface` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `linear` | 1 | HTile surface is linear |
| `_unused` | 16 | Reserved |
| `tccompatible` | 1 | TC compatible |
| `_unused2` | 14 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `depthinfo` Union

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `_unused` | 4 | Reserved |
| `arraymode` | 4 | `GnmArrayMode` — array mode |
| `pipeconfig` | 5 | `GnmPipeConfig` — pipe configuration |
| `bankwidth` | 2 | `GnmBankWidth` — bank width |
| `bankheight` | 2 | `GnmBankHeight` — bank height |
| `macrotileaspect` | 2 | `GnmMacroTileAspect` — macro tile aspect ratio |
| `numbanks` | 2 | `GnmNumBanks` — number of banks |
| `_unused2` | 11 | Reserved |
| `asuint` | 32 | Raw uint32 view |

### `size` Union

| Sub-field | Type | Description |
|-----------|------|-------------|
| `width` | `uint16_t` | Surface width |
| `height` | `uint16_t` | Surface height |
| `asuint` | `uint32_t` | Raw uint32 view |

---

## Functions

### sceGnmCreateDepthRenderTarget

Initializes a `GnmDepthRenderTarget` from the given create info.

```c
GnmError sceGnmCreateDepthRenderTarget(
    GnmDepthRenderTarget* drt, const GnmDepthRenderTargetCreateInfo* createinfo
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `drt` | `GnmDepthRenderTarget*` | Output depth render target |
| `createinfo` | `const GnmDepthRenderTargetCreateInfo*` | Creation parameters |
| **Returns** | `GnmError` | `GNM_ERROR_OK` on success |

### sceGnmDrtCalcByteSize

Calculates the total byte size and alignment of the depth render target surface.

```c
GnmError sceGnmDrtCalcByteSize(
    uint64_t* outsize, uint32_t* outalignment, const GnmDepthRenderTarget* drt
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `outsize` | `uint64_t*` | Output total size in bytes |
| `outalignment` | `uint32_t*` | Output alignment requirement |
| `drt` | `const GnmDepthRenderTarget*` | Depth render target to measure |
| **Returns** | `GnmError` | `GNM_ERROR_OK` on success |

### sceGnmDrtCalcStencilByteOffset

Calculates the byte offset of the stencil buffer within the depth render target.

```c
GnmError sceGnmDrtCalcStencilByteOffset(
    uint64_t* outoffset, const GnmDepthRenderTarget* drt
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `outoffset` | `uint64_t*` | Output stencil byte offset |
| `drt` | `const GnmDepthRenderTarget*` | Depth render target |
| **Returns** | `GnmError` | `GNM_ERROR_OK` on success |

### sceGnmDrtGetNumFragments

Returns the number of MSAA fragments.

```c
static inline uint8_t sceGnmDrtGetNumFragments(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetNumFragments

Sets the number of MSAA fragments.

```c
static inline void sceGnmDrtSetNumFragments(
    GnmDepthRenderTarget* drt, uint32_t numfrags
);
```

### sceGnmDrtSetTileMode

Sets the tile mode for the depth render target.

```c
GnmError sceGnmDrtSetTileMode(GnmDepthRenderTarget* drt, GnmTileMode mode);
```

### sceGnmDrtGetZReadAddress

Returns the Z read base address.

```c
void* sceGnmDrtGetZReadAddress(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetZReadAddress

Sets the Z read base address.

```c
GnmError sceGnmDrtSetZReadAddress(GnmDepthRenderTarget* drt, void* baseaddr);
```

### sceGnmDrtGetStencilReadAddress

Returns the stencil read base address.

```c
void* sceGnmDrtGetStencilReadAddress(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetStencilReadAddress

Sets the stencil read base address.

```c
GnmError sceGnmDrtSetStencilReadAddress(GnmDepthRenderTarget* drt, void* baseaddr);
```

### sceGnmDrtGetZWriteAddress

Returns the Z write base address.

```c
void* sceGnmDrtGetZWriteAddress(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetZWriteAddress

Sets the Z write base address.

```c
GnmError sceGnmDrtSetZWriteAddress(GnmDepthRenderTarget* drt, void* baseaddr);
```

### sceGnmDrtGetStencilWriteAddress

Returns the stencil write base address.

```c
void* sceGnmDrtGetStencilWriteAddress(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetStencilWriteAddress

Sets the stencil write base address.

```c
GnmError sceGnmDrtSetStencilWriteAddress(
    GnmDepthRenderTarget* drt, void* baseaddr
);
```

### sceGnmDrtGetSliceSize

Returns the slice size in bytes.

```c
static inline uint16_t sceGnmDrtGetSliceSize(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetSliceSize

Sets the slice size from pitch and height.

```c
static inline void sceGnmDrtSetSliceSize(
    GnmDepthRenderTarget* drt, uint16_t pitch, uint16_t height
);
```

### sceGnmDrtGetPaddedWidth

Returns the padded width in pixels.

```c
static inline uint16_t sceGnmDrtGetPaddedWidth(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetPaddedWidth

Sets the padded width in pixels. Returns `GNM_ERROR_INVALID_ARGS` if width exceeds maximum.

```c
static inline GnmError sceGnmDrtSetPaddedWidth(
    GnmDepthRenderTarget* drt, uint16_t width
);
```

### sceGnmDrtGetPaddedHeight

Returns the padded height in pixels.

```c
static inline uint16_t sceGnmDrtGetPaddedHeight(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetPaddedHeight

Sets the padded height in pixels. Returns `GNM_ERROR_INVALID_ARGS` if height exceeds maximum.

```c
static inline GnmError sceGnmDrtSetPaddedHeight(
    GnmDepthRenderTarget* drt, uint16_t height
);
```

### sceGnmDrtGetNumSlices

Returns the number of array slices.

```c
static inline uint16_t sceGnmDrtGetNumSlices(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtGetHtileAddress

Returns the HTile base address.

```c
static inline void* sceGnmDrtGetHtileAddress(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetHtileAddress

Sets the HTile base address. Returns `GNM_ERROR_INVALID_ARGS` if `drt` is null, or `GNM_ERROR_INVALID_ALIGNMENT` if address is not 256-byte aligned.

```c
static inline GnmError sceGnmDrtSetHtileAddress(
    GnmDepthRenderTarget* drt, void* baseaddr
);
```

### sceGnmDrtGetMinGpuMode

Returns the minimum GPU mode based on the pipe configuration.

```c
static inline GnmGpuMode sceGnmDrtGetMinGpuMode(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtGetWidth

Returns the surface width.

```c
static inline uint16_t sceGnmDrtGetWidth(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetWidth

Sets the surface width.

```c
static inline void sceGnmDrtSetWidth(GnmDepthRenderTarget* drt, uint16_t width);
```

### sceGnmDrtGetHeight

Returns the surface height.

```c
static inline uint16_t sceGnmDrtGetHeight(const GnmDepthRenderTarget* drt);
```

### sceGnmDrtSetHeight

Sets the surface height.

```c
static inline void sceGnmDrtSetHeight(GnmDepthRenderTarget* drt, uint16_t height);
```

---

## Static Assertions

```c
_Static_assert(sizeof(GnmDepthRenderTarget) == 0x34, "");
```

---

## See Also

- [Render Target](rendertarget.md)
- [Controls](controls.md)
- [Shader](shader.md)
- [Shader Binary](shaderbinary.md)
