# GNM Helpers API Reference

The `gnm_helpers.h` header provides high-level convenience functions wrapping the lower-level GNM driver and GPU address APIs. It covers direct memory allocation, VideoOut display management, 2D texture creation, color render-target creation, shader binary metadata extraction, and command-buffer validation.

Header: `include/gnm_helpers.h`

---

## Constants

### Direct Memory

| Constant | Value | Description |
|----------|-------|-------------|
| `GNM_DIRECT_MEMORY_TYPE_WC_GARLIC` | `3` | Write-combined Garlic memory type. Garlic is the GPU-coherent CPU memory pool. |

### Memory Protection Flags

| Constant | Value | Description |
|----------|-------|-------------|
| `GNM_PROT_CPU_READ` | `0x01` | CPU read access. |
| `GNM_PROT_CPU_RW` | `0x02` | CPU read/write access. |
| `GNM_PROT_GPU_READ` | `0x10` | GPU read access. |
| `GNM_PROT_GPU_WRITE` | `0x20` | GPU write access. |
| `GNM_PROT_CPU_GPU_RW` | `0x33` | Combined CPU + GPU read/write access (`CPU_READ \| CPU_RW \| GPU_READ \| GPU_WRITE`). |

### VideoOut Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `GNM_VIDEO_OUT_MAX_BUFFERS` | `4` | Maximum number of display buffers per VideoOut instance. |
| `GNM_VIDEO_OUT_DEFAULT_WIDTH` | `1920` | Default display width (1080p). |
| `GNM_VIDEO_OUT_DEFAULT_HEIGHT` | `1080` | Default display height (1080p). |
| `GNM_VIDEO_OUT_DEFAULT_BUFFERS` | `2` | Default number of display buffers (double-buffered). |
| `GNM_VIDEO_OUT_BYTES_PER_PIXEL_A8B8G8R8` | `4` | Bytes per pixel for A8B8G8R8 format. |
| `GNM_VIDEO_OUT_MEMORY_ALIGNMENT` | `65536` | Display buffer memory alignment (64 KiB). |
| `GNM_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB` | `0x80002200` | Pixel format constant for A8B8G8R8 sRGB. |
| `GNM_VIDEO_OUT_TILING_MODE_LINEAR` | `1` | Linear tiling mode for display buffers. |
| `GNM_VIDEO_OUT_ASPECT_RATIO_NONE` | `0` | No aspect ratio correction. |
| `GNM_VIDEO_OUT_BUS_MAIN` | `0` | Main display bus. |
| `GNM_VIDEO_OUT_FLIP_60HZ` | `0` | Flip at 60 Hz rate. |
| `GNM_VIDEO_OUT_FLIP_VSYNC` | `1` | Flip on VSync. |

---

## Structs

### GnmDirectMemory

Represents a block of direct (GPU-visible) memory allocated from the system.

```c
typedef struct {
    void* mapped;
    uint64_t directmemory;
    uint64_t size;
    uint64_t alignment;
    void* allocation;
    bool allocated;
} GnmDirectMemory;
```

| Field | Type | Description |
|-------|------|-------------|
| `mapped` | `void*` | CPU-mapped pointer to the memory block. |
| `directmemory` | `uint64_t` | GPU-side direct memory address. |
| `size` | `uint64_t` | Size of the allocation in bytes. |
| `alignment` | `uint64_t` | Alignment of the allocation in bytes. |
| `allocation` | `void*` | Internal allocation handle. |
| `allocated` | `bool` | Whether this struct represents a live allocation. |

---

### GnmVideoOutCreateInfo

Creation parameters for a VideoOut display output.

```c
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t numbuffers;
    uint32_t bytesperpixel;
    uint64_t alignment;
    int32_t bus;
    int32_t pixel_format;
    int32_t tiling_mode;
    int32_t aspect_ratio;
    int32_t flip_rate;
} GnmVideoOutCreateInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `width` | `uint32_t` | Display width in pixels. |
| `height` | `uint32_t` | Display height in pixels. |
| `pitch` | `uint32_t` | Display buffer pitch in pixels. |
| `numbuffers` | `uint32_t` | Number of display buffers (1–4). |
| `bytesperpixel` | `uint32_t` | Bytes per pixel. |
| `alignment` | `uint64_t` | Buffer alignment in bytes. |
| `bus` | `int32_t` | Display bus (`GNM_VIDEO_OUT_BUS_MAIN`). |
| `pixel_format` | `int32_t` | Pixel format (e.g. `GNM_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB`). |
| `tiling_mode` | `int32_t` | Tiling mode (`GNM_VIDEO_OUT_TILING_MODE_LINEAR`). |
| `aspect_ratio` | `int32_t` | Aspect ratio mode (`GNM_VIDEO_OUT_ASPECT_RATIO_NONE`). |
| `flip_rate` | `int32_t` | Flip rate (`GNM_VIDEO_OUT_FLIP_60HZ` or `GNM_VIDEO_OUT_FLIP_VSYNC`). |

---

### GnmVideoOut

Represents an open VideoOut display output with its associated buffers and state.

```c
typedef struct {
    int32_t handle;
    uintptr_t flipqueue;
    GnmDirectMemory memory;
    void* buffers[GNM_VIDEO_OUT_MAX_BUFFERS];
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t numbuffers;
    uint32_t registeredbuffers;
    uint32_t currentbuffer;
    uint64_t buffersize;
    uint64_t bufferstride;
    uint64_t frame;
    uint32_t last_error_stage;
    int32_t last_error_code;
} GnmVideoOut;
```

| Field | Type | Description |
|-------|------|-------------|
| `handle` | `int32_t` | Kernel VideoOut handle. |
| `flipqueue` | `uintptr_t` | Flip queue handle. |
| `memory` | `GnmDirectMemory` | Direct memory backing the display buffers. |
| `buffers` | `void*[4]` | Array of display buffer pointers (up to `GNM_VIDEO_OUT_MAX_BUFFERS`). |
| `width` | `uint32_t` | Display width. |
| `height` | `uint32_t` | Display height. |
| `pitch` | `uint32_t` | Display buffer pitch. |
| `numbuffers` | `uint32_t` | Number of allocated buffers. |
| `registeredbuffers` | `uint32_t` | Number of buffers registered with the kernel. |
| `currentbuffer` | `uint32_t` | Index of the currently displayed buffer. |
| `buffersize` | `uint64_t` | Size of each display buffer in bytes. |
| `bufferstride` | `uint64_t` | Stride of each display buffer in bytes. |
| `frame` | `uint64_t` | Current frame counter. |
| `last_error_stage` | `uint32_t` | Diagnostic stage for a failed VideoOut open (1–6, zero on success). |
| `last_error_code` | `int32_t` | Error code from the last failed operation. |

---

### GnmShaderMetadata

Parsed metadata extracted from a shader binary. Provides access to the shader's header, common data, stage-specific data, and input/output usage slots.

```c
typedef struct {
    GnmShaderType type;
    GnmTargetGpuMode targetgpumodes;
    uint16_t versionmajor;
    uint16_t versionminor;
    const GnmShaderFileHeader* fileheader;
    const GnmShaderCommonData* common;
    const void* stage;
    uint32_t stagesize;
    const GnmInputUsageSlot* inputusageslots;
    uint32_t numinputusageslots;
    uint32_t numinputsemantics;
    uint32_t numexportsemantics;
    const void* shadercode;
    uint32_t shadercodesize;
} GnmShaderMetadata;
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | `GnmShaderType` | Shader stage type (VS, PS, CS, etc.). |
| `targetgpumodes` | `GnmTargetGpuMode` | Target GPU modes (base, Neo, or both). |
| `versionmajor` | `uint16_t` | Shader format major version. |
| `versionminor` | `uint16_t` | Shader format minor version. |
| `fileheader` | `const GnmShaderFileHeader*` | Pointer to the shader file header. |
| `common` | `const GnmShaderCommonData*` | Pointer to the common shader data section. |
| `stage` | `const void*` | Pointer to the stage-specific data section. |
| `stagesize` | `uint32_t` | Size of the stage-specific data in bytes. |
| `inputusageslots` | `const GnmInputUsageSlot*` | Array of input usage slots. |
| `numinputusageslots` | `uint32_t` | Number of input usage slots. |
| `numinputsemantics` | `uint32_t` | Number of input semantics. |
| `numexportsemantics` | `uint32_t` | Number of export semantics. |
| `shadercode` | `const void*` | Pointer to the raw shader machine code. |
| `shadercodesize` | `uint32_t` | Size of the shader machine code in bytes. |

---

### GnmCommandBufferValidationInfo

Result of command-buffer validation, describing capacity and usage.

```c
typedef struct {
    uint32_t capacitydwords;
    uint32_t useddwords;
    uint32_t remainingdwords;
    const char* message;
} GnmCommandBufferValidationInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `capacitydwords` | `uint32_t` | Total capacity of the command buffer in dwords. |
| `useddwords` | `uint32_t` | Number of dwords currently used. |
| `remainingdwords` | `uint32_t` | Number of dwords remaining. |
| `message` | `const char*` | Validation message or error description. |

---

## Direct Memory

### sceGnmDirectMemoryAllocate

```c
GnmError PS4_SYSV_ABI sceGnmDirectMemoryAllocate(
    GnmDirectMemory* memory, uint64_t size, uint64_t alignment,
    int32_t memorytype, int32_t protection
);
```

Allocates a block of direct (GPU-visible) memory. The `memorytype` selects the memory pool (e.g. `GNM_DIRECT_MEMORY_TYPE_WC_GARLIC`), and `protection` sets access permissions (e.g. `GNM_PROT_CPU_GPU_RW`).

### sceGnmDirectMemoryRelease

```c
void PS4_SYSV_ABI sceGnmDirectMemoryRelease(GnmDirectMemory* memory);
```

Releases a previously allocated direct memory block.

---

## VideoOut

### sceGnmVideoOutInitDefaultCreateInfo

```c
void PS4_SYSV_ABI sceGnmVideoOutInitDefaultCreateInfo(
    GnmVideoOutCreateInfo* info, uint32_t width, uint32_t height
);
```

Initialises a `GnmVideoOutCreateInfo` with default settings for the given resolution. Uses A8B8G8R8 sRGB, linear tiling, double-buffering, 64 KiB alignment, and 60 Hz flip rate.

### sceGnmVideoOutCalcBufferLayout

```c
GnmError PS4_SYSV_ABI sceGnmVideoOutCalcBufferLayout(
    const GnmVideoOutCreateInfo* info, uint64_t* outbuffersize,
    uint64_t* outbufferstride
);
```

Calculates the per-buffer size and stride for the given VideoOut configuration.

### sceGnmVideoOutOpen

```c
GnmError PS4_SYSV_ABI sceGnmVideoOutOpen(
    GnmVideoOut* videoout, const GnmVideoOutCreateInfo* info
);
```

Opens a VideoOut display output, allocates direct memory for the display buffers, and registers them with the kernel.

### sceGnmVideoOutClose

```c
void PS4_SYSV_ABI sceGnmVideoOutClose(GnmVideoOut* videoout);
```

Closes a VideoOut display output and releases all associated resources.

### sceGnmVideoOutGetBuffer

```c
void* PS4_SYSV_ABI sceGnmVideoOutGetBuffer(
    const GnmVideoOut* videoout, uint32_t bufferindex
);
```

Returns a CPU-mapped pointer to the specified display buffer.

### sceGnmVideoOutSubmitFlipAndWait

```c
GnmError PS4_SYSV_ABI sceGnmVideoOutSubmitFlipAndWait(
    GnmVideoOut* videoout, uint32_t bufferindex, int64_t fliparg,
    int32_t flipmode
);
```

Submits a flip request for the specified buffer and blocks until the flip completes.

---

## Texture Creation

### sceGnmTexInit2dCreateInfo

```c
void PS4_SYSV_ABI sceGnmTexInit2dCreateInfo(
    GnmTextureCreateInfo* info, GnmDataFormat format, uint32_t width,
    uint32_t height, uint32_t mipcount, GnmTileMode tilemode,
    GnmGpuMode mingpumode
);
```

Initialises a `GnmTextureCreateInfo` for a 2D texture with the specified format, dimensions, mip count, and tile mode.

### sceGnmTexCreate2d

```c
GnmError PS4_SYSV_ABI sceGnmTexCreate2d(
    GnmTexture* texture, void* baseaddr, GnmDataFormat format,
    uint32_t width, uint32_t height, uint32_t mipcount,
    GnmTileMode tilemode, GnmGpuMode mingpumode, uint64_t* outsize,
    uint32_t* outalignment
);
```

Creates a 2D `GnmTexture` object at the given base address. Returns the required size and alignment in `*outsize` and `*outalignment`.

---

## Render Target Creation

### sceGnmRtInitColorTargetCreateInfo

```c
void PS4_SYSV_ABI sceGnmRtInitColorTargetCreateInfo(
    GnmRenderTargetCreateInfo* info, GnmDataFormat format, uint32_t width,
    uint32_t height, uint32_t numslices, uint32_t numsamples,
    uint32_t numfragments, GnmTileMode tilemode, GnmGpuMode mingpumode
);
```

Initialises a `GnmRenderTargetCreateInfo` for a color render target with the specified format, dimensions, MSAA parameters, and tile mode.

### sceGnmRtCreateColorTarget

```c
GnmError PS4_SYSV_ABI sceGnmRtCreateColorTarget(
    GnmRenderTarget* rt, void* baseaddr, GnmDataFormat format,
    uint32_t width, uint32_t height, uint32_t numslices,
    uint32_t numsamples, uint32_t numfragments, GnmTileMode tilemode,
    GnmGpuMode mingpumode, uint64_t* outsize, uint32_t* outalignment
);
```

Creates a color `GnmRenderTarget` at the given base address. Returns the required size and alignment in `*outsize` and `*outalignment`.

---

## Shader Metadata

### sceGnmShaderBinaryGetMetadata

```c
GnmError PS4_SYSV_ABI sceGnmShaderBinaryGetMetadata(
    const void* data, size_t size, GnmShaderMetadata* outmetadata
);
```

Parses a shader binary and extracts metadata (type, version, stage data, input usage slots, shader code pointer) into `*outmetadata`.

---

## Command Buffer Validation

### sceGnmCmdValidate

```c
GnmError PS4_SYSV_ABI sceGnmCmdValidate(
    const GnmCommandBuffer* cmd, GnmCommandBufferValidationInfo* outinfo
);
```

Validates a command buffer and reports capacity, usage, and any validation messages in `*outinfo`.

---

## See Also

- [GNM Driver API](driver.md) — runtime driver functions for draw, dispatch, submit, and shader binding
- [GPU Address API](gpuaddr.md) — surface tiling, decompression, and address computation
- [API Index](index.md) — complete function listing across all headers
