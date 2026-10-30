# Data Format

The `GnmDataFormat` union describes the complete format of a GPU surface or texture — combining the image data format (component bit-widths), numeric type (UNORM, FLOAT, SRGB, etc.), and per-channel source mapping (which component goes to X, Y, Z, W). This header (`gnm_dataformat.h`) provides the format union, initialization and query functions, and a set of predefined `GNM_FMT_*` constants for common formats.

---

## GnmDataFormat Union

A 4-byte (`0x4` bytes) union that packs the surface format, channel type, and four channel selectors into a single `uint32_t`.

```c
typedef union {
    struct {
        GnmImageFormat surfacefmt : 8;
        GnmImgNumFormat chantype : 4;
        GnmChannel chanx : 3;
        GnmChannel chany : 3;
        GnmChannel chanz : 3;
        GnmChannel chanw : 3;
        uint32_t _unused : 8;
    };
    uint32_t asuint;
} GnmDataFormat;
```

_Static_assert: `sizeof(GnmDataFormat) == 0x4`

### Bitfield Layout

| Field | Bits | Type | Description |
|---|---|---|---|
| `surfacefmt` | 8 | `GnmImageFormat` | Image data format (component bit-width layout) |
| `chantype` | 4 | `GnmImgNumFormat` | Numeric interpretation (UNORM, FLOAT, SRGB, etc.) |
| `chanx` | 3 | `GnmChannel` | Source channel for X output |
| `chany` | 3 | `GnmChannel` | Source channel for Y output |
| `chanz` | 3 | `GnmChannel` | Source channel for Z output |
| `chanw` | 3 | `GnmChannel` | Source channel for W output |
| `_unused` | 8 | `uint32_t` | Reserved / unused bits |
| `asuint` | 32 | `uint32_t` | Raw uint32 view of the entire format |

---

## GNM_DATA_FORMAT_INIT Macro

Compile-time initializer macro for `GnmDataFormat`. Produces a compound literal suitable for static initialization.

```c
#define GNM_DATA_FORMAT_INIT(_surfacefmt, _chantype, _chanx, _chany, _chanz, _chanw) \
    {{(_surfacefmt), (_chantype), (_chanx), (_chany), (_chanz), (_chanw), 0}}
```

| Parameter | Type | Description |
|---|---|---|
| `_surfacefmt` | `GnmImageFormat` | Image data format |
| `_chantype` | `GnmImgNumFormat` | Numeric type |
| `_chanx` | `GnmChannel` | X channel source |
| `_chany` | `GnmChannel` | Y channel source |
| `_chanz` | `GnmChannel` | Z channel source |
| `_chanw` | `GnmChannel` | W channel source |

---

## Functions

### sceGnmDfInitFromFmask

Initializes a `GnmDataFormat` for an FMASK surface given the number of samples and fragments.

```c
GnmDataFormat sceGnmDfInitFromFmask(uint32_t numsamples, uint32_t numfrags);
```

| Parameter | Type | Description |
|---|---|---|
| `numsamples` | `uint32_t` | Number of MSAA samples |
| `numfrags` | `uint32_t` | Number of fragment samples |

**Returns:** A `GnmDataFormat` configured for the specified FMASK layout.

---

### sceGnmDfInitFromZ

Initializes a `GnmDataFormat` from a depth (`GnmZFormat`) format.

```c
GnmDataFormat sceGnmDfInitFromZ(GnmZFormat zfmt);
```

| Parameter | Type | Description |
|---|---|---|
| `zfmt` | `GnmZFormat` | Depth format (`GNM_Z_16`, `GNM_Z_24`, `GNM_Z_32_FLOAT`) |

**Returns:** A `GnmDataFormat` configured for the specified depth format.

---

### sceGnmDfInitFromStencil

Initializes a `GnmDataFormat` from a stencil format. *(static inline)*

```c
static inline GnmDataFormat sceGnmDfInitFromStencil(
    GnmStencilFormat stencilfmt, GnmImgNumFormat chantype
);
```

| Parameter | Type | Description |
|---|---|---|
| `stencilfmt` | `GnmStencilFormat` | Stencil format (`GNM_STENCIL_8` or `GNM_STENCIL_INVALID`) |
| `chantype` | `GnmImgNumFormat` | Numeric type for the stencil channel |

**Returns:** A `GnmDataFormat` with all channels mapped to `GNM_CHAN_X`, using `GNM_IMG_DATA_FORMAT_8` for 8-bit stencil.

---

### sceGnmDfGetTexelsPerElement

Returns the number of texels per element for the given format. Block-compressed formats (BC1–BC7) return 16; 1-bit formats return 8; all others return 1. *(static inline)*

```c
static inline uint32_t sceGnmDfGetTexelsPerElement(const GnmDataFormat datafmt);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |

**Returns:** Number of texels per element (1, 8, or 16).

---

### sceGnmDfGetNumComponents

Returns the number of active components in the format.

```c
uint32_t sceGnmDfGetNumComponents(const GnmDataFormat datafmt);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |

**Returns:** Number of components (1–4).

---

### sceGnmDfGetBitsPerElement

Returns the number of bits per element (per texel) for the given format.

```c
uint32_t sceGnmDfGetBitsPerElement(const GnmDataFormat datafmt);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |

**Returns:** Bits per element (e.g. 8 for R8, 32 for R8G8B8A8).

---

### sceGnmDfGetTotalBitsPerElement

Returns the total bits per element, accounting for texels-per-element (e.g. BC formats). *(static inline)*

```c
static inline uint32_t sceGnmDfGetTotalBitsPerElement(const GnmDataFormat fmt);
```

| Parameter | Type | Description |
|---|---|---|
| `fmt` | `const GnmDataFormat` | The data format to query |

**Returns:** `sceGnmDfGetBitsPerElement(fmt) * sceGnmDfGetTexelsPerElement(fmt)`

---

### sceGnmDfGetBytesPerElement

Returns the bytes per element (bits per element / 8). *(static inline)*

```c
static inline uint32_t sceGnmDfGetBytesPerElement(const GnmDataFormat datafmt);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |

**Returns:** Bytes per element.

---

### sceGnmDfGetTotalBytesPerElement

Returns the total bytes per element, accounting for texels-per-element. *(static inline)*

```c
static inline uint32_t sceGnmDfGetTotalBytesPerElement(const GnmDataFormat fmt);
```

| Parameter | Type | Description |
|---|---|---|
| `fmt` | `const GnmDataFormat` | The data format to query |

**Returns:** `sceGnmDfGetTotalBitsPerElement(fmt) / 8`

---

### sceGnmDfIsBlockCompressed

Returns whether the format uses block compression (BC1–BC7). *(static inline)*

```c
static inline bool sceGnmDfIsBlockCompressed(const GnmDataFormat datafmt);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |

**Returns:** `true` if the format is BC1–BC7, `false` otherwise.

---

### sceGnmDfGetRtChannelType

Retrieves the render target channel numeric type (`GnmSurfaceNumber`) for the given format.

```c
bool sceGnmDfGetRtChannelType(const GnmDataFormat datafmt, GnmSurfaceNumber* out);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |
| `out` | `GnmSurfaceNumber*` | Output: the surface number type |

**Returns:** `true` if the format has a valid render target channel type, `false` otherwise.

---

### sceGnmDfGetRtChannelOrder

Retrieves the render target channel swap order (`GnmSurfaceSwap`) for the given format.

```c
bool sceGnmDfGetRtChannelOrder(const GnmDataFormat datafmt, GnmSurfaceSwap* out);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |
| `out` | `GnmSurfaceSwap*` | Output: the surface swap order |

**Returns:** `true` if the format has a valid render target channel order, `false` otherwise.

---

### sceGnmDfGetZFormat

Returns the depth (`GnmZFormat`) format corresponding to the given data format.

```c
GnmZFormat sceGnmDfGetZFormat(const GnmDataFormat datafmt);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |

**Returns:** The equivalent `GnmZFormat`, or `GNM_Z_INVALID` if not a depth format.

---

### sceGnmDfGetStencilFormat

Returns the stencil format corresponding to the given data format.

```c
GnmStencilFormat sceGnmDfGetStencilFormat(const GnmDataFormat datafmt);
```

| Parameter | Type | Description |
|---|---|---|
| `datafmt` | `const GnmDataFormat` | The data format to query |

**Returns:** The equivalent `GnmStencilFormat`, or `GNM_STENCIL_INVALID` if not a stencil format.

---

### sceGnmDfGetTexelsPerElementWide

Returns the "wide" texels-per-element count. BC formats return 4, 1-bit formats return 8, GB_GR/BG_RG formats return 2, all others return 1. *(static inline)*

```c
static inline uint32_t sceGnmDfGetTexelsPerElementWide(const GnmDataFormat fmt);
```

| Parameter | Type | Description |
|---|---|---|
| `fmt` | `const GnmDataFormat` | The data format to query |

**Returns:** Wide texels-per-element count (1, 2, 4, or 8).

---

### sceGnmDfGetTexelsPerElementTall

Returns the "tall" texels-per-element count. BC formats return 4, all others return 1. *(static inline)*

```c
static inline uint32_t sceGnmDfGetTexelsPerElementTall(const GnmDataFormat fmt);
```

| Parameter | Type | Description |
|---|---|---|
| `fmt` | `const GnmDataFormat` | The data format to query |

**Returns:** Tall texels-per-element count (1 or 4).

---

## Predefined Format Constants

All predefined formats are `static const GnmDataFormat` initialized via `GNM_DATA_FORMAT_INIT`. The table below shows each constant with its surface format, numeric type, and channel mapping.

| Constant | Surface Format | Num Format | X | Y | Z | W |
|---|---|---|---|---|---|---|
| `GNM_FMT_INVALID` | `INVALID` | `UNORM` | `CONSTANT0` | `CONSTANT0` | `CONSTANT0` | `CONSTANT0` |
| `GNM_FMT_R8_UNORM` | `8` | `UNORM` | `X` | `CONSTANT0` | `CONSTANT0` | `CONSTANT1` |
| `GNM_FMT_A8_UNORM` | `8` | `UNORM` | `CONSTANT0` | `CONSTANT0` | `CONSTANT0` | `X` |
| `GNM_FMT_R8G8B8A8_SRGB` | `8_8_8_8` | `SRGB` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_R8G8B8A8_UNORM` | `8_8_8_8` | `UNORM` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_R8G8B8A8_UINT` | `8_8_8_8` | `UINT` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_B8G8R8A8_SRGB` | `8_8_8_8` | `SRGB` | `Z` | `Y` | `X` | `W` |
| `GNM_FMT_B8G8R8A8_UNORM` | `8_8_8_8` | `UNORM` | `Z` | `Y` | `X` | `W` |
| `GNM_FMT_R16_UNORM` | `16` | `UNORM` | `X` | `CONSTANT0` | `CONSTANT0` | `CONSTANT1` |
| `GNM_FMT_R16G16_FLOAT` | `16_16` | `FLOAT` | `X` | `Y` | `CONSTANT0` | `CONSTANT1` |
| `GNM_FMT_R16G16B16A16_SRGB` | `16_16_16_16` | `SRGB` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_R16G16B16A16_UNORM` | `16_16_16_16` | `UNORM` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_R16G16B16A16_FLOAT` | `16_16_16_16` | `FLOAT` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_R32_FLOAT` | `32` | `FLOAT` | `X` | `CONSTANT0` | `CONSTANT0` | `CONSTANT1` |
| `GNM_FMT_R32G32_FLOAT` | `32_32` | `FLOAT` | `X` | `Y` | `CONSTANT0` | `CONSTANT1` |
| `GNM_FMT_R32G32B32_UNORM` | `32_32_32` | `UNORM` | `X` | `Y` | `Z` | `CONSTANT0` |
| `GNM_FMT_R32G32B32_FLOAT` | `32_32_32` | `FLOAT` | `X` | `Y` | `Z` | `CONSTANT1` |
| `GNM_FMT_R32G32B32A32_SRGB` | `32_32_32_32` | `SRGB` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_R32G32B32A32_UNORM` | `32_32_32_32` | `UNORM` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_R32G32B32A32_FLOAT` | `32_32_32_32` | `FLOAT` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_BC1_UNORM` | `BC1` | `UNORM` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_BC1_SRGB` | `BC1` | `SRGB` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_BC3_UNORM` | `BC3` | `UNORM` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_BC6_SNORM` | `BC6` | `SNORM` | `X` | `Y` | `Z` | `CONSTANT1` |
| `GNM_FMT_BC6_UNORM` | `BC6` | `UNORM` | `X` | `Y` | `Z` | `CONSTANT1` |
| `GNM_FMT_BC7_UNORM` | `BC7` | `UNORM` | `X` | `Y` | `Z` | `W` |
| `GNM_FMT_BC7_SRGB` | `BC7` | `SRGB` | `X` | `Y` | `Z` | `W` |

---

## See Also

- [Types & Enums](types.md) — `GnmImageFormat`, `GnmImgNumFormat`, `GnmChannel`, `GnmZFormat`, `GnmStencilFormat`
- [Buffer](buffer.md) — Uses `GnmDataFormat` for buffer format get/set
- [Texture](texture.md) — Uses `GnmDataFormat` for texture format get/set
