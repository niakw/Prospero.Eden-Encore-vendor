# Strings

API reference for the GNM string conversion functions. These inline helpers convert GNM enum values to human-readable C strings for debugging and logging.

Header: `gnm_strings.h`

All functions are `static inline` and return `const char*`. Each falls back to an `"Unknown ..."` string for unrecognized values.

---

## Surface Addressing

### gnmStrPipeConfig

Converts a `GnmPipeConfig` to a string.

```c
static inline const char* gnmStrPipeConfig(GnmPipeConfig cfg);
```

| Enum Value | String |
|------------|--------|
| `GNM_ADDR_SURF_P8_32x32_8x16` | `"P8_32x32_8x16"` |
| `GNM_ADDR_SURF_P8_32x32_16x16` | `"P8_32x32_16x16"` |
| *(default)* | `"Unknown config"` |

---

### gnmStrSampleSplit

Converts a `GnmSampleSplit` to a string.

```c
static inline const char* gnmStrSampleSplit(GnmSampleSplit split);
```

| Enum Value | String |
|------------|--------|
| `GNM_ADDR_SAMPLE_SPLIT_1` | `"1"` |
| `GNM_ADDR_SAMPLE_SPLIT_2` | `"2"` |
| `GNM_ADDR_SAMPLE_SPLIT_4` | `"4"` |
| `GNM_ADDR_SAMPLE_SPLIT_8` | `"8"` |
| *(default)* | `"Unknown split"` |

---

### gnmStrTileSplit

Converts a `GnmTileSplit` to a string.

```c
static inline const char* gnmStrTileSplit(GnmTileSplit split);
```

| Enum Value | String |
|------------|--------|
| `GNM_SURF_TILE_SPLIT_64B` | `"64B"` |
| `GNM_SURF_TILE_SPLIT_128B` | `"128B"` |
| `GNM_SURF_TILE_SPLIT_256B` | `"256B"` |
| `GNM_SURF_TILE_SPLIT_512B` | `"512B"` |
| `GNM_SURF_TILE_SPLIT_1KB` | `"1KB"` |
| `GNM_SURF_TILE_SPLIT_2KB` | `"2KB"` |
| `GNM_SURF_TILE_SPLIT_4KB` | `"4KB"` |
| *(default)* | `"Unknown split"` |

---

## Surface Formats

### gnmStrSurfaceFormat

Converts a `GnmImageFormat` to a string.

```c
static inline const char* gnmStrSurfaceFormat(GnmImageFormat fmt);
```

| Enum Value | String |
|------------|--------|
| `GNM_IMG_DATA_FORMAT_INVALID` | `"Invalid"` |
| `GNM_IMG_DATA_FORMAT_8` | `"8"` |
| `GNM_IMG_DATA_FORMAT_16` | `"16"` |
| `GNM_IMG_DATA_FORMAT_8_8` | `"8_8"` |
| `GNM_IMG_DATA_FORMAT_32` | `"32"` |
| `GNM_IMG_DATA_FORMAT_16_16` | `"16_16"` |
| `GNM_IMG_DATA_FORMAT_10_11_11` | `"10_11_11"` |
| `GNM_IMG_DATA_FORMAT_11_11_10` | `"11_11_10"` |
| `GNM_IMG_DATA_FORMAT_10_10_10_2` | `"10_10_10_2"` |
| `GNM_IMG_DATA_FORMAT_2_10_10_10` | `"2_10_10_10"` |
| `GNM_IMG_DATA_FORMAT_8_8_8_8` | `"8_8_8_8"` |
| `GNM_IMG_DATA_FORMAT_32_32` | `"32_32"` |
| `GNM_IMG_DATA_FORMAT_16_16_16_16` | `"16_16_16_16"` |
| `GNM_IMG_DATA_FORMAT_32_32_32` | `"32_32_32"` |
| `GNM_IMG_DATA_FORMAT_32_32_32_32` | `"32_32_32_32"` |
| `GNM_IMG_DATA_FORMAT_5_6_5` | `"5_6_5"` |
| `GNM_IMG_DATA_FORMAT_1_5_5_5` | `"1_5_5_5"` |
| `GNM_IMG_DATA_FORMAT_5_5_5_1` | `"5_5_5_1"` |
| `GNM_IMG_DATA_FORMAT_4_4_4_4` | `"4_4_4_4"` |
| `GNM_IMG_DATA_FORMAT_8_24` | `"8_24"` |
| `GNM_IMG_DATA_FORMAT_24_8` | `"24_8"` |
| `GNM_IMG_DATA_FORMAT_X24_8_32` | `"X24_8_32"` |
| `GNM_IMG_DATA_FORMAT_GB_GR` | `"GB_GR"` |
| `GNM_IMG_DATA_FORMAT_BG_RG` | `"BG_RG"` |
| `GNM_IMG_DATA_FORMAT_5_9_9_9` | `"5_9_9_9"` |
| `GNM_IMG_DATA_FORMAT_BC1` | `"BC1"` |
| `GNM_IMG_DATA_FORMAT_BC2` | `"BC2"` |
| `GNM_IMG_DATA_FORMAT_BC3` | `"BC3"` |
| `GNM_IMG_DATA_FORMAT_BC4` | `"BC4"` |
| `GNM_IMG_DATA_FORMAT_BC5` | `"BC5"` |
| `GNM_IMG_DATA_FORMAT_BC6` | `"BC6"` |
| `GNM_IMG_DATA_FORMAT_BC7` | `"BC7"` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S2_F1` | `"FMASK8_S2_F1"` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S4_F1` | `"FMASK8_S4_F1"` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S8_F1` | `"FMASK8_S8_F1"` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S2_F2` | `"FMASK8_S2_F2"` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S4_F2` | `"FMASK8_S4_F2"` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S4_F4` | `"FMASK8_S4_F4"` |
| `GNM_IMG_DATA_FORMAT_FMASK16_S16_F1` | `"FMASK16_S16_F1"` |
| `GNM_IMG_DATA_FORMAT_FMASK16_S8_F2` | `"FMASK16_S8_F2"` |
| `GNM_IMG_DATA_FORMAT_FMASK32_S16_F2` | `"FMASK32_S16_F2"` |
| `GNM_IMG_DATA_FORMAT_FMASK32_S8_F4` | `"FMASK32_S8_F4"` |
| `GNM_IMG_DATA_FORMAT_FMASK32_S8_F8` | `"FMASK32_S8_F8"` |
| `GNM_IMG_DATA_FORMAT_FMASK64_S16_F4` | `"FMASK64_S16_F4"` |
| `GNM_IMG_DATA_FORMAT_FMASK64_S16_F8` | `"FMASK64_S16_F8"` |
| `GNM_IMG_DATA_FORMAT_4_4` | `"4_4"` |
| `GNM_IMG_DATA_FORMAT_6_5_5` | `"6_5_5"` |
| `GNM_IMG_DATA_FORMAT_1` | `"1"` |
| `GNM_IMG_DATA_FORMAT_1_REVERSED` | `"1_REVERSED"` |
| *(default)* | `"Unknown format"` |

---

## Texture Types

### gnmStrTextureType

Converts a `GnmTextureType` to a string.

```c
static inline const char* gnmStrTextureType(GnmTextureType type);
```

| Enum Value | String |
|------------|--------|
| `GNM_TEXTURE_1D` | `"1D"` |
| `GNM_TEXTURE_2D` | `"2D"` |
| `GNM_TEXTURE_3D` | `"3D"` |
| `GNM_TEXTURE_CUBEMAP` | `"Cubemap"` |
| `GNM_TEXTURE_1D_ARRAY` | `"1D Array"` |
| `GNM_TEXTURE_2D_ARRAY` | `"2D_Array"` |
| `GNM_TEXTURE_2D_MSAA` | `"2D_MSAA"` |
| `GNM_TEXTURE_2D_ARRAY_MSAA` | `"2D Array MSAA"` |
| *(default)* | `"Unknown type"` |

---

## Channel Types

### gnmStrTexChannelType

Converts a `GnmImgNumFormat` (numeric format / channel type) to a string.

```c
static inline const char* gnmStrTexChannelType(GnmImgNumFormat type);
```

| Enum Value | String |
|------------|--------|
| `GNM_IMG_NUM_FORMAT_UNORM` | `"UNORM"` |
| `GNM_IMG_NUM_FORMAT_SNORM` | `"SNORM"` |
| `GNM_IMG_NUM_FORMAT_USCALED` | `"USCALED"` |
| `GNM_IMG_NUM_FORMAT_SSCALED` | `"SSCALED"` |
| `GNM_IMG_NUM_FORMAT_UINT` | `"UINT"` |
| `GNM_IMG_NUM_FORMAT_SINT` | `"SINT"` |
| `GNM_IMG_NUM_FORMAT_SNORM_OGL` | `"SNORM_OGL"` |
| `GNM_IMG_NUM_FORMAT_FLOAT` | `"FLOAT"` |
| `GNM_IMG_NUM_FORMAT_SRGB` | `"SRGB"` |
| `GNM_IMG_NUM_FORMAT_UBNORM` | `"UBNORM"` |
| `GNM_IMG_NUM_FORMAT_UBNORM_OGL` | `"UBNORM_OGL"` |
| `GNM_IMG_NUM_FORMAT_UBINT` | `"UBINT"` |
| `GNM_IMG_NUM_FORMAT_UBSCALED` | `"UBSCALED"` |
| *(default)* | `"Unknown type"` |

---

### gnmStrTexChannel

Converts a `GnmChannel` to a string.

```c
static inline const char* gnmStrTexChannel(GnmChannel chan);
```

| Enum Value | String |
|------------|--------|
| `GNM_CHAN_CONSTANT0` | `"Constant 0"` |
| `GNM_CHAN_CONSTANT1` | `"Constant 1"` |
| `GNM_CHAN_X` | `"X"` |
| `GNM_CHAN_Y` | `"Y"` |
| `GNM_CHAN_Z` | `"Z"` |
| `GNM_CHAN_W` | `"W"` |
| *(default)* | `"Unknown channel"` |

---

## DCC Color Transform

### gnmStrDccColorTransform

Converts a `GnmDccColorTransform` to a string.

```c
static inline const char* gnmStrDccColorTransform(GnmDccColorTransform transform);
```

| Enum Value | String |
|------------|--------|
| `GNM_DCC_CT_AUTO` | `"Auto"` |
| `GNM_DCC_CT_NONE` | `"None"` |
| `GNM_DCC_CT_ABGR` | `"ABGR"` |
| `GNM_DCC_CT_BGRA` | `"BGRA"` |
| *(default)* | `"Unknown transform"` |

---

## Tiling Modes

### gnmStrArrayMode

Converts a `GnmArrayMode` to a string.

```c
static inline const char* gnmStrArrayMode(GnmArrayMode mode);
```

| Enum Value | String |
|------------|--------|
| `GNM_ARRAY_LINEAR_GENERAL` | `"LINEAR_GENERAL"` |
| `GNM_ARRAY_LINEAR_ALIGNED` | `"LINEAR_ALIGNED"` |
| `GNM_ARRAY_1D_TILED_THIN1` | `"1D_TILED_THIN1"` |
| `GNM_ARRAY_1D_TILED_THICK` | `"1D_TILED_THICK"` |
| `GNM_ARRAY_2D_TILED_THIN1` | `"2D_TILED_THIN1"` |
| `GNM_ARRAY_PRT_TILED_THIN1` | `"PRT_TILED_THIN1"` |
| `GNM_ARRAY_PRT_2D_TILED_THIN1` | `"PRT_2D_TILED_THIN1"` |
| `GNM_ARRAY_2D_TILED_THICK` | `"2D_TILED_THICK"` |
| `GNM_ARRAY_2D_TILED_XTHICK` | `"2D_TILED_X_THICK"` |
| `GNM_ARRAY_PRT_TILED_THICK` | `"PRT_TILED_THICK"` |
| `GNM_ARRAY_PRT_2D_TILED_THICK` | `"PRT_2D_TILED_THICK"` |
| `GNM_ARRAY_PRT_3D_TILED_THIN1` | `"PRT_3D_TILED_THIN1"` |
| `GNM_ARRAY_3D_TILED_THIN1` | `"3D_TILED_THIN1"` |
| `GNM_ARRAY_3D_TILED_THICK` | `"3D_TILED_THICK"` |
| `GNM_ARRAY_3D_TILED_XTHICK` | `"3D_TILED_XTHICK"` |
| `GNM_ARRAY_PRT_3D_TILED_THICK` | `"PRT_3D_TILED_THICK"` |
| *(default)* | `"Unknown mode"` |

---

### gnmStrTileMode

Converts a `GnmTileMode` to a string.

```c
static inline const char* gnmStrTileMode(GnmTileMode mode);
```

| Enum Value | String |
|------------|--------|
| `GNM_TM_DEPTH_2D_THIN_64` | `"DEPTH_2D_THIN_64"` |
| `GNM_TM_DEPTH_2D_THIN_128` | `"DEPTH_2D_THIN_128"` |
| `GNM_TM_DEPTH_2D_THIN_256` | `"DEPTH_2D_THIN_256"` |
| `GNM_TM_DEPTH_2D_THIN_512` | `"DEPTH_2D_THIN_512"` |
| `GNM_TM_DEPTH_2D_THIN_1K` | `"DEPTH_2D_THIN_1K"` |
| `GNM_TM_DEPTH_1D_THIN` | `"DEPTH_1D_THIN"` |
| `GNM_TM_DEPTH_2D_THIN_PRT_256` | `"DEPTH_2D_THIN_PRT_256"` |
| `GNM_TM_DEPTH_2D_THIN_PRT_1K` | `"DEPTH_2D_THIN_PRT_1K"` |
| `GNM_TM_DISPLAY_LINEAR_ALIGNED` | `"DISPLAY_LINEAR_ALIGNED"` |
| `GNM_TM_DISPLAY_1D_THIN` | `"DISPLAY_1D_THIN"` |
| `GNM_TM_DISPLAY_2D_THIN` | `"DISPLAY_2D_THIN"` |
| `GNM_TM_DISPLAY_THIN_PRT` | `"DISPLAY_THIN_PRT"` |
| `GNM_TM_DISPLAY_2D_THIN_PRT` | `"DISPLAY_2D_THIN_PRT"` |
| `GNM_TM_THIN_1D_THIN` | `"THIN_1D_THIN"` |
| `GNM_TM_THIN_2D_THIN` | `"THIN_2D_THIN"` |
| `GNM_TM_THIN_3D_THIN` | `"THIN_3D_THIN"` |
| `GNM_TM_THIN_THIN_PRT` | `"THIN_THIN_PRT"` |
| `GNM_TM_THIN_2D_THIN_PRT` | `"THIN_2D_THIN_PRT"` |
| `GNM_TM_THIN_3D_THIN_PRT` | `"THIN_3D_THIN_PRT"` |
| `GNM_TM_THICK_1D_THICK` | `"THICK_1D_THICK"` |
| `GNM_TM_THICK_2D_THICK` | `"THICK_2D_THICK"` |
| `GNM_TM_THICK_3D_THICK` | `"THICK_3D_THICK"` |
| `GNM_TM_THICK_THICK_PRT` | `"THICK_THICK_PRT"` |
| `GNM_TM_THICK_2D_THICK_PRT` | `"THICK_2D_THICK_PRT"` |
| `GNM_TM_THICK_3D_THICK_PRT` | `"THICK_3D_THICK_PRT"` |
| `GNM_TM_THICK_2D_XTHICK` | `"THICK_2D_XTHICK"` |
| `GNM_TM_THICK_3D_XTHICK` | `"THICK_3D_XTHICK"` |
| `GNM_TM_DISPLAY_LINEAR_GENERAL` | `"DISPLAY_LINEAR_GENERAL"` |
| *(default)* | `"Unknown mode"` |

---

### gnmStrMicroTileMode

Converts a `GnmMicroTileMode` to a string.

```c
static inline const char* gnmStrMicroTileMode(GnmMicroTileMode mtm);
```

| Enum Value | String |
|------------|--------|
| `GNM_SURF_DISPLAY_MICRO_TILING` | `"DISPLAY"` |
| `GNM_SURF_THIN_MICRO_TILING` | `"THIN"` |
| `GNM_SURF_DEPTH_MICRO_TILING` | `"DEPTH"` |
| `GNM_SURF_ROTATED_MICRO_TILING` | `"ROTATED"` |
| `GNM_SURF_THICK_MICRO_TILING` | `"THICK"` |
| *(default)* | `"Unknown mode"` |

---

## Macro Tiling Parameters

### gnmStrBankWidth

Converts a `GnmBankWidth` to a string.

```c
static inline const char* gnmStrBankWidth(GnmBankWidth width);
```

| Enum Value | String |
|------------|--------|
| `GNM_SURF_BANK_WIDTH_1` | `"1"` |
| `GNM_SURF_BANK_WIDTH_2` | `"2"` |
| `GNM_SURF_BANK_WIDTH_4` | `"4"` |
| `GNM_SURF_BANK_WIDTH_8` | `"8"` |
| *(default)* | `"Unknown width"` |

---

### gnmStrBankHeight

Converts a `GnmBankHeight` to a string.

```c
static inline const char* gnmStrBankHeight(GnmBankHeight height);
```

| Enum Value | String |
|------------|--------|
| `GNM_SURF_BANK_HEIGHT_1` | `"1"` |
| `GNM_SURF_BANK_HEIGHT_2` | `"2"` |
| `GNM_SURF_BANK_HEIGHT_4` | `"4"` |
| `GNM_SURF_BANK_HEIGHT_8` | `"8"` |
| *(default)* | `"Unknown height"` |

---

### gnmStrNumBanks

Converts a `GnmNumBanks` to a string.

```c
static inline const char* gnmStrNumBanks(GnmNumBanks numbanks);
```

| Enum Value | String |
|------------|--------|
| `GNM_SURF_2_BANK` | `"GNM_NUMBANKS_2"` |
| `GNM_SURF_4_BANK` | `"GNM_NUMBANKS_4"` |
| `GNM_SURF_8_BANK` | `"GNM_NUMBANKS_8"` |
| `GNM_SURF_16_BANK` | `"GNM_NUMBANKS_16"` |
| *(default)* | `"Unknown number"` |

---

### gnmStrMacroTileAspect

Converts a `GnmMacroTileAspect` to a string.

```c
static inline const char* gnmStrMacroTileAspect(const GnmMacroTileAspect aspect);
```

| Enum Value | String |
|------------|--------|
| `GNM_SURF_MACRO_ASPECT_1` | `"1"` |
| `GNM_SURF_MACRO_ASPECT_2` | `"2"` |
| `GNM_SURF_MACRO_ASPECT_4` | `"4"` |
| `GNM_SURF_MACRO_ASPECT_8` | `"8"` |
| *(default)* | `"Unknown aspect"` |

---

### gnmStrMacroTileMode

Converts a `GnmMacroTileMode` to a string.

```c
static inline const char* gnmStrMacroTileMode(const GnmMacroTileMode mtm);
```

| Enum Value | String |
|------------|--------|
| `GNM_MACROTILEMODE_1x4_16` | `"1x4_16"` |
| `GNM_MACROTILEMODE_1x2_16` | `"1x2_16"` |
| `GNM_MACROTILEMODE_1x1_16` | `"1x1_16"` |
| `GNM_MACROTILEMODE_1x1_16_DUP` | `"1x1_16_DUP"` |
| `GNM_MACROTILEMODE_1x1_8` | `"1x1_8"` |
| `GNM_MACROTILEMODE_1x1_4` | `"1x1_4"` |
| `GNM_MACROTILEMODE_1x1_2` | `"1x1_2"` |
| `GNM_MACROTILEMODE_1x1_2_DUP` | `"1x1_2_DUP"` |
| `GNM_MACROTILEMODE_1x8_16` | `"1x8_16"` |
| `GNM_MACROTILEMODE_1x4_16_DUP` | `"1x4_16_DUP"` |
| `GNM_MACROTILEMODE_1x2_16_DUP` | `"1x2_16_DUP"` |
| `GNM_MACROTILEMODE_1x1_16_DUP2` | `"1x1_16_DUP2"` |
| `GNM_MACROTILEMODE_1x1_8_DUP` | `"1x1_8_DUP"` |
| `GNM_MACROTILEMODE_1x1_4_DUP` | `"1x1_4_DUP"` |
| `GNM_MACROTILEMODE_1x1_2_DUP2` | `"1x1_2_DUP2"` |
| `GNM_MACROTILEMODE_1x1_2_DUP3` | `"1x1_2_DUP3"` |
| *(default)* | `"Unknown mode"` |

---

## Shader Types

### gnmStrShaderInputUsageType

Converts a `GnmShaderInputUsageType` to a string.

```c
static inline const char* gnmStrShaderInputUsageType(
    GnmShaderInputUsageType type
);
```

| Enum Value | String |
|------------|--------|
| `GNM_SHINPUTUSAGE_IMM_RESOURCE` | `"imm_resource"` |
| `GNM_SHINPUTUSAGE_IMM_SAMPLER` | `"imm_sampler"` |
| `GNM_SHINPUTUSAGE_IMM_CONSTBUFFER` | `"imm_constbuffer"` |
| `GNM_SHINPUTUSAGE_IMM_VERTEXBUFFER` | `"imm_vertexbuffer"` |
| `GNM_SHINPUTUSAGE_IMM_RWRESOURCE` | `"imm_rwresource"` |
| `GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST` | `"imm_alufloatconst"` |
| `GNM_SHINPUTUSAGE_IMM_ALUBOOL32CONST` | `"imm_alubool32const"` |
| `GNM_SHINPUTUSAGE_IMM_GDSCOUNTERRANGE` | `"imm_gdscounterrange"` |
| `GNM_SHINPUTUSAGE_IMM_GDSMEMORYRANGE` | `"imm_gdsmemoryrange"` |
| `GNM_SHINPUTUSAGE_IMM_GWSBASE` | `"imm_gwsbase"` |
| `GNM_SHINPUTUSAGE_IMM_SRT` | `"imm_srt"` |
| `GNM_SHINPUTUSAGE_IMM_LDSESGSSIZE` | `"imm_ldsesgssize"` |
| `GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER` | `"subptr_fetchshader"` |
| `GNM_SHINPUTUSAGE_PTR_RESOURCETABLE` | `"ptr_resourcetable"` |
| `GNM_SHINPUTUSAGE_PTR_INTERNALRESOURCETABLE` | `"ptr_internalresourcetable"` |
| `GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE` | `"ptr_samplertable"` |
| `GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE` | `"ptr_constbuffertable"` |
| `GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE` | `"ptr_vertexbuffertable"` |
| `GNM_SHINPUTUSAGE_PTR_SOBUFFERTABLE` | `"ptr_sobuffertable"` |
| `GNM_SHINPUTUSAGE_PTR_RWRESOURCETABLE` | `"ptr_rwresourcetable"` |
| `GNM_SHINPUTUSAGE_PTR_INTERNALGLOBALTABLE` | `"ptr_internalglobaltable"` |
| `GNM_SHINPUTUSAGE_PTR_EXTENDEDUSERDATA` | `"ptr_extendeduserdata"` |
| `GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE` | `"ptr_indirectresourcetable"` |
| `GNM_SHINPUTUSAGE_PTR_INDIRECTINTERNALRESOURCETABLE` | `"ptr_indirectinternalresourcetable"` |
| `GNM_SHINPUTUSAGE_PTR_INDIRECTRWRESOURCETABLE` | `"ptr_indirectrwresourcetable"` |
| *(default)* | `"Unknown type"` |

---

### gnmStrShaderType

Converts a `GnmShaderType` to a string.

```c
static inline const char* gnmStrShaderType(GnmShaderType type);
```

| Enum Value | String |
|------------|--------|
| `GNM_SHADER_INVALID` | `"Invalid shader"` |
| `GNM_SHADER_VERTEX` | `"Vertex shader"` |
| `GNM_SHADER_PIXEL` | `"Pixel shader"` |
| `GNM_SHADER_GEOMETRY` | `"Geometry shader"` |
| `GNM_SHADER_COMPUTE` | `"Compute shader"` |
| `GNM_SHADER_EXPORT` | `"Export shader"` |
| `GNM_SHADER_LOCAL` | `"Local shader"` |
| `GNM_SHADER_HULL` | `"Hull shader"` |
| *(default)* | `"Unknown shader"` |

---

### gnmStrShaderBinaryType

Converts a `GnmShaderBinaryType` to a string.

```c
static inline const char* gnmStrShaderBinaryType(GnmShaderBinaryType type);
```

| Enum Value | String |
|------------|--------|
| `GNM_SHB_PS` | `"Pixel Shader"` |
| `GNM_SHB_VS_VS` | `"Vertex Shader (VS)"` |
| `GNM_SHB_VS_ES` | `"Vertex Shader (ES)"` |
| `GNM_SHB_VS_LS` | `"Vertex Shader (LS)"` |
| `GNM_SHB_CS` | `"Compute Shader"` |
| `GNM_SHB_GS` | `"Geometry Shader"` |
| `GNM_SHB_GS_VS` | `"Geometry Shader (VS)"` |
| `GNM_SHB_HS` | `"Hull Shader"` |
| `GNM_SHB_DS_VS` | `"Domain Shader (VS)"` |
| `GNM_SHB_DS_ES` | `"Domain Shader (ES)"` |
| *(default)* | `"Unknown shader"` |

---

### gnmStrShaderStage

Converts a `GnmShaderStage` to a string.

```c
static inline const char* gnmStrShaderStage(GnmShaderStage stage);
```

| Enum Value | String |
|------------|--------|
| `GNM_STAGE_CS` | `"Compute"` |
| `GNM_STAGE_PS` | `"Pixel"` |
| `GNM_STAGE_VS` | `"Vertex"` |
| `GNM_STAGE_GS` | `"Geometry"` |
| `GNM_STAGE_ES` | `"Export"` |
| `GNM_STAGE_HS` | `"Hull"` |
| `GNM_STAGE_LS` | `"Local"` |
| *(default)* | `"Unknown"` |

---

## Pixel Defaults

### gnmStrPixelDefaultValue

Converts a `GnmPixelDefaultValue` to a string.

```c
static inline const char* gnmStrPixelDefaultValue(GnmPixelDefaultValue type);
```

| Enum Value | String |
|------------|--------|
| `GNM_PX_DEFVAL_NONE` | `"None"` |
| `GNM_PX_DEFVAL_0_0_0_1` | `"{0,0,0,1}"` |
| `GNM_PX_DEFVAL_1_1_1_0` | `"{1,1,1,0}"` |
| `GNM_PX_DEFVAL_1_1_1_1` | `"{1,1,1,1}"` |
| *(default)* | `"Unknown value"` |

---

## GPU Mode

### gnmStrTargetGpuMode

Converts a `GnmTargetGpuMode` to a string. This function also handles the combined `GNM_TARGETGPUMODE_BASE | GNM_TARGETGPUMODE_NEO` case.

```c
static inline const char* gnmStrTargetGpuMode(GnmTargetGpuMode mode);
```

| Enum Value | String |
|------------|--------|
| `GNM_TARGETGPUMODE_BASE & GNM_TARGETGPUMODE_NEO` (both) | `"Base and NEO modes"` |
| `GNM_TARGETGPUMODE_UNSPECIFIED` | `"Unspecified"` |
| `GNM_TARGETGPUMODE_BASE` | `"Base mode"` |
| `GNM_TARGETGPUMODE_NEO` | `"NEO mode"` |
| *(default)* | `"Unknown mode"` |

---

## See Also

- [Command Buffer](commandbuffer.md)
- [Draw Command Buffer](drawcommandbuffer.md)
- [Platform](platform.md)
- [Compatibility Layer](compat.md)
