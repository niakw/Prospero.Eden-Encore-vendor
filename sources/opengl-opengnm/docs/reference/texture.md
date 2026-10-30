# Texture

The `GnmTexture` struct describes a GPU texture resource (T# / T-sharp) — used for all texture sampling operations. This header (`gnm_texture.h`) defines the texture create info struct, the 8-register texture descriptor, and functions for creating, querying, and modifying texture resources.

---

## GnmTextureCreateInfo Struct

A helper struct used to initialize a `GnmTexture` via `sceGnmCreateTexture`. Contains the high-level parameters needed to configure a texture descriptor.

```c
typedef struct {
    GnmDataFormat format;
    GnmTextureType texturetype;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t pitch;

    uint32_t nummiplevels;
    uint32_t numslices;
    uint32_t numfragments;

    GnmTileMode tilemodehint;
    GnmGpuMode mingpumode;
} GnmTextureCreateInfo;
```

### Field Reference

| Field | Type | Description |
|---|---|---|
| `format` | `GnmDataFormat` | Texture data format (surface format + numeric type + channel mapping) |
| `texturetype` | `GnmTextureType` | Texture dimensionality (1D, 2D, 3D, cubemap, array, MSAA) |
| `width` | `uint32_t` | Texture width in texels |
| `height` | `uint32_t` | Texture height in texels |
| `depth` | `uint32_t` | Texture depth (for 3D textures) or number of array slices |
| `pitch` | `uint32_t` | Texture pitch (row stride) in texels |
| `nummiplevels` | `uint32_t` | Number of mip levels |
| `numslices` | `uint32_t` | Number of array slices |
| `numfragments` | `uint32_t` | Number of MSAA fragments (for MSAA textures) |
| `tilemodehint` | `GnmTileMode` | Hint for the desired tile mode |
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode (`GNM_GPU_BASE` or `GNM_GPU_NEO`) |

---

## GnmTexture Struct

A 32-byte (`0x20` bytes) texture descriptor consisting of 8 registers (7 packed bitfield registers + 1 metadata address register).

```c
typedef struct {
    uint32_t baseaddress;

    uint32_t baseaddresshi : 6;
    uint32_t mtype_l2 : 2;
    uint32_t minlod : 12;
    GnmImageFormat dataformat : 6;
    GnmImgNumFormat numformat : 4;
    uint32_t mtype0 : 2;

    uint32_t width : 14;
    uint32_t height : 14;
    uint32_t perfmod : 3;
    uint32_t interlaced : 1;

    GnmChannel dstselx : 3;
    GnmChannel dstsely : 3;
    GnmChannel dstselz : 3;
    GnmChannel dstselw : 3;
    uint32_t baselevel : 4;
    uint32_t lastlevel : 4;
    GnmTileMode tilingindex : 5;
    uint32_t pow2pad : 1;
    uint32_t mtype2 : 1;
    uint32_t atc : 1;
    GnmTextureType type : 4;

    uint32_t depth : 13;
    uint32_t pitch : 14;
    uint32_t _unused : 5;

    uint32_t basearray : 13;
    uint32_t lastarray : 13;
    uint32_t _unused2 : 6;

    uint32_t minlodwarn : 12;
    uint32_t counterbankid : 8;
    uint32_t lodhdwcnten : 1;
    uint32_t compressionen : 1;
    uint32_t alphaisonmsb : 1;
    uint32_t colortransform : 1;
    uint32_t alttilemode : 1;
    uint32_t _unused3 : 7;

    uint32_t metadataaddr;
} GnmTexture;
```

_Static_assert: `sizeof(GnmTexture) == 0x20`

### Field Reference

| Register | Field | Bits | Type | Description |
|---|---|---|---|---|
| 0 | `baseaddress` | 32 | `uint32_t` | Low 32 bits of base address (shifted left 8 on read) |
| 1 | `baseaddresshi` | 6 | `uint32_t` | High 6 bits of base address |
| 1 | `mtype_l2` | 2 | `uint32_t` | L2 cache memory type |
| 1 | `minlod` | 12 | `uint32_t` | Minimum LOD clamp |
| 1 | `dataformat` | 6 | `GnmImageFormat` | Image data format (component bit-width layout) |
| 1 | `numformat` | 4 | `GnmImgNumFormat` | Numeric format (UNORM, FLOAT, SRGB, etc.) |
| 1 | `mtype0` | 2 | `uint32_t` | Memory type field 0 (L1 cache bypass) |
| 2 | `width` | 14 | `uint32_t` | Texture width minus 1 |
| 2 | `height` | 14 | `uint32_t` | Texture height minus 1 |
| 2 | `perfmod` | 3 | `uint32_t` | Performance modulation mode |
| 2 | `interlaced` | 1 | `uint32_t` | Interlaced content flag |
| 3 | `dstselx` | 3 | `GnmChannel` | Destination channel X selection |
| 3 | `dstsely` | 3 | `GnmChannel` | Destination channel Y selection |
| 3 | `dstselz` | 3 | `GnmChannel` | Destination channel Z selection |
| 3 | `dstselw` | 3 | `GnmChannel` | Destination channel W selection |
| 3 | `baselevel` | 4 | `uint32_t` | Base mip level |
| 3 | `lastlevel` | 4 | `uint32_t` | Last mip level (or log2 numfragments for MSAA) |
| 3 | `tilingindex` | 5 | `GnmTileMode` | Tile mode index |
| 3 | `pow2pad` | 1 | `uint32_t` | Power-of-2 padding flag |
| 3 | `mtype2` | 1 | `uint32_t` | Memory type field 2 (read-only flag) |
| 3 | `atc` | 1 | `uint32_t` | ATC (address translation cache) enable |
| 3 | `type` | 4 | `GnmTextureType` | Texture type (1D, 2D, 3D, cubemap, array, MSAA) |
| 4 | `depth` | 13 | `uint32_t` | Texture depth minus 1 (3D) |
| 4 | `pitch` | 14 | `uint32_t` | Pitch minus 1 (row stride in texels) |
| 4 | `_unused` | 5 | `uint32_t` | Reserved |
| 5 | `basearray` | 13 | `uint32_t` | First array slice index |
| 5 | `lastarray` | 13 | `uint32_t` | Last array slice index |
| 5 | `_unused2` | 6 | `uint32_t` | Reserved |
| 6 | `minlodwarn` | 12 | `uint32_t` | Minimum LOD warning threshold |
| 6 | `counterbankid` | 8 | `uint32_t` | Counter bank ID |
| 6 | `lodhdwcnten` | 1 | `uint32_t` | LOD hardware counter enable |
| 6 | `compressionen` | 1 | `uint32_t` | Compression (DCC) enable |
| 6 | `alphaisonmsb` | 1 | `uint32_t` | Alpha is on MSB |
| 6 | `colortransform` | 1 | `uint32_t` | DCC color transform enable |
| 6 | `alttilemode` | 1 | `uint32_t` | Alternate tile mode (NEO GPU) flag |
| 6 | `_unused3` | 7 | `uint32_t` | Reserved |
| 7 | `metadataaddr` | 32 | `uint32_t` | Metadata (DCC) buffer address |

---

## Functions

### sceGnmCreateTexture

Creates a `GnmTexture` descriptor from a `GnmTextureCreateInfo`. This is the primary texture creation function that computes the correct tile mode, dimensions, and format encoding.

```c
GnmError sceGnmCreateTexture(
    GnmTexture* tex, const GnmTextureCreateInfo* createinfo
);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `GnmTexture*` | Output: the initialized texture descriptor |
| `createinfo` | `const GnmTextureCreateInfo*` | Texture creation parameters |

**Returns:** `GNM_ERROR_OK` on success, or a `GnmError` code on failure.

---

### sceGnmTexGetBaseAddress

Returns the GPU virtual base address of the texture. The stored `baseaddress` is shifted left by 8 bits. *(static inline)*

```c
static inline void* sceGnmTexGetBaseAddress(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** The 64-bit base address as a `void*` (`tex->baseaddress << 8`).

---

### sceGnmTexSetBaseAddress

Sets the GPU virtual base address of the texture.

```c
void sceGnmTexSetBaseAddress(GnmTexture* tex, void* baseaddr);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `GnmTexture*` | Pointer to the texture descriptor to modify |
| `baseaddr` | `void*` | The 64-bit base address |

---

### sceGnmTexGetFormat

Retrieves the texture's data format as a `GnmDataFormat`. *(static inline)*

```c
static inline GnmDataFormat sceGnmTexGetFormat(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** A `GnmDataFormat` constructed from the texture's `dataformat`, `numformat`, and channel selection fields.

---

### sceGnmTexSetFormat

Sets the texture's data format from a `GnmDataFormat`. *(static inline)*

```c
static inline void sceGnmTexSetFormat(GnmTexture* tex, GnmDataFormat df);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `GnmTexture*` | Pointer to the texture descriptor to modify |
| `df` | `GnmDataFormat` | The format to apply |

---

### sceGnmTexGetWidth

Returns the texture width in texels (stored value + 1). *(static inline)*

```c
static inline uint32_t sceGnmTexGetWidth(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Texture width in texels.

---

### sceGnmTexSetWidth

Sets the texture width (stored as `width - 1`). *(static inline)*

```c
static inline void sceGnmTexSetWidth(GnmTexture* tex, uint32_t width);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `GnmTexture*` | Pointer to the texture descriptor to modify |
| `width` | `uint32_t` | Texture width in texels |

---

### sceGnmTexGetHeight

Returns the texture height in texels (stored value + 1). *(static inline)*

```c
static inline uint32_t sceGnmTexGetHeight(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Texture height in texels.

---

### sceGnmTexSetHeight

Sets the texture height (stored as `height - 1`). *(static inline)*

```c
static inline void sceGnmTexSetHeight(GnmTexture* tex, uint32_t height);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `GnmTexture*` | Pointer to the texture descriptor to modify |
| `height` | `uint32_t` | Texture height in texels |

---

### sceGnmTexGetDepth

Returns the texture depth (stored value + 1). *(static inline)*

```c
static inline uint32_t sceGnmTexGetDepth(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Texture depth.

---

### sceGnmTexSetDepth

Sets the texture depth (stored as `depth - 1`). *(static inline)*

```c
static inline void sceGnmTexSetDepth(GnmTexture* tex, uint32_t depth);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `GnmTexture*` | Pointer to the texture descriptor to modify |
| `depth` | `uint32_t` | Texture depth |

---

### sceGnmTexGetPitch

Returns the texture pitch in texels (stored value + 1). *(static inline)*

```c
static inline uint32_t sceGnmTexGetPitch(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Texture pitch in texels.

---

### sceGnmTexSetPitch

Sets the texture pitch (stored as `pitch - 1`). *(static inline)*

```c
static inline void sceGnmTexSetPitch(GnmTexture* tex, uint32_t pitch);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `GnmTexture*` | Pointer to the texture descriptor to modify |
| `pitch` | `uint32_t` | Texture pitch in texels |

---

### sceGnmTexGetBaseMipLevel

Returns the base mip level. For MSAA textures (`GNM_TEXTURE_2D_MSAA` and `GNM_TEXTURE_2D_ARRAY_MSAA`), always returns 0. *(static inline)*

```c
static inline uint32_t sceGnmTexGetBaseMipLevel(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Base mip level.

---

### sceGnmTexGetLastMipLevel

Returns the last mip level. For MSAA textures, always returns 0. *(static inline)*

```c
static inline uint32_t sceGnmTexGetLastMipLevel(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Last mip level.

---

### sceGnmTexGetNumMips

Returns the total number of mip levels (`lastlevel + 1`). *(static inline)*

```c
static inline uint32_t sceGnmTexGetNumMips(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Number of mip levels.

---

### sceGnmTexGetNumFaces

Returns the number of cube faces. Returns 6 for cubemap textures, 1 for all others. *(static inline)*

```c
static inline uint32_t sceGnmTexGetNumFaces(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** 6 for `GNM_TEXTURE_CUBEMAP`, 1 otherwise.

---

### sceGnmTexGetTotalArraySlices

Returns the total number of array slices. For 3D textures, returns 1. For other types, returns the depth. *(static inline)*

```c
static inline uint32_t sceGnmTexGetTotalArraySlices(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** 1 for 3D textures, `sceGnmTexGetDepth(tex)` otherwise.

---

### sceGnmTexGetNumArraySlices

Returns the number of array slices (computed from `basearray` and `lastarray`).

```c
uint32_t sceGnmTexGetNumArraySlices(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Number of array slices.

---

### sceGnmTexGetNumFragments

Returns the number of MSAA fragments. For MSAA textures, returns `1 << lastlevel`. For non-MSAA textures, returns 1. *(static inline)*

```c
static inline uint8_t sceGnmTexGetNumFragments(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** Number of MSAA fragments (1, 2, 4, or 8).

---

### sceGnmTexSetMemoryType

Sets the memory type and L1 cache bypass attribute for the texture. *(static inline)*

```c
static inline void sceGnmTexSetMemoryType(
    GnmTexture* tex, GnmMemoryType memtype, bool l1cachebypass
);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `GnmTexture*` | Pointer to the texture descriptor to modify |
| `memtype` | `GnmMemoryType` | GPU memory type |
| `l1cachebypass` | `bool` | If `true`, bypass the L1 cache |

---

### sceGnmTexBuildInfo

Builds a `GpaTextureInfo` struct from the texture descriptor, consolidating all texture parameters into a query-friendly format. *(static inline)*

```c
static inline GpaTextureInfo sceGnmTexBuildInfo(const GnmTexture* tex);
```

| Parameter | Type | Description |
|---|---|---|
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** A `GpaTextureInfo` struct populated with the texture's type, format, dimensions, pitch, fragments, mips, slices, tile mode, GPU mode, and pow2 padding flag.

---

### sceGnmTexCalcByteSize

Calculates the total byte size and alignment requirement for the texture's surface memory.

```c
GnmError sceGnmTexCalcByteSize(
    uint64_t* outsize, uint32_t* outalignment, const GnmTexture* tex
);
```

| Parameter | Type | Description |
|---|---|---|
| `outsize` | `uint64_t*` | Output: total byte size of the texture surface |
| `outalignment` | `uint32_t*` | Output: alignment requirement in bytes |
| `tex` | `const GnmTexture*` | Pointer to the texture descriptor |

**Returns:** `GNM_ERROR_OK` on success, or a `GnmError` code on failure.

---

## See Also

- [Types & Enums](types.md) — `GnmTextureType`, `GnmTileMode`, `GnmGpuMode`, `GnmImageFormat`, `GnmImgNumFormat`, `GnmChannel`, `GnmMemoryType`
- [Data Format](dataformat.md) — `GnmDataFormat` and predefined `GNM_FMT_*` constants
- [Sampler](sampler.md) — `GnmSampler` (S#) for texture sampling configuration
- [Buffer](buffer.md) — `GnmBuffer` (V#) for buffer resource configuration
- [Error Handling](error.md) — `GnmError` codes returned by creation functions
