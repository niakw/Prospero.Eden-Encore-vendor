# Shaders

This guide covers the PS4 shader binary format, stage register setup, fetch
shader generation, and shader binding to the pipeline.

---

## Shader Stages

The PS4 GPU (GCN architecture) supports the following shader stages:

| Stage | Enum | Register Struct | Set Function |
|-------|------|-----------------|-------------|
| Compute | `GNM_STAGE_CS` | `GnmCsStageRegisters` | `sceGnmSetCsShader` |
| Pixel | `GNM_STAGE_PS` | `GnmPsStageRegisters` | `sceGnmSetPsShader` |
| Vertex | `GNM_STAGE_VS` | `GnmVsStageRegisters` | `sceGnmSetVsShader` |
| Geometry | `GNM_STAGE_GS` | `GnmGsStageRegisters` | `sceGnmSetGsShader` |
| Export | `GNM_STAGE_ES` | `GnmEsStageRegisters` | `sceGnmSetEsShader` |
| Hull | `GNM_STAGE_HS` | `GnmHsStageRegisters` | `sceGnmSetHsShader` |
| Local | `GNM_STAGE_LS` | `GnmLsStageRegisters` | `sceGnmSetLsShader` |

---

## Stage Registers

Each shader stage has a register struct that the GPU reads to find the shader
program and its configuration. The most important fields are the program
address (split into lo/hi 32-bit halves) and the resource registers.

### Setting the Shader Address

```c
GnmVsStageRegisters vsRegs;
sceGnmVsRegsSetAddress(&vsRegs, shaderCodePtr);

GnmPsStageRegisters psRegs;
sceGnmPsRegsSetAddress(&psRegs, shaderCodePtr);

GnmCsStageRegisters csRegs;
sceGnmCsRegsSetAddress(&csRegs, shaderCodePtr);
```

The address is stored as `address >> 8` (256-byte alignment, matching
`GNM_ALIGNMENT_SHADER_BYTES`).

### VS Stage Registers (0x1C bytes)

| Field | Description |
|-------|-------------|
| `spishaderpgmlovs` | Program address low (>> 8) |
| `spishaderpgmhivs` | Program address high (>> 40) |
| `spishaderpgmrsrc1vs` | Resource config 1 |
| `spishaderpgmrsrc2vs` | Resource config 2 |
| `spivsoutconfig` | Output configuration |
| `spishaderposformat` | Position format |
| `paclvsoutcntl` | Output control |

### PS Stage Registers (0x30 bytes)

| Field | Description |
|-------|-------------|
| `spishaderpgmlops` | Program address low (>> 8) |
| `spishaderpgmhips` | Program address high (>> 40) |
| `spishaderpgmrsrc1ps` | Resource config 1 |
| `spishaderpgmrsrc2ps` | Resource config 2 |
| `spishaderzformat` | Z format |
| `spishadercolformat` | Color export format (GnmPsExportFormat) |
| `spipsinputena` | PS input enable |
| `spipsinputaddr` | PS input address |
| `spipsincontrol` | PS input control |
| `spibaryccntl` | Barycentric control |
| `dbshadercontrol` | DB shader control |
| `cbshadermask` | Color buffer shader mask |

---

## Shader Binary Format

PS4 shaders are stored in a binary container format with a file header, common
data, stage-specific data, input usage slots, and the shader code.

### File Header (0x10 bytes)

```c
typedef struct {
    uint32_t magic;          // GNM_SHADER_FILE_HEADER_ID (0x72646853 = "Shdr")
    uint16_t vermajor;
    uint16_t verminor;
    uint8_t type;            // GnmShaderType
    uint8_t headersizedwords;
    uint8_t auxdata;
    uint8_t targetgpumodes;  // GnmTargetGpuMode
    uint32_t _unused;
} GnmShaderFileHeader;
```

### Common Data (0x8 bytes)

```c
typedef struct {
    uint32_t shadersize : 23;
    uint32_t isusingsrt : 1;
    uint32_t numinputusageslots : 8;
    uint16_t embeddedconstantbufferdqwords;
    uint16_t scratchsizeperthreaddwords;
} GnmShaderCommonData;
```

### VS Shader (0x28 bytes + tables)

```c
typedef struct {
    GnmShaderCommonData common;
    GnmVsStageRegisters registers;
    uint8_t numinputsemantics;
    uint8_t numexportsemantics;
    uint8_t gsmodeornuminputsemanticscs;
    uint8_t fetchcontrol;
} GnmVsShader;
```

After the `GnmVsShader` struct, the binary contains:

1. `GnmInputUsageSlot[numinputusageslots]` — resource usage table
2. `GnmVertexInputSemantic[numinputsemantics]` — vertex input semantics
3. `GnmVertexExportSemantic[numexportsemantics]` — vertex export semantics
4. Shader code (at offset `registers.spishaderpgmlovs`)

### PS Shader (0x3C bytes + tables)

```c
typedef struct {
    GnmShaderCommonData common;
    GnmPsStageRegisters registers;
    uint8_t numinputsemantics;
    uint8_t _unused[3];
} GnmPsShader;
```

After the struct: input usage slots, then pixel input semantics, then code.

### Shader Binary Info (0x1C bytes)

Embedded at the end of the shader code:

```c
typedef struct {
    uint8_t signature[7];    // "OrbShdr"
    uint8_t version;
    uint32_t ispsslcg : 1;
    uint32_t issourcecached : 1;
    uint32_t type : 4;       // GnmShaderBinaryType
    uint32_t sourcetype : 2;
    uint32_t length : 24;    // Shader code length
    uint8_t chunkusagebaseoffsetdwords;
    uint8_t numinputusageslots;
    // ...
    uint32_t shaderhash0;
    uint32_t shaderhash1;
    uint32_t crc32;
} GnmShaderBinaryInfo;
```

---

## Extracting Shader Metadata

Use the helper API to parse a shader binary:

```c
#include <gnm_helpers.h>

GnmShaderMetadata meta;
GnmError err = sceGnmShaderBinaryGetMetadata(
    shaderData, shaderSize, &meta
);
```

`GnmShaderMetadata` contains:

| Field | Description |
|-------|-------------|
| `type` | `GnmShaderType` (VS, PS, CS, etc.) |
| `targetgpumodes` | `GnmTargetGpuMode` |
| `versionmajor` / `versionminor` | Shader version |
| `fileheader` | Pointer to `GnmShaderFileHeader` |
| `common` | Pointer to `GnmShaderCommonData` |
| `stage` | Pointer to stage-specific data |
| `stagesize` | Size of stage-specific data |
| `inputusageslots` | Pointer to `GnmInputUsageSlot` array |
| `numinputusageslots` | Count of input usage slots |
| `numinputsemantics` | Count of input semantics |
| `numexportsemantics` | Count of export semantics |
| `shadercode` | Pointer to shader code |
| `shadercodesize` | Size of shader code |

### Inline Accessors

```c
const GnmShaderCommonData* common = sceGnmShfCommonData(fileHeader);

const GnmInputUsageSlot* slots = sceGnmVsShaderInputUsageSlotTable(vsShader);
const GnmVertexInputSemantic* inputs = sceGnmVsShaderInputSemanticTable(vsShader);
const GnmVertexExportSemantic* exports = sceGnmVsShaderExportSemanticTable(vsShader);
uint32_t size = sceGnmVsShaderCalcSize(vsShader);
const void* code = sceGnmVsShaderCodePtr(vsShader);
```

---

## Input Usage Slots

`GnmInputUsageSlot` (4 bytes) describes what resources a shader expects in its
user data registers:

```c
typedef struct {
    uint8_t usagetype;       // GnmShaderInputUsageType
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
```

### Usage Types

| Type | Description | Size (dwords) |
|------|-------------|---------------|
| `GNM_SHINPUTUSAGE_IMM_RESOURCE` | Immediate resource (T#) | 0 |
| `GNM_SHINPUTUSAGE_IMM_SAMPLER` | Immediate sampler (S#) | 4 |
| `GNM_SHINPUTUSAGE_IMM_CONSTBUFFER` | Immediate constant buffer (V#) | 4 |
| `GNM_SHINPUTUSAGE_IMM_VERTEXBUFFER` | Immediate vertex buffer (V#) | 4 |
| `GNM_SHINPUTUSAGE_IMM_RWRESOURCE` | Immediate RW resource | 0 |
| `GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST` | Immediate float constant | 1 |
| `GNM_SHINPUTUSAGE_IMM_ALUBOOL32CONST` | Immediate bool constant | 1 |
| `GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER` | Fetch shader sub-pointer | 2 |
| `GNM_SHINPUTUSAGE_PTR_RESOURCETABLE` | Resource table pointer | 2 |
| `GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE` | Sampler table pointer | 2 |
| `GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE` | Constant buffer table pointer | 2 |
| `GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE` | Vertex buffer table pointer | 2 |

Use `sceGnmShaderInputUsageTypeSize()` to get the size in dwords for a usage type.

---

## Fetch Shaders

Fetch shaders are small GCN programs that load vertex data from vertex buffers.
opengnm can generate fetch shaders at runtime using the GCN assembler.

### Creating a Fetch Shader

```c
#include <gnm_shader.h>

GnmFetchShaderCreateInfo ci = {
    .regs = &vsRegs,
    .flags = GNM_FETCH_FLAG_NONE,
    .inputusages = inputUsageSlots,
    .numinputusages = numSlots,
    .vtxinputs = vtxInputSemantics,
    .numvtxinputs = numInputs,
    .remaptable = remapTable,
    .remaptablecount = remapCount,
    .instancedata = instanceModes,
    .numinstancedata = numInstanceModes,
    .vertexbaseusgpr = 0,
    .instancebaseusgpr = 0,
};

// Calculate required size
uint32_t fetchSize;
sceGnmFetchShaderCalcSize(&fetchSize, &ci);

// Allocate and generate
void* fetchShader = aligned_alloc(256, fetchSize);
GnmFetchShaderResults results;
sceGnmCreateFetchShader(fetchShader, fetchSize, &ci, &results);

// Apply the fetch shader modifier to VS registers
sceGnmVsRegsSetFetchShaderModifier(&vsRegs, &results);
```

### Fetch Shader Results

```c
typedef struct {
    uint32_t sgprs;       // Number of SGPRs used
    uint32_t vgprcompcnt; // VGPR compute count
} GnmFetchShaderResults;
```

---

## Binding Shaders

### In the draw command buffer

```c
// Set VS shader
sceGnmDrawCmdSetVsShader(&cmd, &vsRegs, 0);

// Set PS shader
sceGnmDrawCmdSetPsShader(&cmd, &psRegs);

// Set embedded shaders (built-in firmware shaders)
sceGnmDrawCmdSetEmbeddedVsShader(&cmd, GNM_EMBEDDED_VSH_FULLSCREEN, 0);
sceGnmDrawCmdSetEmbeddedPsShader(&cmd, GNM_EMBEDDED_PSH_DUMMY);

// Set CS shader (for compute dispatch)
sceGnmDrawCmdSetCsShader(&cmd, &csRegs);
```

### Via the driver runtime

```c
sceGnmSetVsShader(cmdbuf, size, vsRegs, 0);
sceGnmSetPsShader(cmdbuf, size, psRegs);
sceGnmSetCsShader(cmdbuf, size, csRegs);
```

### Updating shaders

For partial shader updates without re-sending the full shader:

```c
sceGnmUpdateVsShader(cmdbuf, size, vsRegs, 0);
sceGnmUpdatePsShader(cmdbuf, size, psRegs);
```

---

## PS Input Usage

Connect VS exports to PS inputs:

```c
sceGnmDrawCmdSetPsInputUsage(
    &cmd,
    vsExportTable, numVsExports,
    psInputTable, numPsInputs
);
```

### Pixel Input Semantic (2 bytes)

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
    // NEO mode fields...
} GnmPixelInputSemantic;
```

### Default Values

| Value | Description |
|-------|-------------|
| `GNM_PX_DEFVAL_NONE` | No default |
| `GNM_PX_DEFVAL_0_0_0_1` | {0, 0, 0, 1} |
| `GNM_PX_DEFVAL_1_1_1_0` | {1, 1, 1, 0} |
| `GNM_PX_DEFVAL_1_1_1_1` | {1, 1, 1, 1} |

---

## Embedded Shaders

The PS4 firmware includes built-in shaders for common operations. opengnm
exposes them through the `sceGnmDrawCmdSetEmbeddedVsShader` /
`sceGnmDrawCmdSetEmbeddedPsShader` draw-command-buffer entry points, which
flow through the `sceGnmDriverSetEmbedded*` wrappers in the Orbis backend.

### Embedded VS Shaders

| Enum | Description |
|------|-------------|
| `GNM_EMBEDDED_VSH_FULLSCREEN` | Full-screen triangle |

### Embedded PS Shaders

| Enum | Description |
|------|-------------|
| `GNM_EMBEDDED_PSH_DUMMY` | Dummy pixel shader |
| `GNM_EMBEDDED_PSH_DUMMY_RG32` | Dummy pixel shader (RG32 format) |

### Backend Behavior

The `sceGnmDriverSetEmbedded*` wrappers use runtime HLE detection to select
between two emission paths:

On **real PS4 hardware** (libSceGnmDriver module loaded), the wrappers emit
the PM4 register-write sequence locally using embedded shader register blobs
and pad the reserved dword space with `PKT3_NOP`. This avoids the FW 9.00
crash in firmware shader-set helpers (`sceGnmSetVsShader` crashes inside the
GPU work pump context) while producing identical GPU state.

Under **HLE emulators** such as shadPS4 (where there is no
`libSceGnmDriver` module), the wrappers forward to the HLE-exposed
`sceGnmSetEmbeddedVsShader` / `sceGnmSetEmbeddedPsShader` entry points. The
emulator then writes its own built-in dummy shader code and registers.

The HLE forwarding path is necessary because the local register blobs
reference program addresses that only exist on real hardware; under shadPS4,
`SearchBinaryInfo` would fail with "Shader binary info not found" as soon as
a sample used an embedded PS shader. See
[Backends — HLE Emulator Support](../backends.md#hle-emulator-support-shadps4)
for the full rationale.

---

## See Also

- [Shader Reference](../reference/shader.md)
- [Shader Binary Reference](../reference/shaderbinary.md)
- [Command Buffers Guide](command-buffers.md)
- [Driver Runtime Reference](../reference/driver.md)
