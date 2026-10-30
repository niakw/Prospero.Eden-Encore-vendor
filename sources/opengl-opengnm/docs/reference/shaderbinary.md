# Shader Binary

API reference for shader binary structures and inline accessors in the opengnm PS4 GNM library. These structs describe the on-disk and in-memory layout of compiled GNM shader binaries.

Header: `gnm_shaderbinary.h`

---

## Defines

```c
#define GNM_SHADER_FILE_HEADER_ID 0x72646853  // "Shdr"
#define GNM_SHADER_BINARY_INFO_MAGIC "OrbShdr"
```

| Define | Value | Description |
|--------|-------|-------------|
| `GNM_SHADER_FILE_HEADER_ID` | `0x72646853` | Magic identifier for shader file headers ("Shdr") |
| `GNM_SHADER_BINARY_INFO_MAGIC` | `"OrbShdr"` | Magic signature for shader binary info blocks |

---

## GnmShaderType

Enumerates shader stage types.

```c
typedef enum {
    GNM_SHADER_INVALID = 0x0,
    GNM_SHADER_VERTEX = 0x1,
    GNM_SHADER_PIXEL = 0x2,
    GNM_SHADER_GEOMETRY = 0x3,
    GNM_SHADER_COMPUTE = 0x4,
    GNM_SHADER_EXPORT = 0x5,
    GNM_SHADER_LOCAL = 0x6,
    GNM_SHADER_HULL = 0x7,
} GnmShaderType;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_SHADER_INVALID` | 0x0 | Invalid shader type |
| `GNM_SHADER_VERTEX` | 0x1 | Vertex shader |
| `GNM_SHADER_PIXEL` | 0x2 | Pixel (fragment) shader |
| `GNM_SHADER_GEOMETRY` | 0x3 | Geometry shader |
| `GNM_SHADER_COMPUTE` | 0x4 | Compute shader |
| `GNM_SHADER_EXPORT` | 0x5 | Export shader |
| `GNM_SHADER_LOCAL` | 0x6 | Local shader |
| `GNM_SHADER_HULL` | 0x7 | Hull shader |

---

## GnmTargetGpuMode

Enumerates target GPU modes for shader compilation.

```c
typedef enum {
    GNM_TARGETGPUMODE_UNSPECIFIED = 0x0,
    GNM_TARGETGPUMODE_BASE = 0x1,
    GNM_TARGETGPUMODE_NEO = 0x2,
} GnmTargetGpuMode;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_TARGETGPUMODE_UNSPECIFIED` | 0x0 | Unspecified GPU mode |
| `GNM_TARGETGPUMODE_BASE` | 0x1 | Base PS4 GPU |
| `GNM_TARGETGPUMODE_NEO` | 0x2 | PS4 Pro (NEO) GPU |

---

## GnmShaderFileHeader

File header for a shader binary (0x10 bytes).

```c
typedef struct {
    uint32_t magic;     // GNM_SHADER_FILE_HEADER_ID
    uint16_t vermajor;
    uint16_t verminor;
    uint8_t type;  // GnmShaderType
    uint8_t headersizedwords;
    uint8_t auxdata;
    uint8_t targetgpumodes;  // GnmTargetGpuMode
    uint32_t _unused;
} GnmShaderFileHeader;
_Static_assert(sizeof(GnmShaderFileHeader) == 0x10, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `magic` | `uint32_t` | `GNM_SHADER_FILE_HEADER_ID` |
| `vermajor` | `uint16_t` | Major version |
| `verminor` | `uint16_t` | Minor version |
| `type` | `uint8_t` | `GnmShaderType` — shader stage type |
| `headersizedwords` | `uint8_t` | Header size in dwords |
| `auxdata` | `uint8_t` | Auxiliary data |
| `targetgpumodes` | `uint8_t` | `GnmTargetGpuMode` — target GPU modes |
| `_unused` | `uint32_t` | Reserved |

---

## GnmShaderCommonData

Common data shared by all shader types (0x8 bytes).

```c
typedef struct {
    uint32_t shadersize : 23;
    uint32_t isusingsrt : 1;
    uint32_t numinputusageslots : 8;
    uint16_t embeddedconstantbufferdqwords;  // 16 byte double q words
    uint16_t scratchsizeperthreaddwords;
} GnmShaderCommonData;
_Static_assert(sizeof(GnmShaderCommonData) == 0x8, "");
```

| Field | Bits/Type | Description |
|-------|-----------|-------------|
| `shadersize` | 23 | Shader code size in dwords |
| `isusingsrt` | 1 | Whether SRT is used |
| `numinputusageslots` | 8 | Number of input usage slots |
| `embeddedconstantbufferdqwords` | `uint16_t` | Embedded constant buffer size in 16-byte double-qwords |
| `scratchsizeperthreaddwords` | `uint16_t` | Scratch size per thread in dwords |

---

## GnmVsShaderGsMode

Enumerates geometry shader mode flags for vertex shaders. Values can be combined as bitmasks.

```c
typedef enum {
    GNM_VSGSMODE_G = 0x1,

    GNM_VSGSMODE_G_ONCHIP = 0x2,
    GNM_VSGSMODE_G_ES_PASSTHRU = 0x4,
    GNM_VSGSMODE_G_ES_ELEMENTINFO = 0x8,
    GNM_VSGSMODE_G_CUTMODE_1024 = 0x0,
    GNM_VSGSMODE_G_CUTMODE_512 = 0x10,
    GNM_VSGSMODE_G_CUTMODE_256 = 0x20,
    GNM_VSGSMODE_G_CUTMODE_128 = 0x30,
    GNM_VSGSMODE_G_SUPRESSCUTS = 0x40,
    GNM_VSGSMODE_G_CUTMODE_MASK = 0x30,
    GNM_VSGSMODE_G_CUTMODE_SHIFT = 0x4,

    GNM_VSGSMODE_OFF = 0x0,
    GNM_VSGSMODE_A = 0x2,
    GNM_VSGSMODE_B = 0x4,
    GNM_VSGSMODE_C = 0x8,
    GNM_VSGSMODE_SPRITE_EN = 0xA,
    GNM_VSGSMODE_MASK = 0xE,
    GNM_VSGSMODE_CPACK = 0x10,
} GnmVsShaderGsMode;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_VSGSMODE_G` | 0x1 | Geometry shader enabled |
| `GNM_VSGSMODE_G_ONCHIP` | 0x2 | On-chip GS |
| `GNM_VSGSMODE_G_ES_PASSTHRU` | 0x4 | GS ES passthrough |
| `GNM_VSGSMODE_G_ES_ELEMENTINFO` | 0x8 | GS ES element info |
| `GNM_VSGSMODE_G_CUTMODE_1024` | 0x0 | GS cut mode 1024 |
| `GNM_VSGSMODE_G_CUTMODE_512` | 0x10 | GS cut mode 512 |
| `GNM_VSGSMODE_G_CUTMODE_256` | 0x20 | GS cut mode 256 |
| `GNM_VSGSMODE_G_CUTMODE_128` | 0x30 | GS cut mode 128 |
| `GNM_VSGSMODE_G_SUPRESSCUTS` | 0x40 | Suppress GS cuts |
| `GNM_VSGSMODE_G_CUTMODE_MASK` | 0x30 | Mask for GS cut mode bits |
| `GNM_VSGSMODE_G_CUTMODE_SHIFT` | 0x4 | Shift for GS cut mode bits |
| `GNM_VSGSMODE_OFF` | 0x0 | GS mode off |
| `GNM_VSGSMODE_A` | 0x2 | VS mode A |
| `GNM_VSGSMODE_B` | 0x4 | VS mode B |
| `GNM_VSGSMODE_C` | 0x8 | VS mode C |
| `GNM_VSGSMODE_SPRITE_EN` | 0xA | Sprite enable |
| `GNM_VSGSMODE_MASK` | 0xE | Mask for VS GS mode bits |
| `GNM_VSGSMODE_CPACK` | 0x10 | C pack mode |

---

## GnmVsShader

Vertex shader binary structure (0x28 bytes). Followed in memory by input usage slots, vertex input semantics, and export semantics.

```c
typedef struct {
    GnmShaderCommonData common;
    GnmVsStageRegisters registers;

    uint8_t numinputsemantics;
    uint8_t numexportsemantics;
    uint8_t gsmodeornuminputsemanticscs;  // GnmVsShaderGsMode
    uint8_t fetchcontrol;
} GnmVsShader;
_Static_assert(sizeof(GnmVsShader) == 0x28, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `common` | `GnmShaderCommonData` | Common shader data |
| `registers` | `GnmVsStageRegisters` | VS stage registers |
| `numinputsemantics` | `uint8_t` | Number of vertex input semantics |
| `numexportsemantics` | `uint8_t` | Number of vertex export semantics |
| `gsmodeornuminputsemanticscs` | `uint8_t` | `GnmVsShaderGsMode` or CS input semantic count |
| `fetchcontrol` | `uint8_t` | Fetch control |

---

## GnmPsShader

Pixel shader binary structure (0x3C bytes). Followed in memory by input usage slots and pixel input semantics.

```c
typedef struct {
    GnmShaderCommonData common;
    GnmPsStageRegisters registers;

    uint8_t numinputsemantics;
    uint8_t _unused[3];
} GnmPsShader;
_Static_assert(sizeof(GnmPsShader) == 0x3c, "");
```

| Field | Type | Description |
|-------|------|-------------|
| `common` | `GnmShaderCommonData` | Common shader data |
| `registers` | `GnmPsStageRegisters` | PS stage registers |
| `numinputsemantics` | `uint8_t` | Number of pixel input semantics |
| `_unused` | `uint8_t[3]` | Reserved |

---

## GnmShaderBinaryType

Enumerates the shader binary type, indicating which pipeline stage and sub-mode the binary represents.

```c
typedef enum {
    GNM_SHB_PS = 0,
    GNM_SHB_VS_VS = 1,
    GNM_SHB_VS_ES = 2,
    GNM_SHB_VS_LS = 3,
    GNM_SHB_CS = 4,
    GNM_SHB_GS = 5,
    GNM_SHB_GS_VS = 6,
    GNM_SHB_HS = 7,
    GNM_SHB_DS_VS = 8,
    GNM_SHB_DS_ES = 9,
} GnmShaderBinaryType;
```

| Enum Value | Value | Description |
|------------|-------|-------------|
| `GNM_SHB_PS` | 0 | Pixel shader |
| `GNM_SHB_VS_VS` | 1 | Vertex shader (VS path) |
| `GNM_SHB_VS_ES` | 2 | Vertex shader (ES path) |
| `GNM_SHB_VS_LS` | 3 | Vertex shader (LS path) |
| `GNM_SHB_CS` | 4 | Compute shader |
| `GNM_SHB_GS` | 5 | Geometry shader |
| `GNM_SHB_GS_VS` | 6 | Geometry shader (VS path) |
| `GNM_SHB_HS` | 7 | Hull shader |
| `GNM_SHB_DS_VS` | 8 | Domain shader (VS path) |
| `GNM_SHB_DS_ES` | 9 | Domain shader (ES path) |

---

## GnmShaderBinaryInfo

Shader binary info block (0x1C bytes). Embedded at the end of a shader binary, containing metadata, hash, and CRC.

```c
typedef struct {
    uint8_t signature[7];  // GNM_SHADER_BINARY_INFO_MAGIC
    uint8_t version;

    // if true, it's PSSL/CG. else it's IL/shtb
    uint32_t ispsslcg : 1;
    // is debugging source cached?
    uint32_t issourcecached : 1;
    uint32_t type : 4;      // GnmShaderBinaryType
    uint32_t sourcetype : 2;  // ShaderSourceType
    // Shader code length
    uint32_t length : 24;

    // starts at ((uint32_t*)&ShaderBinaryInfo) - chunkusagebaseoffsetdwords
    uint8_t chunkusagebaseoffsetdwords;
    uint8_t numinputusageslots;
    uint8_t hassrt : 1;
    uint8_t hassrtusedvalidinfo : 1;
    uint8_t hasextendedusageinfo : 1;
    uint8_t _unused : 5;
    uint8_t _unused2;

    uint32_t shaderhash0;
    uint32_t shaderhash1;
    // CRC32 of whole shader until this field
    uint32_t crc32;
} GnmShaderBinaryInfo;
_Static_assert(sizeof(GnmShaderBinaryInfo) == 0x1c, "");
```

| Field | Type/Bits | Description |
|-------|-----------|-------------|
| `signature` | `uint8_t[7]` | `GNM_SHADER_BINARY_INFO_MAGIC` ("OrbShdr") |
| `version` | `uint8_t` | Binary info version |
| `ispsslcg` | 1 | True if PSSL/CG, false if IL/shtb |
| `issourcecached` | 1 | Whether debug source is cached |
| `type` | 4 | `GnmShaderBinaryType` — shader binary type |
| `sourcetype` | 2 | `ShaderSourceType` — source type |
| `length` | 24 | Shader code length |
| `chunkusagebaseoffsetdwords` | `uint8_t` | Base offset in dwords to chunk usage data |
| `numinputusageslots` | `uint8_t` | Number of input usage slots |
| `hassrt` | 1 | Has SRT |
| `hassrtusedvalidinfo` | 1 | Has valid SRT usage info |
| `hasextendedusageinfo` | 1 | Has extended usage info |
| `_unused` | 5 | Reserved |
| `_unused2` | `uint8_t` | Reserved |
| `shaderhash0` | `uint32_t` | Shader hash (low 32 bits) |
| `shaderhash1` | `uint32_t` | Shader hash (high 32 bits) |
| `crc32` | `uint32_t` | CRC32 of whole shader up to this field |

---

## Inline Accessor Functions

### sceGnmShaderCommonCodeSize

Returns the total code size including embedded constant buffer.

```c
static inline uint32_t sceGnmShaderCommonCodeSize(const GnmShaderCommonData* data);
```

### sceGnmShaderInputUsageTypeSize

Returns the size in dwords of a shader input usage type. Returns -1 for unknown types.

```c
static inline int32_t sceGnmShaderInputUsageTypeSize(GnmShaderInputUsageType type);
```

| Usage Type | Size (dwords) |
|------------|---------------|
| `IMM_RESOURCE`, `IMM_RWRESOURCE`, `IMM_SRT` | 0 |
| `IMM_SAMPLER`, `IMM_CONSTBUFFER`, `IMM_VERTEXBUFFER` | 4 |
| `IMM_ALUFLOATCONST`, `IMM_ALUBOOL32CONST`, `IMM_GDSCOUNTERRANGE`, `IMM_GDSMEMORYRANGE`, `IMM_GWSBASE`, `IMM_LDSESGSSIZE` | 1 |
| `SUBPTR_FETCHSHADER`, all `PTR_*` types | 2 |
| Unknown | -1 |

### sceGnmShfCommonData

Returns a pointer to the `GnmShaderCommonData` immediately following a `GnmShaderFileHeader`. Returns NULL if `shf` is NULL.

```c
static inline const GnmShaderCommonData* sceGnmShfCommonData(
    const GnmShaderFileHeader* shf
);
```

### sceGnmVsShaderInputUsageSlotTable

Returns a pointer to the `GnmInputUsageSlot` table following a `GnmVsShader`.

```c
static inline const GnmInputUsageSlot* sceGnmVsShaderInputUsageSlotTable(
    const GnmVsShader* vs
);
```

### sceGnmVsShaderInputSemanticTable

Returns a pointer to the `GnmVertexInputSemantic` table following the input usage slots of a `GnmVsShader`.

```c
static inline const GnmVertexInputSemantic* sceGnmVsShaderInputSemanticTable(
    const GnmVsShader* vs
);
```

### sceGnmVsShaderExportSemanticTable

Returns a pointer to the `GnmVertexExportSemantic` table following the input semantics of a `GnmVsShader`.

```c
static inline const GnmVertexExportSemantic* sceGnmVsShaderExportSemanticTable(
    const GnmVsShader* vs
);
```

### sceGnmVsShaderCalcSize

Calculates the total size in bytes of a `GnmVsShader` including all trailing tables (input usage slots, input semantics, export semantics). Result is 4-byte aligned.

```c
static inline uint32_t sceGnmVsShaderCalcSize(const GnmVsShader* vs);
```

### sceGnmVsShaderCodePtr

Returns a pointer to the shader code within a `GnmVsShader`. Returns NULL if `vs` is NULL.

```c
static inline const void* sceGnmVsShaderCodePtr(const GnmVsShader* vs);
```

### sceGnmPsShaderInputUsageSlotTable

Returns a pointer to the `GnmInputUsageSlot` table following a `GnmPsShader`.

```c
static inline const GnmInputUsageSlot* sceGnmPsShaderInputUsageSlotTable(
    const GnmPsShader* ps
);
```

### sceGnmPsShaderInputSemanticTable

Returns a pointer to the `GnmPixelInputSemantic` table following the input usage slots of a `GnmPsShader`.

```c
static inline const GnmPixelInputSemantic* sceGnmPsShaderInputSemanticTable(
    const GnmPsShader* ps
);
```

### sceGnmPsShaderCalcSize

Calculates the total size in bytes of a `GnmPsShader` including all trailing tables (input usage slots, pixel input semantics). Result is 4-byte aligned.

```c
static inline uint32_t sceGnmPsShaderCalcSize(const GnmPsShader* ps);
```

### sceGnmPsShaderCodePtr

Returns a pointer to the shader code within a `GnmPsShader`. Returns NULL if `ps` is NULL.

```c
static inline const void* sceGnmPsShaderCodePtr(const GnmPsShader* ps);
```

---

## Static Assertions

```c
_Static_assert(sizeof(GnmShaderFileHeader) == 0x10, "");
_Static_assert(sizeof(GnmShaderCommonData) == 0x8, "");
_Static_assert(sizeof(GnmVsShader) == 0x28, "");
_Static_assert(sizeof(GnmPsShader) == 0x3c, "");
_Static_assert(sizeof(GnmShaderBinaryInfo) == 0x1c, "");
```

---

## See Also

- [Shader](shader.md)
- [Controls](controls.md)
- [Render Target](rendertarget.md)
- [Depth Render Target](depthrendertarget.md)
