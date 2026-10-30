# Buffer

The `GnmBuffer` struct describes a GPU buffer resource (V# / V-sharp) — used for vertex buffers, constant buffers (uniform buffers), and other structured GPU memory accesses. This header (`gnm_buffer.h`) defines the buffer descriptor, its 4-register bitfield layout, and functions for creating and querying buffer resources.

---

## GnmBuffer Struct

A 16-byte (`0x10` bytes) buffer descriptor consisting of 4 registers, each 32 bits wide.

```c
typedef struct {
    /* register 0 */
    uint32_t baseaddress;

    /* register 1 */
    uint32_t baseaddresshi : 12;
    uint32_t mtype_l1s : 2;
    uint32_t mtype_l2 : 2;
    uint32_t stride : 14;
    uint32_t cacheswizzle : 1;
    uint32_t swizzleen : 1;

    /* register 2 */
    uint32_t numrecords;

    /* register 3 */
    GnmChannel dstselx : 3;
    GnmChannel dstsely : 3;
    GnmChannel dstselz : 3;
    GnmChannel dstselw : 3;
    GnmBufNumFormat numformat : 3;
    GnmBufferFormat dataformat : 4;
    uint32_t elementsize : 2;
    uint32_t indexstride : 2;
    uint32_t addtiden : 1;
    uint32_t atc : 1;
    uint32_t hashen : 1;
    uint32_t heap : 1;
    uint32_t mtype : 3;
    uint32_t type : 2;
} GnmBuffer;
```

_Static_assert: `sizeof(GnmBuffer) == 0x10`

### Field Reference

| Register | Field | Bits | Type | Description |
|---|---|---|---|---|
| 0 | `baseaddress` | 32 | `uint32_t` | Low 32 bits of the buffer's GPU virtual base address |
| 1 | `baseaddresshi` | 12 | `uint32_t` | High 12 bits of the base address (bits 32–43) |
| 1 | `mtype_l1s` | 2 | `uint32_t` | L1 shader cache memory type |
| 1 | `mtype_l2` | 2 | `uint32_t` | L2 cache memory type |
| 1 | `stride` | 14 | `uint32_t` | Stride between elements in bytes |
| 1 | `cacheswizzle` | 1 | `uint32_t` | Enable cache swizzle |
| 1 | `swizzleen` | 1 | `uint32_t` | Enable swizzle |
| 2 | `numrecords` | 32 | `uint32_t` | Number of records (elements) in the buffer |
| 3 | `dstselx` | 3 | `GnmChannel` | Destination channel X selection |
| 3 | `dstsely` | 3 | `GnmChannel` | Destination channel Y selection |
| 3 | `dstselz` | 3 | `GnmChannel` | Destination channel Z selection |
| 3 | `dstselw` | 3 | `GnmChannel` | Destination channel W selection |
| 3 | `numformat` | 3 | `GnmBufNumFormat` | Numeric format (UNORM, SNORM, UINT, FLOAT, etc.) |
| 3 | `dataformat` | 4 | `GnmBufferFormat` | Data format (component bit-width layout) |
| 3 | `elementsize` | 2 | `uint32_t` | Element size encoding |
| 3 | `indexstride` | 2 | `uint32_t` | Index stride encoding |
| 3 | `addtiden` | 1 | `uint32_t` | Add thread ID enable |
| 3 | `atc` | 1 | `uint32_t` | ATC (address translation cache) enable |
| 3 | `hashen` | 1 | `uint32_t` | Hash enable |
| 3 | `heap` | 1 | `uint32_t` | Heap selection |
| 3 | `mtype` | 3 | `uint32_t` | Memory type (combined mtype field) |
| 3 | `type` | 2 | `uint32_t` | Buffer type |

---

## Functions

### sceGnmBufGetBaseAddress

Returns the full 64-bit GPU virtual base address of the buffer. *(static inline)*

```c
static inline void* sceGnmBufGetBaseAddress(const GnmBuffer* buf);
```

| Parameter | Type | Description |
|---|---|---|
| `buf` | `const GnmBuffer*` | Pointer to the buffer descriptor |

**Returns:** The 64-bit base address as a `void*`, combining `baseaddress` and `baseaddresshi`.

---

### sceGnmBufSetBaseAddress

Sets the GPU virtual base address of the buffer. The address must be aligned to 4 bytes; otherwise an error message is emitted and the function returns without modifying the buffer. *(static inline)*

```c
static inline void sceGnmBufSetBaseAddress(GnmBuffer* buf, void* baseaddr);
```

| Parameter | Type | Description |
|---|---|---|
| `buf` | `GnmBuffer*` | Pointer to the buffer descriptor to modify |
| `baseaddr` | `void*` | The 64-bit base address (must be 4-byte aligned) |

---

### sceGnmBufGetFormat

Retrieves the buffer's data format as a `GnmDataFormat`. *(static inline)*

```c
static inline GnmDataFormat sceGnmBufGetFormat(const GnmBuffer* buf);
```

| Parameter | Type | Description |
|---|---|---|
| `buf` | `const GnmBuffer*` | Pointer to the buffer descriptor |

**Returns:** A `GnmDataFormat` constructed from the buffer's `dataformat`, `numformat`, and channel selection fields.

---

### sceGnmBufSetFormat

Sets the buffer's data format from a `GnmDataFormat`. Validates that the channel type is within the buffer format range (`GNM_BUF_NUM_FORMAT_UNORM` to `GNM_BUF_NUM_FORMAT_FLOAT`) and the surface format is valid for buffers (`GNM_BUF_DATA_FORMAT_8` to `GNM_BUF_DATA_FORMAT_32_32_32_32`). Emits an error message if the format is unsupported. *(static inline)*

```c
static inline void sceGnmBufSetFormat(GnmBuffer* buf, GnmDataFormat fmt);
```

| Parameter | Type | Description |
|---|---|---|
| `buf` | `GnmBuffer*` | Pointer to the buffer descriptor to modify |
| `fmt` | `GnmDataFormat` | The format to apply |

---

### sceGnmBufSetMemoryType

Sets the memory type and cache bypass attributes for the buffer. *(static inline)*

```c
static inline void sceGnmBufSetMemoryType(
    GnmBuffer* buf, GnmMemoryType memtype, bool l1cachebypass, bool kcachebypass
);
```

| Parameter | Type | Description |
|---|---|---|
| `buf` | `GnmBuffer*` | Pointer to the buffer descriptor to modify |
| `memtype` | `GnmMemoryType` | GPU memory type (e.g. `GNM_MEMORY_READONLY`) |
| `l1cachebypass` | `bool` | If `true`, bypass the L1 cache |
| `kcachebypass` | `bool` | If `true`, bypass the k-cache (L1 shader) |

---

### sceGnmCreateConstBuffer

Creates a `GnmBuffer` descriptor for a constant (uniform) buffer. The buffer is configured with stride 16, `GNM_FMT_R32G32B32A32_FLOAT` format, and `GNM_MEMORY_READONLY` memory type. *(static inline)*

```c
static inline GnmBuffer sceGnmCreateConstBuffer(
    void* baseaddr, uint32_t bytesize
);
```

| Parameter | Type | Description |
|---|---|---|
| `baseaddr` | `void*` | GPU virtual base address (must be 4-byte aligned) |
| `bytesize` | `uint32_t` | Total byte size of the constant buffer data |

**Returns:** A configured `GnmBuffer` descriptor. The `numrecords` field is set to `(bytesize + 15) / 16` (number of 16-byte elements).

---

### sceGnmCreateVertexBuffer

Creates a `GnmBuffer` descriptor for a vertex buffer. Validates that the base address is properly aligned (based on element size, minimum 4 bytes) and that if stride is 0, `numelements` matches the format's element byte size. *(static inline)*

```c
static inline GnmBuffer sceGnmCreateVertexBuffer(
    void* baseaddr, GnmDataFormat fmt, uint32_t stride, uint32_t numelements
);
```

| Parameter | Type | Description |
|---|---|---|
| `baseaddr` | `void*` | GPU virtual base address (alignment depends on element size) |
| `fmt` | `GnmDataFormat` | Vertex element data format |
| `stride` | `uint32_t` | Stride between vertices in bytes (0 = tightly packed) |
| `numelements` | `uint32_t` | Number of vertex elements in the buffer |

**Returns:** A configured `GnmBuffer` descriptor with `GNM_MEMORY_READONLY` memory type.

**Validation:**

- If `baseaddr` is not aligned to `max(elementbytesize, 4)`, an error message is emitted.
- If `stride == 0` and `numelements != elementbytesize`, an error message is emitted.

---

## See Also

- [Types & Enums](types.md) — `GnmBufferFormat`, `GnmBufNumFormat`, `GnmChannel`, `GnmMemoryType`
- [Data Format](dataformat.md) — `GnmDataFormat` and predefined `GNM_FMT_*` constants
- [Error Handling](error.md) — `sceGnmWriteMsg` / `sceGnmWriteMsgf` used for validation
- [Texture](texture.md) — `GnmTexture` (T#) resource descriptor
