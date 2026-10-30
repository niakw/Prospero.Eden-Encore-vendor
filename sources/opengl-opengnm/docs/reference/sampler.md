# Sampler

The `GnmSampler` struct describes a GPU sampler state (S# / S-sharp) — used to control how textures are sampled, including filtering, addressing modes, LOD clamping, anisotropy, and border color. This header (`gnm_sampler.h`) defines the 4-register sampler descriptor and its bitfield layout.

---

## GnmSampler Struct

A 16-byte (`0x10` bytes) sampler descriptor consisting of 4 registers, each 32 bits wide.

```c
typedef struct {
    /* register 0 */
    GnmTexClamp clampx : 3;
    GnmTexClamp clampy : 3;
    GnmTexClamp clampz : 3;
    uint32_t maxanisoratio : 3;
    GnmDepthCompare depthcomparefunc : 3;
    uint32_t forceunormalized : 1;
    uint32_t anisothreshold : 3;
    uint32_t mccoordtrunc : 1;
    uint32_t forcedegamma : 1;
    uint32_t anisobias : 6;
    uint32_t trunccoord : 1;
    uint32_t disablecubewrap : 1;
    GnmFilterMode filtermode : 2;
    uint32_t _unused : 1;

    /* register 1 */
    uint32_t minlod : 12;
    uint32_t maxlod : 12;
    uint32_t perfmip : 4;
    uint32_t perfz : 4;

    /* register 2 */
    uint32_t lodbias : 14;
    uint32_t lodbiassec : 6;
    GnmFilter xymagfilter : 2;
    GnmFilter xyminfilter : 2;
    GnmZFilter zfilter : 2;
    GnmMipFilter mipfilter : 2;
    uint32_t mippointpreclamp : 1;
    uint32_t disablelsbceil : 1;
    uint32_t _unused2 : 2;

    /* register 3 */
    uint32_t bordercolorptr : 12;
    uint32_t _unused3 : 18;
    GnmBorderColor bordercolortype : 2;
} GnmSampler;
```

_Static_assert: `sizeof(GnmSampler) == 0x10`

### Field Reference

#### Register 0 — Addressing, Anisotropy, Compare

| Field | Bits | Type | Description |
|---|---|---|---|
| `clampx` | 3 | `GnmTexClamp` | Texture coordinate clamping mode for U (X) axis |
| `clampy` | 3 | `GnmTexClamp` | Texture coordinate clamping mode for V (Y) axis |
| `clampz` | 3 | `GnmTexClamp` | Texture coordinate clamping mode for W (Z) axis |
| `maxanisoratio` | 3 | `uint32_t` | Maximum anisotropy ratio (log2: 0=1x, 1=2x, 2=4x, ...) |
| `depthcomparefunc` | 3 | `GnmDepthCompare` | Depth comparison function for compare-sampling |
| `forceunormalized` | 1 | `uint32_t` | Force unnormalized texture coordinates |
| `anisothreshold` | 3 | `uint32_t` | Anisotropy threshold |
| `mccoordtrunc` | 1 | `uint32_t` | Multi-sample coordinate truncation enable |
| `forcedegamma` | 1 | `uint32_t` | Force de-gamma (sRGB to linear) on sample |
| `anisobias` | 6 | `uint32_t` | Anisotropy bias |
| `trunccoord` | 1 | `uint32_t` | Truncate texture coordinates |
| `disablecubewrap` | 1 | `uint32_t` | Disable cube map wrapping |
| `filtermode` | 2 | `GnmFilterMode` | Filter mode (blend, min, max) |
| `_unused` | 1 | `uint32_t` | Reserved |

#### Register 1 — LOD Clamping & Performance

| Field | Bits | Type | Description |
|---|---|---|---|
| `minlod` | 12 | `uint32_t` | Minimum LOD clamp |
| `maxlod` | 12 | `uint32_t` | Maximum LOD clamp |
| `perfmip` | 4 | `uint32_t` | Per-mip performance modulation |
| `perfz` | 4 | `uint32_t` | Per-Z performance modulation |

#### Register 2 — Filtering & LOD Bias

| Field | Bits | Type | Description |
|---|---|---|---|
| `lodbias` | 14 | `uint32_t` | LOD bias (integer part) |
| `lodbiassec` | 6 | `uint32_t` | LOD bias (fractional part) |
| `xymagfilter` | 2 | `GnmFilter` | XY magnification filter (point, bilinear, aniso) |
| `xyminfilter` | 2 | `GnmFilter` | XY minification filter (point, bilinear, aniso) |
| `zfilter` | 2 | `GnmZFilter` | Z (depth) filter mode (none, point, linear) |
| `mipfilter` | 2 | `GnmMipFilter` | Mipmap filter mode (none, point, linear) |
| `mippointpreclamp` | 1 | `uint32_t` | Mip point pre-clamp enable |
| `disablelsbceil` | 1 | `uint32_t` | Disable LSB ceiling |
| `_unused2` | 2 | `uint32_t` | Reserved |

#### Register 3 — Border Color

| Field | Bits | Type | Description |
|---|---|---|---|
| `bordercolorptr` | 12 | `uint32_t` | Border color table pointer/index |
| `_unused3` | 18 | `uint32_t` | Reserved |
| `bordercolortype` | 2 | `GnmBorderColor` | Border color type (transparent black, opaque black, opaque white, from table) |

---

## Functions

### sceGnmSampGetAnisotropyRatio

Returns the actual anisotropy ratio as a power of 2. The `maxanisoratio` field stores the log2 value, so this function returns `1 << maxanisoratio`. *(static inline)*

```c
static inline uint8_t sceGnmSampGetAnisotropyRatio(const GnmSampler* s);
```

| Parameter | Type | Description |
|---|---|---|
| `s` | `const GnmSampler*` | Pointer to the sampler descriptor |

**Returns:** The anisotropy ratio (1, 2, 4, 8, 16, etc.).

---

## See Also

- [Types & Enums](types.md) — `GnmTexClamp`, `GnmDepthCompare`, `GnmFilterMode`, `GnmFilter`, `GnmZFilter`, `GnmMipFilter`, `GnmBorderColor`
- [Texture](texture.md) — `GnmTexture` (T#) resource descriptor
- [Data Format](dataformat.md) — `GnmDataFormat` for texture format configuration
