# Shader

API reference for shader stage registers, input/output semantics, and fetch shader creation in the opengnm PS4 GNM library.

Header: `gnm_shader.h`

---

## GnmShaderInputUsageType

Enumerates the types of shader input usage slots, describing how user data registers are consumed by the shader.

```c
typedef enum {
    GNM_SHINPUTUSAGE_IMM_RESOURCE = 0x0,
    GNM_SHINPUTUSAGE_IMM_SAMPLER = 0x1,
    GNM_SHINPUTUSAGE_IMM_CONSTBUFFER = 0x2,
    GNM_SHINPUTUSAGE_IMM_VERTEXBUFFER = 0x3,
    GNM_SHINPUTUSAGE_IMM_RWRESOURCE = 0x4,
    GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST = 0x5,
    GNM_SHINPUTUSAGE_IMM_ALUBOOL32CONST = 0x6,
    GNM_SHINPUTUSAGE_IMM_GDSCOUNTERRANGE = 0x7,
    GNM_SHINPUTUSAGE_IMM_GDSMEMORYRANGE = 0x8,
    GNM_SHINPUTUSAGE_IMM_GWSBASE = 0x9,
    GNM_SHINPUTUSAGE_IMM_SRT = 0xa,
    GNM_SHINPUTUSAGE_IMM_LDSESGSSIZE = 0xd,
    GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER = 0x12,
    GNM_SHINPUTUSAGE_PTR_RESOURCETABLE = 0x13,
    GNM_SHINPUTUSAGE_PTR_INTERNALRESOURCETABLE = 0x14,
    GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE = 0x15,
    GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE = 0x16,
    GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE = 0x17,
    GNM_SHINPUTUSAGE_PTR_SOBUFFERTABLE = 0x18,
    GNM_SHINPUTUSAGE_PTR_RWRESOURCETABLE = 0x19,
    GNM_SHINPUTUSAGE_PTR_INTERNALGLOBALTABLE = 0x1a,
    GNM_SHINPUTUSAGE_PTR_EXTENDEDUSERDATA = 0x1b,
    GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE = 0x1c,
    GNM_SHINPUTUSAGE_PTR_INDIRECTINTERNALRESOURCETABLE = 0x1d,
    GNM_SHINPUTUSAGE_PTR_INDIRECTRWRESOURCETABLE = 0x1e,
} GnmShaderInputUsageType;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_SHINPUTUSAGE_IMM_RESOURCE` | 0x0 | Immediate resource |
| `GNM_SHINPUTUSAGE_IMM_SAMPLER` | 0x1 | Immediate sampler |
| `GNM_SHINPUTUSAGE_IMM_CONSTBUFFER` | 0x2 | Immediate constant buffer |
| `GNM_SHINPUTUSAGE_IMM_VERTEXBUFFER` | 0x3 | Immediate vertex buffer |
| `GNM_SHINPUTUSAGE_IMM_RWRESOURCE` | 0x4 | Immediate read-write resource |
| `GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST` | 0x5 | Immediate ALU float constant |
| `GNM_SHINPUTUSAGE_IMM_ALUBOOL32CONST` | 0x6 | Immediate ALU bool32 constant |
| `GNM_SHINPUTUSAGE_IMM_GDSCOUNTERRANGE` | 0x7 | Immediate GDS counter range |
| `GNM_SHINPUTUSAGE_IMM_GDSMEMORYRANGE` | 0x8 | Immediate GDS memory range |
| `GNM_SHINPUTUSAGE_IMM_GWSBASE` | 0x9 | Immediate GWS base |
| `GNM_SHINPUTUSAGE_IMM_SRT` | 0xa | Immediate SRT |
| `GNM_SHINPUTUSAGE_IMM_LDSESGSSIZE` | 0xd | Immediate LDS/ES/GS size |
| `GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER` | 0x12 | Sub-pointer to fetch shader |
| `GNM_SHINPUTUSAGE_PTR_RESOURCETABLE` | 0x13 | Pointer to resource table |
| `GNM_SHINPUTUSAGE_PTR_INTERNALRESOURCETABLE` | 0x14 | Pointer to internal resource table |
| `GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE` | 0x15 | Pointer to sampler table |
| `GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE` | 0x16 | Pointer to constant buffer table |
| `GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE` | 0x17 | Pointer to vertex buffer table |
| `GNM_SHINPUTUSAGE_PTR_SOBUFFERTABLE` | 0x18 | Pointer to SO buffer table |
| `GNM_SHINPUTUSAGE_PTR_RWRESOURCETABLE` | 0x19 | Pointer to read-write resource table |
| `GNM_SHINPUTUSAGE_PTR_INTERNALGLOBALTABLE` | 0x1a | Pointer to internal global table |
| `GNM_SHINPUTUSAGE_PTR_EXTENDEDUSERDATA` | 0x1b | Pointer to extended user data |
| `GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE` | 0x1c | Pointer to indirect resource table |
| `GNM_SHINPUTUSAGE_PTR_INDIRECTINTERNALRESOURCETABLE` | 0x1d | Pointer to indirect internal resource table |
| `GNM_SHINPUTUSAGE_PTR_INDIRECTRWRESOURCETABLE` | 0x1e | Pointer to indirect read-write resource table |

---

## GnmInputUsageSlot

Describes a single shader input usage slot (0x4 bytes).

```c
typedef struct {
    uint8_t usagetype;  // GnmShaderInputUsageType
    uint8_t apislot;
    uint8_t startregister;

    union {
        struct {
            uint8_t registercount : 1;
            uint8_t resourcetype : 1;
            uint8_t _unused : 2;
            uint8_t chunkmask : 4;
        };
        uint8_t srtdwordsminusone;
    };
} GnmInputUsageSlot;
_Static_assert(sizeof(GnmInputUsageSlot) == 0x4, "");
```

| Field | Type | Bits | Description |
|-------|------|------|-------------|
| `usagetype` | `uint8_t` | 8 | `GnmShaderInputUsageType` |
| `apislot` | `uint8_t` | 8 | API slot index |
| `startregister` | `uint8_t` | 8 | Starting user data register |
| `registercount` | `uint8_t` | 1 | Register count (bitfield mode) |
| `resourcetype` | `uint8_t` | 1 | Resource type (bitfield mode) |
| `_unused` | `uint8_t` | 2 | Reserved (bitfield mode) |
| `chunkmask` | `uint8_t` | 4 | Chunk mask (bitfield mode) |
| `srtdwordsminusone` | `uint8_t` | 8 | SRT dwords minus one (union mode) |

---

## GnmVertexInputSemantic

Describes a vertex shader input semantic (0x4 bytes).

```c
typedef struct {
    uint8_t semantic;
    uint8_t vgpr;
    uint8_t sizeinelements;
    uint8_t _unused;
} GnmVertexInputSemantic;
_Static_assert(sizeof(GnmVertexInputSemantic) == 0x4, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `semantic` | `uint8_t` | Semantic ID |
| `vgpr` | `uint8_t` | VGPR register index |
| `sizeinelements` | `uint8_t` | Size in elements |
| `_unused` | `uint8_t` | Reserved |

---

## GnmVertexExportSemantic

Describes a vertex shader export semantic (0x2 bytes).

```c
typedef struct {
    uint8_t semantic;
    uint8_t outindex : 5;
    uint8_t _unused : 1;
    uint8_t exportf16 : 2;
} GnmVertexExportSemantic;
_Static_assert(sizeof(GnmVertexExportSemantic) == 0x2, "");
```

| Field | Type | Bits | Description |
|-------|------|------|-------------|
| `semantic` | `uint8_t` | 8 | Semantic ID |
| `outindex` | `uint8_t` | 5 | Output index |
| `_unused` | `uint8_t` | 1 | Reserved |
| `exportf16` | `uint8_t` | 2 | Export as f16 flag |

---

## GnmPixelDefaultValue

Enumerates default values for pixel shader inputs when not provided.

```c
typedef enum {
    GNM_PX_DEFVAL_NONE = 0,
    GNM_PX_DEFVAL_0_0_0_1 = 1,
    GNM_PX_DEFVAL_1_1_1_0 = 2,
    GNM_PX_DEFVAL_1_1_1_1 = 3,
} GnmPixelDefaultValue;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_PX_DEFVAL_NONE` | 0 | No default value |
| `GNM_PX_DEFVAL_0_0_0_1` | 1 | Default (0, 0, 0, 1) |
| `GNM_PX_DEFVAL_1_1_1_0` | 2 | Default (1, 1, 1, 0) |
| `GNM_PX_DEFVAL_1_1_1_1` | 3 | Default (1, 1, 1, 1) |

---

## GnmPixelInputSemantic

Describes a pixel shader input semantic (0x2 bytes). Has a NEO-mode variant for f16 interpolation.

```c
typedef union {
    struct {
        uint16_t semantic : 8;
        uint16_t defaultvalue : 2;  // GnmPixelDefaultValue
        uint16_t isflatshaded : 1;
        uint16_t islinear : 1;
        uint16_t iscustom : 1;
        uint16_t _unused : 3;
    };
    // NEO mode only
    struct {
        uint16_t : 12;
        uint16_t defaultvaluehi : 2;  // GnmPixelDefaultValue
        // bitmask for each enabled f16 interp
        uint16_t interpf16 : 2;
    };
} GnmPixelInputSemantic;
_Static_assert(sizeof(GnmPixelInputSemantic) == 0x2, "");
```

### Base Mode Fields

| Field | Bits | Description |
|-------|------|-------------|
| `semantic` | 8 | Semantic ID |
| `defaultvalue` | 2 | `GnmPixelDefaultValue` |
| `isflatshaded` | 1 | Flat-shaded input |
| `islinear` | 1 | Linear interpolated input |
| `iscustom` | 1 | Custom interpolated input |
| `_unused` | 3 | Reserved |

### NEO Mode Fields

| Field | Bits | Description |
|-------|------|-------------|
| `defaultvaluehi` | 2 | `GnmPixelDefaultValue` (high bits, NEO) |
| `interpf16` | 2 | Bitmask for enabled f16 interpolants (NEO only) |

---

## GnmPsExportFormat

Enumerates pixel shader export formats.

```c
typedef enum {
    GNM_PS_EXP_FMT_ZERO = 0x0,
    GNM_PS_EXP_FMT_32R = 0x1,
    GNM_PS_EXP_FMT_32GR = 0x2,
    GNM_PS_EXP_FMT_32AR = 0x3,
    GNM_PS_EXP_FMT_FP16_ABGR = 0x4,
    GNM_PS_EXP_FMT_UNORM16_ABGR = 0x5,
    GNM_PS_EXP_FMT_SNORM16_ABGR = 0x6,
    GNM_PS_EXP_FMT_UINT16_ABGR = 0x7,
    GNM_PS_EXP_FMT_SINT16_ABGR = 0x8,
    GNM_PS_EXP_FMT_32_ABGR = 0x9,
    GNM_PS_EXP_FMT_FP16_OR_32_OPT = 0x84,
    GNM_PS_EXP_FMT_32_OPT = 0x89,
} GnmPsExportFormat;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_PS_EXP_FMT_ZERO` | 0x0 | No export (zero) |
| `GNM_PS_EXP_FMT_32R` | 0x1 | 32-bit R |
| `GNM_PS_EXP_FMT_32GR` | 0x2 | 32-bit GR |
| `GNM_PS_EXP_FMT_32AR` | 0x3 | 32-bit AR |
| `GNM_PS_EXP_FMT_FP16_ABGR` | 0x4 | FP16 ABGR |
| `GNM_PS_EXP_FMT_UNORM16_ABGR` | 0x5 | UNorm16 ABGR |
| `GNM_PS_EXP_FMT_SNORM16_ABGR` | 0x6 | SNorm16 ABGR |
| `GNM_PS_EXP_FMT_UINT16_ABGR` | 0x7 | UInt16 ABGR |
| `GNM_PS_EXP_FMT_SINT16_ABGR` | 0x8 | SInt16 ABGR |
| `GNM_PS_EXP_FMT_32_ABGR` | 0x9 | 32-bit ABGR |
| `GNM_PS_EXP_FMT_FP16_OR_32_OPT` | 0x84 | FP16 or 32 (optional) |
| `GNM_PS_EXP_FMT_32_OPT` | 0x89 | 32-bit (optional) |

---

## GnmFetchShaderFlags

Flags for fetch shader creation.

```c
typedef enum {
    GNM_FETCH_FLAG_NONE = 0x0,
    GNM_FETCH_FLAG_SHIFT_OVERSTEP0 = 0x1,
} GnmFetchShaderFlags;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_FETCH_FLAG_NONE` | 0x0 | No flags |
| `GNM_FETCH_FLAG_SHIFT_OVERSTEP0` | 0x1 | Shift overstep rate 0 |

---

## GnmFetchShaderInstancingMode

Instancing modes for fetch shader vertex fetching.

```c
typedef enum {
    GNM_FETCH_MODE_VERTEXINDEX = 0,
    GNM_FETCH_MODE_INSTANCEID = 1,
    GNM_FETCH_MODE_INSTANCEID_OVERSTEPRATE0 = 2,
    GNM_FETCH_MODE_INSTANCEID_OVERSTEPRATE1 = 3,
} GnmFetchShaderInstancingMode;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_FETCH_MODE_VERTEXINDEX` | 0 | Fetch by vertex index |
| `GNM_FETCH_MODE_INSTANCEID` | 1 | Fetch by instance ID |
| `GNM_FETCH_MODE_INSTANCEID_OVERSTEPRATE0` | 2 | Fetch by instance ID, overstep rate 0 |
| `GNM_FETCH_MODE_INSTANCEID_OVERSTEPRATE1` | 3 | Fetch by instance ID, overstep rate 1 |

---

## Stage Register Structs

### GnmVsStageRegisters

Vertex shader stage registers (0x1C bytes).

```c
typedef struct {
    uint32_t spishaderpgmlovs;
    uint32_t spishaderpgmhivs;

    uint32_t spishaderpgmrsrc1vs;
    uint32_t spishaderpgmrsrc2vs;

    uint32_t spivsoutconfig;
    uint32_t spishaderposformat;
    uint32_t paclvsoutcntl;
} GnmVsStageRegisters;
_Static_assert(sizeof(GnmVsStageRegisters) == 0x1c, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `spishaderpgmlovs` | `uint32_t` | Shader program low address (VS) |
| `spishaderpgmhivs` | `uint32_t` | Shader program high address (VS) |
| `spishaderpgmrsrc1vs` | `uint32_t` | Shader resource 1 (VS) |
| `spishaderpgmrsrc2vs` | `uint32_t` | Shader resource 2 (VS) |
| `spivsoutconfig` | `uint32_t` | VS output configuration |
| `spishaderposformat` | `uint32_t` | Position export format |
| `paclvsoutcntl` | `uint32_t` | VS output control |

### GnmPsStageRegisters

Pixel shader stage registers (0x30 bytes).

```c
typedef struct {
    uint32_t spishaderpgmlops;
    uint32_t spishaderpgmhips;

    uint32_t spishaderpgmrsrc1ps;
    uint32_t spishaderpgmrsrc2ps;

    uint32_t spishaderzformat;
    uint32_t spishadercolformat;  // GnmPsExportFormat

    uint32_t spipsinputena;
    uint32_t spipsinputaddr;

    uint32_t spipsincontrol;
    uint32_t spibaryccntl;

    uint32_t dbshadercontrol;
    uint32_t cbshadermask;
} GnmPsStageRegisters;
_Static_assert(sizeof(GnmPsStageRegisters) == 0x30, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `spishaderpgmlops` | `uint32_t` | Shader program low address (PS) |
| `spishaderpgmhips` | `uint32_t` | Shader program high address (PS) |
| `spishaderpgmrsrc1ps` | `uint32_t` | Shader resource 1 (PS) |
| `spishaderpgmrsrc2ps` | `uint32_t` | Shader resource 2 (PS) |
| `spishaderzformat` | `uint32_t` | Z format |
| `spishadercolformat` | `uint32_t` | `GnmPsExportFormat` — color export format |
| `spipsinputena` | `uint32_t` | PS input enable |
| `spipsinputaddr` | `uint32_t` | PS input address |
| `spipsincontrol` | `uint32_t` | PS input control |
| `spibaryccntl` | `uint32_t` | Barycentric control |
| `dbshadercontrol` | `uint32_t` | DB shader control |
| `cbshadermask` | `uint32_t` | CB shader mask |

### GnmCsStageRegisters

Compute shader stage registers (0x1C bytes).

```c
typedef struct {
    uint32_t computepgmlo;
    uint32_t computepgmhi;

    uint32_t computepgmrsrc1;
    uint32_t computepgmrsrc2;

    uint32_t computenumthreadx;
    uint32_t computenumthready;
    uint32_t computenumthreadz;
} GnmCsStageRegisters;
_Static_assert(sizeof(GnmCsStageRegisters) == 0x1c, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `computepgmlo` | `uint32_t` | Compute program low address |
| `computepgmhi` | `uint32_t` | Compute program high address |
| `computepgmrsrc1` | `uint32_t` | Compute resource 1 |
| `computepgmrsrc2` | `uint32_t` | Compute resource 2 |
| `computenumthreadx` | `uint32_t` | Number of threads in X dimension |
| `computenumthready` | `uint32_t` | Number of threads in Y dimension |
| `computenumthreadz` | `uint32_t` | Number of threads in Z dimension |

### GnmGsStageRegisters

Geometry shader stage registers (0x1C bytes).

```c
typedef struct {
    uint32_t spishaderpgmlogs;
    uint32_t spishaderpgmhigs;

    uint32_t spishaderpgmrsrc1gs;
    uint32_t spishaderpgmrsrc2gs;

    uint32_t vgtstrmoutconfig;
    uint32_t vgtgsoutprimtype;
    uint32_t vgtgsinstancecnt;
} GnmGsStageRegisters;
_Static_assert(sizeof(GnmGsStageRegisters) == 0x1c, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `spishaderpgmlogs` | `uint32_t` | Shader program low address (GS) |
| `spishaderpgmhigs` | `uint32_t` | Shader program high address (GS) |
| `spishaderpgmrsrc1gs` | `uint32_t` | Shader resource 1 (GS) |
| `spishaderpgmrsrc2gs` | `uint32_t` | Shader resource 2 (GS) |
| `vgtstrmoutconfig` | `uint32_t` | Stream-out configuration |
| `vgtgsoutprimtype` | `uint32_t` | GS output primitive type |
| `vgtgsinstancecnt` | `uint32_t` | GS instance count |

### GnmEsStageRegisters

Export shader (hull-shader predecessor) stage registers (0x10 bytes).

```c
typedef struct {
    uint32_t spishaderpgmloes;
    uint32_t spishaderpgmhies;

    uint32_t spishaderpgmrsrc1es;
    uint32_t spishaderpgmrsrc2es;
} GnmEsStageRegisters;
_Static_assert(sizeof(GnmEsStageRegisters) == 0x10, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `spishaderpgmloes` | `uint32_t` | Shader program low address (ES) |
| `spishaderpgmhies` | `uint32_t` | Shader program high address (ES) |
| `spishaderpgmrsrc1es` | `uint32_t` | Shader resource 1 (ES) |
| `spishaderpgmrsrc2es` | `uint32_t` | Shader resource 2 (ES) |

### GnmHsStageRegisters

Hull shader stage registers (0x1C bytes).

```c
typedef struct {
    uint32_t spishaderpgmlohs;
    uint32_t spishaderpgmhihs;

    uint32_t spishaderpgmrsrc1hs;
    uint32_t spishaderpgmrsrc2hs;

    uint32_t vgttfparam;
    uint32_t vgthosmaxtesslevel;
    uint32_t vgthosmintesslevel;
} GnmHsStageRegisters;
_Static_assert(sizeof(GnmHsStageRegisters) == 0x1c, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `spishaderpgmlohs` | `uint32_t` | Shader program low address (HS) |
| `spishaderpgmhihs` | `uint32_t` | Shader program high address (HS) |
| `spishaderpgmrsrc1hs` | `uint32_t` | Shader resource 1 (HS) |
| `spishaderpgmrsrc2hs` | `uint32_t` | Shader resource 2 (HS) |
| `vgttfparam` | `uint32_t` | Tessellation factor parameters |
| `vgthosmaxtesslevel` | `uint32_t` | Maximum tessellation level |
| `vgthosmintesslevel` | `uint32_t` | Minimum tessellation level |

### GnmLsStageRegisters

Local shader (LS) stage registers (0x10 bytes).

```c
typedef struct {
    uint32_t spishaderpgmlols;
    uint32_t spishaderpgmhils;

    uint32_t spishaderpgmrsrc1ls;
    uint32_t spishaderpgmrsrc2ls;
} GnmLsStageRegisters;
_Static_assert(sizeof(GnmLsStageRegisters) == 0x10, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `spishaderpgmlols` | `uint32_t` | Shader program low address (LS) |
| `spishaderpgmhils` | `uint32_t` | Shader program high address (LS) |
| `spishaderpgmrsrc1ls` | `uint32_t` | Shader resource 1 (LS) |
| `spishaderpgmrsrc2ls` | `uint32_t` | Shader resource 2 (LS) |

---

## GnmFetchShaderCreateInfo

Parameters for creating a fetch shader via `sceGnmCreateFetchShader`.

```c
typedef struct {
    const GnmVsStageRegisters* regs;
    GnmFetchShaderFlags flags;

    const GnmInputUsageSlot* inputusages;
    uint32_t numinputusages;

    const GnmVertexInputSemantic* vtxinputs;
    uint32_t numvtxinputs;

    const uint32_t* remaptable;
    uint32_t remaptablecount;

    const GnmFetchShaderInstancingMode* instancedata;
    uint32_t numinstancedata;

    uint8_t vertexbaseusgpr;
    uint8_t instancebaseusgpr;
} GnmFetchShaderCreateInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `regs` | `const GnmVsStageRegisters*` | VS stage registers to use |
| `flags` | `GnmFetchShaderFlags` | Fetch shader flags |
| `inputusages` | `const GnmInputUsageSlot*` | Input usage slot array |
| `numinputusages` | `uint32_t` | Number of input usage slots |
| `vtxinputs` | `const GnmVertexInputSemantic*` | Vertex input semantic array |
| `numvtxinputs` | `uint32_t` | Number of vertex input semantics |
| `remaptable` | `const uint32_t*` | Semantic remap table |
| `remaptablecount` | `uint32_t` | Number of entries in remap table |
| `instancedata` | `const GnmFetchShaderInstancingMode*` | Instancing mode array |
| `numinstancedata` | `uint32_t` | Number of instancing mode entries |
| `vertexbaseusgpr` | `uint8_t` | Base user SGPR for vertex data |
| `instancebaseusgpr` | `uint8_t` | Base user SGPR for instance data |

---

## GnmFetchShaderResults

Output results from `sceGnmCreateFetchShader`, used to update VS registers.

```c
typedef struct {
    uint32_t sgprs;
    uint32_t vgprcompcnt;
} GnmFetchShaderResults;
```

| Field | Type | Description |
|-------|------|-------------|
| `sgprs` | `uint32_t` | Number of SGPRs used |
| `vgprcompcnt` | `uint32_t` | VGPR compute count |

---

## Functions

### sceGnmFetchShaderCalcSize

Calculates the required size in bytes for a fetch shader.

```c
GnmError sceGnmFetchShaderCalcSize(
    uint32_t* outsize, const GnmFetchShaderCreateInfo* ci
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `outsize` | `uint32_t*` | Output size in bytes |
| `ci` | `const GnmFetchShaderCreateInfo*` | Fetch shader creation info |
| **Returns** | `GnmError` | `GNM_ERROR_OK` on success |

### sceGnmCreateFetchShader

Creates a fetch shader in the provided output buffer.

```c
GnmError sceGnmCreateFetchShader(
    void* outdata, uint32_t outdatasize, const GnmFetchShaderCreateInfo* ci,
    GnmFetchShaderResults* r
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `outdata` | `void*` | Output buffer for fetch shader |
| `outdatasize` | `uint32_t` | Size of output buffer |
| `ci` | `const GnmFetchShaderCreateInfo*` | Fetch shader creation info |
| `r` | `GnmFetchShaderResults*` | Output results |
| **Returns** | `GnmError` | `GNM_ERROR_OK` on success |

### sceGnmVsRegsSetFetchShaderModifier

Updates VS stage registers with fetch shader results (SGPR count and VGPR compute count).

```c
void sceGnmVsRegsSetFetchShaderModifier(
    GnmVsStageRegisters* regs, const GnmFetchShaderResults* r
);
```

### sceGnmVsRegsSetAddress

Sets the shader program address in VS stage registers.

```c
static inline void sceGnmVsRegsSetAddress(GnmVsStageRegisters* regs, void* addr);
```

### sceGnmPsRegsSetAddress

Sets the shader program address in PS stage registers.

```c
static inline void sceGnmPsRegsSetAddress(GnmPsStageRegisters* regs, void* addr);
```

### sceGnmCsRegsSetAddress

Sets the shader program address in CS stage registers.

```c
static inline void sceGnmCsRegsSetAddress(GnmCsStageRegisters* regs, void* addr);
```

### sceGnmGsRegsSetAddress

Sets the shader program address in GS stage registers.

```c
static inline void sceGnmGsRegsSetAddress(GnmGsStageRegisters* regs, void* addr);
```

### sceGnmEsRegsSetAddress

Sets the shader program address in ES stage registers.

```c
static inline void sceGnmEsRegsSetAddress(GnmEsStageRegisters* regs, void* addr);
```

### sceGnmHsRegsSetAddress

Sets the shader program address in HS stage registers.

```c
static inline void sceGnmHsRegsSetAddress(GnmHsStageRegisters* regs, void* addr);
```

### sceGnmLsRegsSetAddress

Sets the shader program address in LS stage registers.

```c
static inline void sceGnmLsRegsSetAddress(GnmLsStageRegisters* regs, void* addr);
```

---

## Static Assertions

```c
_Static_assert(sizeof(GnmInputUsageSlot) == 0x4, "");
_Static_assert(sizeof(GnmVertexInputSemantic) == 0x4, "");
_Static_assert(sizeof(GnmVertexExportSemantic) == 0x2, "");
_Static_assert(sizeof(GnmVsStageRegisters) == 0x1c, "");
_Static_assert(sizeof(GnmPixelInputSemantic) == 0x2, "");
_Static_assert(sizeof(GnmPsStageRegisters) == 0x30, "");
_Static_assert(sizeof(GnmCsStageRegisters) == 0x1c, "");
_Static_assert(sizeof(GnmGsStageRegisters) == 0x1c, "");
_Static_assert(sizeof(GnmEsStageRegisters) == 0x10, "");
_Static_assert(sizeof(GnmHsStageRegisters) == 0x1c, "");
_Static_assert(sizeof(GnmLsStageRegisters) == 0x10, "");
```

---

## See Also

- [Shader Binary](shaderbinary.md)
- [Controls](controls.md)
- [Render Target](rendertarget.md)
- [Depth Render Target](depthrendertarget.md)
