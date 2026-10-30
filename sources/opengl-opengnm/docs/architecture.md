# Architecture

opengnm is structured as a layered library with a shared core and two
platform-specific backends. This page describes the subsystems, data flow, and
design principles.

---

## Subsystem Overview

```mermaid
graph TB
    subgraph "Public API Headers"
        TYPES[gnm_types.h<br/>Enums & Constants]
        ERROR[gnm_error.h<br/>Error Codes]
        DF[gnm_dataformat.h<br/>Data Formats]
        RES[Resource Descriptors<br/>buffer, texture, sampler,<br/>rendertarget, depthrt]
        CTRL[gnm_controls.h<br/>Control Registers]
        SH[gnm_shader.h<br/>Stage Registers & Fetch]
        SHB[gnm_shaderbinary.h<br/>Shader Binary Format]
        CB[gnm_commandbuffer.h<br/>Command Buffer]
        DCB[gnm_drawcommandbuffer.h<br/>Draw Command Buffer]
        DRV[gnmdriver.h<br/>sceGnm* Runtime]
        GPA[gpuaddr.h<br/>sceGpa* Surface Math]
        PLAT[platform.h<br/>GPU Mode Detection]
        HELP[gnm_helpers.h<br/>VideoOut & Memory Helpers]
    end

    subgraph "Core Implementation"
        GCN[GCN Assembler<br/>src/gcn/]
        PM4[PM4 Encoder<br/>src/pm4/]
        GPA_IMPL[GpuAddr / AddrLib<br/>src/gpuaddr/]
        CORE[Resource Logic<br/>src/*.c]
    end

    subgraph "Backends"
        ORBIS[driver_orbis.c<br/>Firmware Delegation]
        GENERIC[driver_generic.c<br/>PM4 Emission]
    end

    TYPES --> DF
    DF --> RES
    RES --> CB
    CB --> DCB
    SH --> DCB
    SHB --> HELP
    CTRL --> DCB
    DCB --> DRV
    GPA --> RES
    PLAT --> HELP

    CORE --> GCN
    CORE --> PM4
    CORE --> GPA_IMPL

    DRV --> ORBIS
    DRV --> GENERIC
    DCB --> ORBIS
    DCB --> GENERIC
```

## Design Principles

### 1. ABI Compatibility First

Every public struct uses `_Static_assert` to verify its binary size matches the
Sony SDK layout. This ensures that code compiled against the official SDK
headers produces identical binary descriptors.

```c
_Static_assert(sizeof(GnmTexture) == 0x20, "");
_Static_assert(sizeof(GnmRenderTarget) == 0x40, "");
_Static_assert(sizeof(GnmCommandBuffer) == 0x40, "");
```

### 2. Header-Only Where Possible

Many accessor functions are `static inline` in the headers, meaning they
generate no library symbols and can be used without linking against opengnm.
This includes texture, buffer, render target, and depth render target getters
and setters.

### 3. Two Backends, One API

The same `sceGnm*` function signatures are implemented differently depending on
the selected backend:

| Aspect | Orbis Backend | Generic Backend |
|--------|--------------|-----------------|
| File | `src/driver_orbis.c` | `src/driver_generic.c` |
| Target | PS4 hardware | Host (macOS/Linux/Windows) |
| Submit | Forwards to `libSceGnmDriver` firmware | No-op / PM4 buffer capture |
| Draw commands | Firmware packet building | Direct PM4 emission |
| Use case | Production PS4 apps | Testing, development, CI |

See [Backends](backends.md) for details.

### 4. Source-Level freegnm Compatibility

The `<compat/freegnm.h>` header provides `#define` aliases that map old `gnm*`
wrapper names to `sceGnm*` ABI symbols. This is source-only — no second set of
exported symbols is created. Existing freegnm consumers can switch to opengnm
by changing their include path.

---

## Data Flow

### Typical Rendering Frame

```mermaid
sequenceDiagram
    participant App
    participant CB as Command Buffer
    participant DCB as Draw Cmd Buffer
    participant DRV as sceGnm Driver
    participant FW as Firmware
    participant VO as VideoOut

    App->>VO: sceGnmVideoOutOpen()
    App->>CB: sceGnmCmdInit(buffer, size, ...)
    App->>DCB: sceGnmDrawCmdInitDefaultHardwareState(cmd)
    App->>DCB: sceGnmDrawCmdSetRenderTarget(cmd, 0, &rt)
    App->>DCB: sceGnmDrawCmdSetVsShader(cmd, &vsRegs, 0)
    App->>DCB: sceGnmDrawCmdSetPsShader(cmd, &psRegs)
    App->>DCB: sceGnmDrawCmdSetPrimitiveType(cmd, GNM_PT_TRILIST)
    App->>DCB: sceGnmDrawCmdDrawIndexAuto(cmd, vertexCount)
    App->>DRV: sceGnmSubmitAndFlipCommandBuffers(...)
    DRV->>FW: libSceGnmDriver submit
    FW->>VO: Flip display buffer
    App->>VO: sceGnmVideoOutSubmitFlipAndWait()
```

### Surface Setup Flow

```mermaid
graph LR
    A[Choose format<br/>GnmDataFormat] --> B[sceGpaComputeSurfaceInfo]
    B --> C[Allocate direct memory<br/>sceGnmDirectMemoryAllocate]
    C --> D[Create descriptor<br/>sceGnmCreateTexture /<br/>sceGnmCreateRenderTarget]
    D --> E[Bind to command buffer<br/>sceGnmDrawCmdSetRenderTarget]
```

---

## Source Layout

```
opengnm/
├── include/                # Public API headers
│   ├── gnm.h              # Master include
│   ├── gnm_types.h        # Enums, constants, limits
│   ├── gnm_error.h        # Error codes
│   ├── gnm_dataformat.h   # Data formats
│   ├── gnm_buffer.h       # Buffer descriptor
│   ├── gnm_texture.h      # Texture descriptor
│   ├── gnm_sampler.h      # Sampler descriptor
│   ├── gnm_rendertarget.h # Render target descriptor
│   ├── gnm_depthrendertarget.h
│   ├── gnm_controls.h     # Control register structs
│   ├── gnm_shader.h       # Shader stage registers
│   ├── gnm_shaderbinary.h # Shader binary format
│   ├── gnm_commandbuffer.h
│   ├── gnm_drawcommandbuffer.h
│   ├── gnmdriver.h        # sceGnm* runtime (207+ functions)
│   ├── gpuaddr.h          # sceGpa* surface computation
│   ├── platform.h         # GPU mode detection
│   ├── gnm_helpers.h      # VideoOut, memory, validation
│   ├── gnm_strings.h      # Enum string converters
│   ├── compat/freegnm.h   # freegnm source compatibility
│   └── gnm/               # Forwarding headers for compat
├── src/                    # Implementation
│   ├── *.c                # Core resource logic
│   ├── driver_orbis.c     # Orbis backend
│   ├── driver_generic.c   # Generic backend
│   ├── platform_orbis.c
│   ├── platform_generic.c
│   ├── gcn/               # GCN assembler (fetch shaders)
│   ├── pm4/               # PM4 packet encoder
│   ├── gpuaddr/           # AddrLib surface computation
│   └── u/                 # Internal utilities
├── tests/                  # Test suite (54+ tests)
├── CMakeLists.txt          # CMake build
├── Makefile                # Make build
└── config.{generic,orbis}.mak
```

---

## Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `GNM_NUM_SHADER_STAGES` | 8 | Number of shader stages |
| `GNM_MAX_RENDERTARGETS` | 8 | Maximum simultaneous render targets |
| `GNM_MAX_VIEWPORTS` | 16 | Maximum simultaneous viewports |
| `GNM_MAX_TSHARP_USERDATA_SLOTS` | 8 | Max texture user data slots |
| `GNM_MAX_SSHARP_USERDATA_SLOTS` | 12 | Max sampler user data slots |
| `GNM_MAX_VSHARP_USERDATA_SLOTS` | 12 | Max buffer user data slots |
| `GNM_MAX_POINTER_USERDATA_SLOTS` | 14 | Max pointer user data slots |
| `GNM_ALIGNMENT_SHADER_BYTES` | 256 | Shader code alignment |
| `GNM_ALIGNMENT_BUFFER_BYTES` | 4 | Buffer base address alignment |
| `GNM_INDIRECT_BUFFER_MAX_BYTESIZE` | 0x3FFFFC | Max indirect buffer size |
