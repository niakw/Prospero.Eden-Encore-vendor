# GPU Address API Reference

The `gpuaddr.h` header and its companion `pm4/gpuaddr_types.h` declare the GPU address (Gpa) library — the surface tiling, address computation, and texture decompression subsystem of the opengnm project. This library computes surface layouts, tile modes, swizzle patterns, HTile/Cmask/Fmask metadata, and performs linear-to-tiled surface conversions and block-compression decompression.

Headers: `include/gpuaddr.h`, `include/pm4/gpuaddr_types.h`

---

## GpaError

Error codes returned by all `sceGpa*` functions.

```c
typedef enum {
    GPA_ERR_OK = 0,
    GPA_ERR_INVALID_ARGS,
    GPA_ERR_OVERFLOW,
    GPA_ERR_TILING_ERROR,
    GPA_ERR_UNSUPPORTED,
    GPA_ERR_INTERNAL_ERROR,
    GPA_ERR_NOT_COMPRESSED,
} GpaError;
```

| Value | Name | Description |
|-------|------|-------------|
| `0` | `GPA_ERR_OK` | Success. |
| `1` | `GPA_ERR_INVALID_ARGS` | Invalid arguments passed to the function. |
| `2` | `GPA_ERR_OVERFLOW` | Buffer overflow — output buffer too small. |
| `3` | `GPA_ERR_TILING_ERROR` | Surface tiling configuration error. |
| `4` | `GPA_ERR_UNSUPPORTED` | The requested format or configuration is not supported. |
| `5` | `GPA_ERR_INTERNAL_ERROR` | Internal library error. |
| `6` | `GPA_ERR_NOT_COMPRESSED` | The texture is not block-compressed. |

### sceGpaStrError

```c
const char* sceGpaStrError(const GpaError err);
```

Returns a human-readable string describing the error code.

---

## Types from gpuaddr_types.h

### GpaSurfaceType

Enumerates the logical role of a GPU surface, used by `sceGpaFindOptimalSurface`.

```c
typedef enum {
    GPA_SURFACE_COLORDISPLAY,
    GPA_SURFACE_COLOR,
    GPA_SURFACE_DEPTHSTENCIL,
    GPA_SURFACE_DEPTH,
    GPA_SURFACE_STENCIL,
    GPA_SURFACE_FMASK,
    GPA_SURFACE_TEXTUREFLAT,
    GPA_SURFACE_TEXTUREVOLUME,
    GPA_SURFACE_TEXTURECUBEMAP,
    GPA_SURFACE_RWTEXTUREFLAT,
    GPA_SURFACE_RWTEXTUREVOLUME,
    GPA_SURFACE_RWTEXTURECUBEMAP,
} GpaSurfaceType;
```

| Value | Name | Description |
|-------|------|-------------|
| `0` | `GPA_SURFACE_COLORDISPLAY` | Color render target that is also displayable. |
| `1` | `GPA_SURFACE_COLOR` | Color render target. |
| `2` | `GPA_SURFACE_DEPTHSTENCIL` | Combined depth-stencil surface. |
| `3` | `GPA_SURFACE_DEPTH` | Depth-only surface. |
| `4` | `GPA_SURFACE_STENCIL` | Stencil-only surface. |
| `5` | `GPA_SURFACE_FMASK` | Fragment-mask surface (MSAA). |
| `6` | `GPA_SURFACE_TEXTUREFLAT` | 2D texture. |
| `7` | `GPA_SURFACE_TEXTUREVOLUME` | 3D texture. |
| `8` | `GPA_SURFACE_TEXTURECUBEMAP` | Cubemap texture. |
| `9` | `GPA_SURFACE_RWTEXTUREFLAT` | Read-write 2D texture (UAV). |
| `10` | `GPA_SURFACE_RWTEXTUREVOLUME` | Read-write 3D texture (UAV). |
| `11` | `GPA_SURFACE_RWTEXTURECUBEMAP` | Read-write cubemap (UAV). |

---

### GpaSurfaceFlags

Bitfield describing surface usage flags. Size: 4 bytes.

```c
typedef struct {
    uint32_t colortarget : 1;
    uint32_t depthtarget : 1;
    uint32_t stenciltarget : 1;
    uint32_t texture : 1;
    uint32_t cube : 1;
    uint32_t volume : 1;
    uint32_t fmask : 1;
    uint32_t cubeasarray : 1;
    uint32_t overlay : 1;
    uint32_t display : 1;
    uint32_t prt : 1;
    uint32_t pow2pad : 1;
    uint32_t texcompatible : 1;
    uint32_t _unused : 19;
} GpaSurfaceFlags;
_Static_assert(sizeof(GpaSurfaceFlags) == 0x4, "");
```

| Field | Bits | Description |
|-------|------|-------------|
| `colortarget` | 1 | Surface is a color render target. |
| `depthtarget` | 1 | Surface is a depth target. |
| `stenciltarget` | 1 | Surface is a stencil target. |
| `texture` | 1 | Surface is a shader-readable texture. |
| `cube` | 1 | Surface is a cubemap. |
| `volume` | 1 | Surface is a 3D volume texture. |
| `fmask` | 1 | Surface is an FMASK surface. |
| `cubeasarray` | 1 | Treat cubemap faces as array slices. |
| `overlay` | 1 | Surface is an overlay. |
| `display` | 1 | Surface is displayable. |
| `prt` | 1 | Surface is partially-resident (PRT). |
| `pow2pad` | 1 | Surface is power-of-two padded. |
| `texcompatible` | 1 | Surface is texture-compatible (TC compatible). |
| `_unused` | 19 | Reserved — must be zero. |

---

### GpaSurfaceProperties

Result of `sceGpaFindOptimalSurface` — describes the recommended tile mode and flags for a surface.

```c
typedef struct {
    GnmTileMode tilemode;
    GpaSurfaceFlags flags;
} GpaSurfaceProperties;
```

| Field | Type | Description |
|-------|------|-------------|
| `tilemode` | `GnmTileMode` | Recommended tile mode for the surface. |
| `flags` | `GpaSurfaceFlags` | Surface flags for the recommended configuration. |

---

### GpaSurfaceInfo

Computed surface layout information. Output of `sceGpaComputeSurfaceInfo`.

```c
typedef struct {
    uint32_t pitch;
    uint32_t height;
    uint32_t depth;
    uint64_t surfacesize;
    uint32_t basealign;
    uint32_t pitchalign;
    uint32_t heightalign;
    uint32_t depthalign;
    uint32_t bitsperelem;

    uint32_t blockwidth;
    uint32_t blockheight;

    GnmTileMode tilemode;
    GpaTileInfo tileinfo;

    struct {
        uint32_t istexcompatible : 1;
        uint32_t _unused : 31;
    };
} GpaSurfaceInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `pitch` | `uint32_t` | Surface pitch in elements. |
| `height` | `uint32_t` | Surface height in elements. |
| `depth` | `uint32_t` | Surface depth (for 3D textures). |
| `surfacesize` | `uint64_t` | Total surface size in bytes. |
| `basealign` | `uint32_t` | Base address alignment in bytes. |
| `pitchalign` | `uint32_t` | Pitch alignment in elements. |
| `heightalign` | `uint32_t` | Height alignment in elements. |
| `depthalign` | `uint32_t` | Depth alignment in elements. |
| `bitsperelem` | `uint32_t` | Bits per element. |
| `blockwidth` | `uint32_t` | Block width (for compressed formats). |
| `blockheight` | `uint32_t` | Block height (for compressed formats). |
| `tilemode` | `GnmTileMode` | Tile mode used for the surface. |
| `tileinfo` | `GpaTileInfo` | Detailed tile configuration. |
| `istexcompatible` | `uint32_t:1` | Whether the surface is texture-compatible. |
| `_unused` | `uint32_t:31` | Reserved. |

---

### GpaTilingParams

Parameters describing how a surface should be tiled. Used by the tiler and surface-computation functions.

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

| Field | Type | Description |
|-------|------|-------------|
| `tilemode` | `GnmTileMode` | Tile mode to use. |
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode (base or Neo). |
| `linearwidth` | `uint32_t` | Width in linear (untiled) space. |
| `linearheight` | `uint32_t` | Height in linear space. |
| `lineardepth` | `uint32_t` | Depth in linear space. |
| `numfragsperpixel` | `uint32_t` | Number of fragments per pixel (MSAA). |
| `basetiledpitch` | `uint32_t` | Base tiled pitch (for mip-level computation). |
| `miplevel` | `uint32_t` | Mip level being tiled. |
| `arrayslice` | `uint32_t` | Array slice being tiled. |
| `surfaceflags` | `GpaSurfaceFlags` | Surface usage flags. |
| `bitsperfrag` | `uint32_t` | Bits per fragment. |
| `isblockcompressed` | `bool` | Whether the surface is block-compressed. |

---

### GpaTextureInfo

Describes a complete texture for tiling and decompression operations.

```c
typedef struct {
    GnmTextureType type;
    GnmDataFormat fmt;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t depth;

    uint32_t numfrags;
    uint32_t nummips;
    uint32_t numslices;

    GnmTileMode tm;
    GnmGpuMode mingpumode;

    bool pow2pad;
} GpaTextureInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | `GnmTextureType` | Texture dimensionality (2D, 3D, cubemap, etc.). |
| `fmt` | `GnmDataFormat` | Surface data format. |
| `width` | `uint32_t` | Texture width in texels. |
| `height` | `uint32_t` | Texture height in texels. |
| `pitch` | `uint32_t` | Texture pitch in texels. |
| `depth` | `uint32_t` | Texture depth (for 3D). |
| `numfrags` | `uint32_t` | Number of fragments per pixel (MSAA). |
| `nummips` | `uint32_t` | Number of mip levels. |
| `numslices` | `uint32_t` | Number of array slices. |
| `tm` | `GnmTileMode` | Tile mode. |
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode. |
| `pow2pad` | `bool` | Whether the texture is power-of-two padded. |

---

### GpaTileInfo

Detailed tile configuration parameters.

```c
typedef struct {
    GnmArrayMode arraymode;
    GnmNumBanks banks;
    GnmBankWidth bankwidth;
    GnmBankHeight bankheight;
    GnmMacroTileAspect macroaspectratio;
    GnmTileSplit tilesplit;
    GnmPipeConfig pipeconfig;
} GpaTileInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `arraymode` | `GnmArrayMode` | Array mode (linear, 1D/2D/3D tiling). |
| `banks` | `GnmNumBanks` | Number of memory banks. |
| `bankwidth` | `GnmBankWidth` | Bank width in tiles. |
| `bankheight` | `GnmBankHeight` | Bank height in tiles. |
| `macroaspectratio` | `GnmMacroTileAspect` | Macro-tile aspect ratio. |
| `tilesplit` | `GnmTileSplit` | Tile split size. |
| `pipeconfig` | `GnmPipeConfig` | Pipe configuration. |

---

### GpaHtileParams / GpaHtileInfo

Parameters and computed info for HTile (hierarchical depth buffer) surfaces.

#### GpaHtileParams

```c
typedef struct {
    uint32_t pitch;
    uint32_t height;
    uint32_t numslices;
    uint32_t numfrags;
    uint32_t bpp;

    GnmArrayMode arraymode;
    GnmNumBanks banks;
    GnmPipeConfig pipeconfig;

    GnmGpuMode mingpumode;

    struct {
        uint32_t tccompatible : 1;
        uint32_t reserved : 31;
    } flags;
} GpaHtileParams;
```

| Field | Type | Description |
|-------|------|-------------|
| `pitch` | `uint32_t` | Surface pitch. |
| `height` | `uint32_t` | Surface height. |
| `numslices` | `uint32_t` | Number of array slices. |
| `numfrags` | `uint32_t` | Number of fragments (MSAA). |
| `bpp` | `uint32_t` | Bits per pixel. |
| `arraymode` | `GnmArrayMode` | Array mode. |
| `banks` | `GnmNumBanks` | Number of memory banks. |
| `pipeconfig` | `GnmPipeConfig` | Pipe configuration. |
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode. |
| `tccompatible` | `uint32_t:1` | TC-compatible flag. |
| `reserved` | `uint32_t:31` | Reserved. |

#### GpaHtileInfo

```c
typedef struct {
    uint32_t pitch;
    uint32_t height;
    uint32_t basealign;
    uint32_t bpp;
    uint32_t macrowidth;
    uint32_t macroheight;
    uint64_t htilebytes;
    uint64_t slicebytes;
} GpaHtileInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `pitch` | `uint32_t` | HTile pitch. |
| `height` | `uint32_t` | HTile height. |
| `basealign` | `uint32_t` | Base address alignment. |
| `bpp` | `uint32_t` | Bits per pixel. |
| `macrowidth` | `uint32_t` | Macro-tile width. |
| `macroheight` | `uint32_t` | Macro-tile height. |
| `htilebytes` | `uint64_t` | Total HTile size in bytes. |
| `slicebytes` | `uint64_t` | Size per slice in bytes. |

---

### GpaCmaskParams / GpaCmaskInfo

Parameters and computed info for Cmask (color compression mask) surfaces.

#### GpaCmaskParams

```c
typedef struct {
    uint32_t pitch;
    uint32_t height;
    uint32_t numslices;
    uint32_t numfrags;
    uint32_t bpp;
    GnmTileMode tilemode;

    GnmGpuMode mingpumode;

    struct {
        uint32_t tccompatible : 1;
        uint32_t reserved : 31;
    } flags;
} GpaCmaskParams;
```

| Field | Type | Description |
|-------|------|-------------|
| `pitch` | `uint32_t` | Surface pitch. |
| `height` | `uint32_t` | Surface height. |
| `numslices` | `uint32_t` | Number of array slices. |
| `numfrags` | `uint32_t` | Number of fragments (MSAA). |
| `bpp` | `uint32_t` | Bits per pixel. |
| `tilemode` | `GnmTileMode` | Tile mode. |
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode. |
| `tccompatible` | `uint32_t:1` | TC-compatible flag. |
| `reserved` | `uint32_t:31` | Reserved. |

#### GpaCmaskInfo

```c
typedef struct {
    uint32_t pitch;
    uint32_t height;
    uint32_t basealign;
    uint32_t bpp;
    uint32_t macrowidth;
    uint32_t macroheight;
    uint32_t blockmax;
    uint64_t cmaskbytes;
    uint64_t slicebytes;
} GpaCmaskInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `pitch` | `uint32_t` | Cmask pitch. |
| `height` | `uint32_t` | Cmask height. |
| `basealign` | `uint32_t` | Base address alignment. |
| `bpp` | `uint32_t` | Bits per pixel. |
| `macrowidth` | `uint32_t` | Macro-tile width. |
| `macroheight` | `uint32_t` | Macro-tile height. |
| `blockmax` | `uint32_t` | Maximum block index. |
| `cmaskbytes` | `uint64_t` | Total Cmask size in bytes. |
| `slicebytes` | `uint64_t` | Size per slice in bytes. |

---

### GpaFmaskParams / GpaFmaskInfo

Parameters and computed info for FMASK (fragment mask) surfaces used in MSAA.

#### GpaFmaskParams

```c
typedef struct {
    uint32_t pitch;
    uint32_t height;
    uint32_t numslices;
    uint32_t numfrags;
    uint32_t bpp;
    GnmTileMode tilemode;

    GnmGpuMode mingpumode;

    bool isblockcompressed;
} GpaFmaskParams;
```

| Field | Type | Description |
|-------|------|-------------|
| `pitch` | `uint32_t` | Surface pitch. |
| `height` | `uint32_t` | Surface height. |
| `numslices` | `uint32_t` | Number of array slices. |
| `numfrags` | `uint32_t` | Number of fragments (MSAA). |
| `bpp` | `uint32_t` | Bits per pixel. |
| `tilemode` | `GnmTileMode` | Tile mode. |
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode. |
| `isblockcompressed` | `bool` | Whether the FMASK is block-compressed. |

#### GpaFmaskInfo

```c
typedef struct {
    uint32_t pitch;
    uint32_t height;
    uint32_t basealign;
    uint32_t pitchalign;
    uint32_t heightalign;
    uint32_t bpp;
    uint64_t fmaskbytes;
    uint64_t slicebytes;
} GpaFmaskInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `pitch` | `uint32_t` | FMASK pitch. |
| `height` | `uint32_t` | FMASK height. |
| `basealign` | `uint32_t` | Base address alignment. |
| `pitchalign` | `uint32_t` | Pitch alignment. |
| `heightalign` | `uint32_t` | Height alignment. |
| `bpp` | `uint32_t` | Bits per pixel. |
| `fmaskbytes` | `uint64_t` | Total FMASK size in bytes. |
| `slicebytes` | `uint64_t` | Size per slice in bytes. |

---

### GpaSurfaceContext

Context for surface coordinate computation. Initialised by `sceGpaInitSurfaceContext`.

```c
typedef struct {
    GnmGpuMode mingpumode;
    GnmTileMode tilemode;
    GpaTileInfo tileinfo;

    uint32_t linearwidth;
    uint32_t linearheight;
    uint32_t lineardepth;
    uint32_t paddedwidth;
    uint32_t paddedheight;
    uint32_t paddeddepth;

    uint32_t bitsperelement;
    uint32_t numfragsperpixel;

    uint32_t bankswizzlemask;
    uint32_t pipeswizzlemask;
} GpaSurfaceContext;
```

| Field | Type | Description |
|-------|------|-------------|
| `mingpumode` | `GnmGpuMode` | Minimum GPU mode. |
| `tilemode` | `GnmTileMode` | Tile mode. |
| `tileinfo` | `GpaTileInfo` | Detailed tile configuration. |
| `linearwidth` | `uint32_t` | Linear width. |
| `linearheight` | `uint32_t` | Linear height. |
| `lineardepth` | `uint32_t` | Linear depth. |
| `paddedwidth` | `uint32_t` | Padded width (after alignment). |
| `paddedheight` | `uint32_t` | Padded height. |
| `paddeddepth` | `uint32_t` | Padded depth. |
| `bitsperelement` | `uint32_t` | Bits per element. |
| `numfragsperpixel` | `uint32_t` | Fragments per pixel (MSAA). |
| `bankswizzlemask` | `uint32_t` | Bank swizzle mask. |
| `pipeswizzlemask` | `uint32_t` | Pipe swizzle mask. |

---

### GpaSurfaceRegion

Defines a sub-region of a surface for partial tiling operations.

```c
typedef struct {
    uint32_t left;   // -X
    uint32_t top;    // -Y
    uint32_t front;  // -Z

    uint32_t right;  // +X
    uint32_t bottom; // +Y
    uint32_t back;   // +Z
} GpaSurfaceRegion;
```

| Field | Type | Description |
|-------|------|-------------|
| `left` | `uint32_t` | Minimum X coordinate. |
| `top` | `uint32_t` | Minimum Y coordinate. |
| `front` | `uint32_t` | Minimum Z coordinate. |
| `right` | `uint32_t` | Maximum X coordinate. |
| `bottom` | `uint32_t` | Maximum Y coordinate. |
| `back` | `uint32_t` | Maximum Z coordinate. |

---

### GpaSurfaceIndex

Index tuple identifying a specific sub-resource within a texture.

```c
typedef struct {
    uint32_t arrayindex;
    uint32_t face;
    uint32_t mip;
    uint32_t depth;
    uint32_t fragment;
    uint32_t sample;
} GpaSurfaceIndex;
```

| Field | Type | Description |
|-------|------|-------------|
| `arrayindex` | `uint32_t` | Array slice index. |
| `face` | `uint32_t` | Cubemap face index. |
| `mip` | `uint32_t` | Mip level. |
| `depth` | `uint32_t` | Depth slice (for 3D). |
| `fragment` | `uint32_t` | Fragment index (MSAA). |
| `sample` | `uint32_t` | Sample index (MSAA). |

---

## Surface Computation

Functions that compute surface layout information (size, alignment, tile mode) from tiling parameters.

### sceGpaComputeSurfaceInfo

```c
GpaError sceGpaComputeSurfaceInfo(GpaSurfaceInfo* out, const GpaTilingParams* tp);
```

Computes the full surface layout (size, alignment, pitch, height, tile info) from tiling parameters.

### sceGpaComputeHtileInfo

```c
GpaError sceGpaComputeHtileInfo(
    GpaHtileInfo* outinfo, const GpaHtileParams* params
);
```

Computes HTile (hierarchical depth buffer) layout information.

### sceGpaComputeCmaskInfo

```c
GpaError sceGpaComputeCmaskInfo(
    GpaCmaskInfo* outinfo, const GpaCmaskParams* params
);
```

Computes Cmask (color compression mask) layout information.

### sceGpaComputeFmaskInfo

```c
GpaError sceGpaComputeFmaskInfo(
    GpaFmaskInfo* outinfo, const GpaFmaskParams* params
);
```

Computes FMASK (fragment mask) layout information.

### sceGpaComputeSurfaceTileMode

```c
GpaError sceGpaComputeSurfaceTileMode(
    GnmTileMode* outtilemode, GnmGpuMode mingpumode, GnmArrayMode arraymode,
    GpaSurfaceFlags flags, GnmDataFormat surfacefmt, uint32_t numfragsperpixel,
    GnmMicroTileMode mtm
);
```

Determines the optimal tile mode for a surface given its properties and GPU mode.

### sceGpaInitSurfaceContext

```c
GpaError sceGpaInitSurfaceContext(
    GpaSurfaceContext* ctx, size_t surfsize, const GpaTilingParams* tp
);
```

Initialises a `GpaSurfaceContext` for subsequent coordinate computations.

### sceGpaComputeSurfaceCoord

```c
GpaError sceGpaComputeSurfaceCoord(
    uint64_t* outoffset, uint64_t* outbitoffset, const GpaSurfaceContext* ctx,
    uint32_t x, uint32_t y, uint32_t z, uint32_t fragindex
);
```

Computes the byte offset and bit offset of a specific texel within a tiled surface.

### sceGpaComputeSurfaceSizeOffset

```c
GpaError sceGpaComputeSurfaceSizeOffset(
    uint64_t* outsize, uint64_t* outoffset, const GpaTextureInfo* tex,
    uint32_t miplevel, uint32_t arrayslice
);
```

Computes the size and offset of a specific mip level and array slice within a texture.

---

## Surface Generation

### sceGpaFindOptimalSurface

```c
GpaError sceGpaFindOptimalSurface(
    GpaSurfaceProperties* outprops, GpaSurfaceType surfacetype, uint32_t bpp,
    uint32_t numfrags, bool mipmapped, GnmGpuMode mingpumode
);
```

Finds the optimal surface configuration (tile mode and flags) for the given surface type and parameters.

---

## Element / Utility

### sceGpaGetTileInfo

```c
GpaError sceGpaGetTileInfo(
    GpaTileInfo* outinfo, GnmTileMode tilemode, uint32_t bpp, uint32_t numfrags,
    GnmGpuMode gpumode
);
```

Retrieves the `GpaTileInfo` (array mode, banks, bank width/height, pipe config, etc.) for a given tile mode.

### sceGpaComputeBaseSwizzle

```c
GpaError sceGpaComputeBaseSwizzle(
    uint32_t* outswizzle, GnmTileMode tilemode, uint32_t surfindex,
    uint32_t bpp, uint32_t numfrags, GnmGpuMode gpumode
);
```

Computes the base swizzle value for a surface from its tile mode and index.

---

## Decompression

### sceGpaGetDecompressedSize

```c
GpaError sceGpaGetDecompressedSize(
    uint64_t* outlen, const GpaTextureInfo* texinfo
);
```

Returns the decompressed size of a block-compressed texture.

### sceGpaDecompressTexture

```c
GpaError sceGpaDecompressTexture(
    void* outbuf, uint64_t outlen, const void* inbuf, uint64_t inlen,
    const GpaTextureInfo* texinfo, GnmDataFormat* outfmt
);
```

Decompresses a block-compressed texture into a linear buffer. Returns the output format in `*outfmt`.

---

## Tiler

Functions for converting between linear and tiled surface layouts.

### sceGpaTpInit

```c
GpaError sceGpaTpInit(
    GpaTilingParams* tp, const GpaTextureInfo* tex, uint32_t miplevel,
    uint32_t arrayslice
);
```

Initialises `GpaTilingParams` from a `GpaTextureInfo` for a specific mip level and array slice.

### sceGpaTileSurface

```c
GpaError sceGpaTileSurface(
    void* outbuf, size_t outlen, const void* inbuf, size_t inlen,
    const GpaTilingParams* srctp, const GpaTilingParams* dst_tp
);
```

Converts an entire surface from one tiling configuration to another.

### sceGpaTileSurfaceRegion

```c
GpaError sceGpaTileSurfaceRegion(
    void* outbuf, size_t outlen, const void* inbuf, size_t inlen,
    const GpaTilingParams* srctp, const GpaTilingParams* dst_tp,
    const GpaSurfaceRegion* region
);
```

Converts a sub-region of a surface from one tiling configuration to another.

### sceGpaTileTextureIndexed

```c
GpaError sceGpaTileTextureIndexed(
    const void* inbuf, size_t inlen, void* outbuf, size_t outlen,
    const GpaTextureInfo* texinfo, GnmTileMode newtiling, uint32_t mip,
    uint32_t slice
);
```

Re-tiles a specific mip level and array slice of a texture to a new tile mode.

### sceGpaTileTextureAll

```c
GpaError sceGpaTileTextureAll(
    const void* inbuf, size_t inlen, void* outbuf, size_t outlen,
    const GpaTextureInfo* texinfo, GnmTileMode newtiling
);
```

Re-tiles all mip levels and array slices of a texture to a new tile mode.

---

## See Also

- [GNM Driver API](driver.md) — runtime driver functions for draw, dispatch, submit, and shader binding
- [Helpers API](helpers.md) — direct memory, VideoOut, texture/render-target creation, shader metadata
- [API Index](index.md) — complete function listing across all headers
