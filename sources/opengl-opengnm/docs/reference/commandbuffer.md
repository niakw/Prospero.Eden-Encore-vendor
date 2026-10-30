# Command Buffer

API reference for the GNM command buffer, the core object used to record GPU commands for submission to the graphics queue.

Header: `gnm_commandbuffer.h`

---

## Types

### GnmCommandCallbackFunc

Function pointer type for a command buffer callback. Invoked when the command buffer needs to emit or process a command.

```c
typedef bool (*GnmCommandCallbackFunc)(
    GnmCommandBuffer* cb, uint32_t sizedwords, void* userdata
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `GnmCommandBuffer*` | The command buffer being processed |
| `sizedwords` | `uint32_t` | Size of the command in DWORDs |
| `userdata` | `void*` | User-supplied opaque pointer |

**Returns:** `bool` — `true` if the callback handled the command successfully.

---

### GnmCommandCallback

Pairs a callback function with its user data pointer.

```c
typedef struct {
    GnmCommandCallbackFunc func;
    void* userdata;
} GnmCommandCallback;
```

| Field | Type | Description |
|-------|------|-------------|
| `func` | `GnmCommandCallbackFunc` | The callback function to invoke |
| `userdata` | `void*` | Opaque pointer passed to `func` on each call |

---

### GnmCommandBufferFlags

Bitfield flags controlling command buffer behavior.

```c
typedef struct {
    uint64_t predication_enabled : 1;
    uint64_t shadertype : 1;
    uint64_t _unused : 62;
} GnmCommandBufferFlags;
```

| Field | Type (bits) | Description |
|-------|-------------|-------------|
| `predation_enabled` | `uint64_t : 1` | Whether predation is enabled for this buffer |
| `shadertype` | `uint64_t : 1` | Shader type flag |
| `_unused` | `uint64_t : 62` | Reserved / unused bits |

---

### GnmCommandBuffer

The command buffer structure. Occupies exactly 0x40 (64) bytes.

```c
struct GnmCommandBuffer {
    uint32_t* beginptr;
    uint32_t* endptr;
    uint32_t* cmdptr;

    GnmCommandCallback callback;
    GnmCommandBufferFlags flags;

    uint64_t _unused;
    uint32_t sizedwords;
    uint32_t _unused2;
};
```

| Field | Type | Offset | Description |
|-------|------|--------|-------------|
| `beginptr` | `uint32_t*` | `0x00` | Pointer to the start of the command buffer memory |
| `endptr` | `uint32_t*` | `0x08` | Pointer to the end (one past the last usable DWORD) of the buffer |
| `cmdptr` | `uint32_t*` | `0x10` | Current write cursor — next command will be written here |
| `callback` | `GnmCommandCallback` | `0x18` | Callback function and user data for command emission |
| `flags` | `GnmCommandBufferFlags` | `0x28` | Bitfield flags (predation, shader type) |
| `_unused` | `uint64_t` | `0x30` | Reserved |
| `sizedwords` | `uint32_t` | `0x38` | Total size of the buffer in DWORDs |
| `_unused2` | `uint32_t` | `0x3C` | Reserved |

#### Static Assert

```c
_Static_assert(sizeof(GnmCommandBuffer) == 0x40, "");
```

The structure is guaranteed to be exactly 64 bytes.

---

## Functions

### sceGnmCmdInit

Initializes a `GnmCommandBuffer` from a block of memory and an optional callback.

```c
GnmCommandBuffer sceGnmCmdInit(
    void* buffer, uint32_t bytesize, GnmCommandCallbackFunc* cb, void* cbdata
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `buffer` | `void*` | Pointer to the memory backing the command buffer |
| `bytesize` | `uint32_t` | Size of `buffer` in bytes |
| `cb` | `GnmCommandCallbackFunc*` | Optional callback function (may be `NULL`) |
| `cbdata` | `void*` | Opaque data passed to the callback |

**Returns:** `GnmCommandBuffer` — an initialized command buffer value.

---

### sceGnmCmdReset

Resets the command buffer's write cursor back to the beginning, discarding all previously recorded commands. This is an inline function.

```c
static inline void sceGnmCmdReset(GnmCommandBuffer* cmd) {
    cmd->cmdptr = cmd->beginptr;
}
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `cmd` | `GnmCommandBuffer*` | The command buffer to reset |

---

### sceGnmCmdAllocInside

Allocates space inside the command buffer with a specified size and alignment.

```c
void* sceGnmCmdAllocInside(
    GnmCommandBuffer* cmd, uint32_t size, uint32_t alignment
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `cmd` | `GnmCommandBuffer*` | The command buffer to allocate from |
| `size` | `uint32_t` | Number of bytes to allocate |
| `alignment` | `uint32_t` | Required alignment of the returned pointer |

**Returns:** `void*` — pointer to the allocated region, or `NULL` on failure.

---

## See Also

- [Draw Command Buffer](drawcommandbuffer.md)
- [Platform](platform.md)
- [Compatibility Layer](compat.md)
